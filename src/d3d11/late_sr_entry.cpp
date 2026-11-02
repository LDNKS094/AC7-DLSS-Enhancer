#include "late_sr_entry.h"
#include "../d3d12/nr_capture.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Late SR HRESULT "+std::to_string(hr));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
constexpr char shader[]=R"(
cbuffer C:register(b0){uint left,top,width,height,sourceSrgb,pad0,pad1,pad2;};
Texture2D<float4> Input:register(t0);Texture2D<float4> Low:register(t1);SamplerState Linear:register(s0);RWTexture2D<float4> Converted:register(u0);
[numthreads(8,8,1)]void Convert(uint3 p:SV_DispatchThreadID){if(p.x<width&&p.y<height){float4 c=Input.Load(int3(p.xy+uint2(left,top),0));if(sourceSrgb)c.rgb=lerp(c.rgb*12.92,1.055*pow(max(c.rgb,0),1.0/2.4)-.055,step(.0031308,c.rgb));Converted[p.xy]=c;}}
struct V{float4 pos:SV_Position;float2 uv:TEXCOORD0;};
V Vertex(uint id:SV_VertexID){V v;v.uv=float2((id<<1)&2,id&2);v.pos=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;}
float4 Pixel(V v):SV_Target{float4 low=Low.SampleLevel(Linear,v.uv,0);float3 c=Input.SampleLevel(Linear,v.uv,0).rgb;c=all(isfinite(c))?(top?c:saturate(c)):low.rgb;if(left)c=lerp(c/12.92,pow((c+.055)/1.055,2.4),step(.04045,c));return float4(c,low.a);}
)";
}
LateSrEntry::LateSrEntry(ModelContext&gpu,NativeModels::ColorInput input):gpu_(gpu),device11_(gpu.sourceDevice()),colorInput_(input){
    auto*d=device11_.Get();
    d->GetImmediateContext(&context_);check(d->CreateDeferredContext(0,&deferred_));
    for(unsigned i=0;i<3;i++){ComPtr<ID3DBlob>code,error;const auto hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,i==0?"Convert":i==1?"Vertex":"Pixel",i==0?"cs_5_0":i==1?"vs_5_0":"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS,0,&code,&error);if(FAILED(hr))throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Late SR shader compile failed");if(i==0)check(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&convert_));else if(i==1)check(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex_));else check(d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&pixel_));}
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(d->CreateBuffer(&cb,nullptr,&constants_));
    CD3D11_SAMPLER_DESC sampler(D3D11_DEFAULT);sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;check(d->CreateSamplerState(&sampler,&sampler_));
    CD3D11_RASTERIZER_DESC raster(D3D11_DEFAULT);raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;check(d->CreateRasterizerState(&raster,&raster_));CD3D11_DEPTH_STENCIL_DESC depth(D3D11_DEFAULT);depth.DepthEnable=FALSE;check(d->CreateDepthStencilState(&depth,&depthState_));CD3D11_BLEND_DESC blend(D3D11_DEFAULT);blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;check(d->CreateBlendState(&blend,&blend_));
}
bool LateSrEntry::drained(){return !bridge_||(!bridge_->busy()&&bridge_->completedValue()>=bridge_->submittedValue());}
void LateSrEntry::invalidate(){prepared_=false;if(models_)models_->invalidateHistory();}
void LateSrEntry::inputLayout(unsigned w,unsigned h){
    normalizer_=std::make_unique<GuideNormalizer>(device11_.Get(),w,h,true);prepared_=false;color_.Reset();colorSrv_.Reset();colorUav_.Reset();width_=w;height_=h;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;check(device11_->CreateTexture2D(&desc,nullptr,&color_));check(device11_->CreateShaderResourceView(color_.Get(),nullptr,&colorSrv_));check(device11_->CreateUnorderedAccessView(color_.Get(),nullptr,&colorUav_));
}
bool LateSrEntry::reserve(unsigned w,unsigned h,unsigned ow,unsigned oh,bool srEnabled,const NrSettings&settings){
    if(!w||!h||ow<w||oh<h)return false;
    const unsigned nrWidth=nrSpatialLayout(w,h,settings).workWidth;
    if(w!=width_||h!=height_||ow!=outWidth_||oh!=outHeight_||srEnabled!=srEnabled_||nrWidth!=nrWidth_||!bridge_){
        if(!drained())return false;
        if(w!=width_||h!=height_)inputLayout(w,h);
        layout(w,h,ow,oh,srEnabled,nrWidth);
    }
    return !bridge_->busy()&&bridge_->completedValue()>=slots_[next_].fence;
}
bool LateSrEntry::prepare(ID3D11DeviceContext*c,const ViewIdentity&id,ID3D11Texture2D*depth,ID3D11Texture2D*motion){
    if(prepared_&&id==view_)return true;
    const unsigned w=unsigned(id.rect[2]),h=unsigned(id.rect[3]);
    if(w!=width_||h!=height_){if(!drained())return false;inputLayout(w,h);}
    ClipToPrevious transform;std::memcpy(transform.data(),id.bytes.data()+110*16,64);normalizer_->record(c,depth,motion,{unsigned(id.rect[0]),unsigned(id.rect[1]),w,h},transform,{2*id.jitter[0]/w,-2*id.jitter[1]/h});
    view_=id;prepared_=true;return true;
}
std::array<ID3D11Texture2D*,2>LateSrEntry::preparedGuides()const{
    if(!prepared_||!normalizer_)throw std::logic_error("Normalized guides requested outside their prepared View");
    return {normalizer_->depth(),normalizer_->motion()};
}
void LateSrEntry::layout(unsigned w,unsigned h,unsigned ow,unsigned oh,bool srEnabled,unsigned nrWidth){
    gpu_.initializeDevice();
    models_.reset();bridge_.reset();outputSrv_.Reset();
    bridge_=std::make_unique<SharedTextureBridge>(device11_.Get(),gpu_.device(),gpu_.queue(),w,h,SharedTextureBridge::ColorInput::ldrScene,0,0,ow,oh);
    models_=std::make_unique<NativeModels>(gpu_,w,h,ow,oh,colorInput_,srEnabled,nrWidth);models_->initialize();
    for(auto&s:slots_){s={};check(gpu_.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator)));check(gpu_.device()->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list)));check(s.list->Close());}
    check(device11_->CreateShaderResourceView(bridge_->outputTexture(),nullptr,&outputSrv_));outWidth_=ow;outHeight_=oh;srEnabled_=srEnabled;nrWidth_=nrWidth;next_=0;
}
void LateSrEntry::drawCopy(ID3D11RenderTargetView*target,D3D11_VIEWPORT viewport,ID3D11ShaderResourceView*output,bool srgb,ComPtr<ID3D11CommandList>&commands){
    const unsigned constants[]={unsigned(srgb),unsigned(colorInput_==NativeModels::ColorInput::sceneHdr),0,0,0,0,0,0};deferred_->UpdateSubresource(constants_.Get(),0,nullptr,constants,0,0);deferred_->OMSetRenderTargets(1,&target,nullptr);deferred_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff);deferred_->OMSetDepthStencilState(depthState_.Get(),0);deferred_->RSSetState(raster_.Get());deferred_->RSSetViewports(1,&viewport);deferred_->IASetInputLayout(nullptr);deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);deferred_->VSSetShader(vertex_.Get(),nullptr,0);deferred_->HSSetShader(nullptr,nullptr,0);deferred_->DSSetShader(nullptr,nullptr,0);deferred_->GSSetShader(nullptr,nullptr,0);deferred_->PSSetShader(pixel_.Get(),nullptr,0);ID3D11Buffer*cb=constants_.Get();deferred_->PSSetConstantBuffers(0,1,&cb);ID3D11ShaderResourceView*views[]={output,colorSrv_.Get()};deferred_->PSSetShaderResources(0,2,views);ID3D11SamplerState*sampler=sampler_.Get();deferred_->PSSetSamplers(0,1,&sampler);deferred_->Draw(3,0);check(deferred_->FinishCommandList(FALSE,&commands));
}
bool LateSrEntry::draw(ID3D11DeviceContext*c,ID3D11Texture2D*source,ID3D11RenderTargetView*target,D3D11_VIEWPORT viewport,uint64_t token,uint64_t sequence,NrCapture*capture,const NrSettings&settings,float sharpness){
    if(!prepared_||c!=context_.Get()||!source||!target)return false;
    const unsigned ow=unsigned(viewport.Width),oh=unsigned(viewport.Height);
    if(ow<width_||oh<height_||!ow||!oh)return false;
    const auto inputDesc=bridge_?bridge_->resource(0)->GetDesc():D3D12_RESOURCE_DESC{};
    if(!bridge_||inputDesc.Width!=width_||inputDesc.Height!=height_||ow!=outWidth_||oh!=outHeight_){if(!drained())return false;layout(width_,height_,ow,oh,srEnabled_,nrWidth_);}
    auto&s=slots_[next_];if(bridge_->completedValue()<s.fence)return false;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
    if(colorInput_==NativeModels::ColorInput::sceneHdr){if(desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)return false;srv.Format=desc.Format;}
    else if(desc.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)srv.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    else if(desc.Format==DXGI_FORMAT_B8G8R8X8_TYPELESS||desc.Format==DXGI_FORMAT_B8G8R8X8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)srv.Format=DXGI_FORMAT_B8G8R8X8_UNORM;
    else if(desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;else return false;
    const bool encodedView=desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||desc.Format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;if(encodedView)srv.Format=desc.Format;
    if(unsigned(view_.rect[0])+width_>desc.Width||unsigned(view_.rect[1])+height_>desc.Height)return false;
    if(source_.Get()!=source){
        ComPtr<ID3D11ShaderResourceView>next;check(device11_->CreateShaderResourceView(source,&srv,&next));
        sourceSrv_=std::move(next);source_=source;
    }
    const unsigned rect[]={unsigned(view_.rect[0]),unsigned(view_.rect[1]),width_,height_,unsigned(encodedView),0,0,0};deferred_->UpdateSubresource(constants_.Get(),0,nullptr,rect,0,0);deferred_->CSSetShader(convert_.Get(),nullptr,0);ID3D11Buffer*cb=constants_.Get();deferred_->CSSetConstantBuffers(0,1,&cb);auto*in=sourceSrv_.Get();auto*uav=colorUav_.Get();deferred_->CSSetShaderResources(0,1,&in);deferred_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);deferred_->Dispatch((width_+7)/8,(height_+7)/8,1);ComPtr<ID3D11CommandList>convert;check(deferred_->FinishCommandList(FALSE,&convert));context_->ExecuteCommandList(convert.Get(),TRUE);
    check(s.allocator->Reset());check(s.list->Reset(s.allocator.Get(),nullptr));for(unsigned i=0;i<3;i++)transition(s.list.Get(),bridge_->resource(i),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(s.list.Get(),bridge_->resource(4),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    models_->recordScene(s.list.Get(),bridge_->resource(0),bridge_->resource(1),bridge_->resource(2),bridge_->resource(4),token,view_.current[1],view_.previous[1],sequence,view_.cameraCut,view_.jitter,sharpness,settings,next_);
    transition(s.list.Get(),bridge_->resource(4),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    const bool nrEnabled=colorInput_==NativeModels::ColorInput::sceneHdr&&settings.enabled;
    if(capture)capture->record(s.list.Get(),bridge_->resource(0),bridge_->resource(4),{0,0,width_,height_},token,nrEnabled,settings,sequence,view_.counter,colorInput_==NativeModels::ColorInput::sceneHdr,view_.cameraCut,view_.jitter,sharpness,srEnabled_,nrWidth_);
    for(unsigned i=0;i<3;i++)transition(s.list.Get(),bridge_->resource(i),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);check(s.list->Close());const std::array<ID3D11Texture2D*,2>normalized={normalizer_->depth(),normalizer_->motion()};bridge_->uploadScene(token,color_.Get(),&normalized);bridge_->submit(s.list.Get());s.fence=bridge_->submittedValue();if(capture)capture->submitted(gpu_.queue());
    D3D11_RENDER_TARGET_VIEW_DESC tv{};target->GetDesc(&tv);const bool srgb=tv.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||tv.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||tv.Format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;ComPtr<ID3D11CommandList>commands;drawCopy(target,viewport,outputSrv_.Get(),srgb,commands);bridge_->queueOutputDraw(token,commands.Get());models_->commit(token,view_.current[1],nrEnabled,settings,sequence);next_=(next_+1)%slots_.size();prepared_=false;return true;
}
}
