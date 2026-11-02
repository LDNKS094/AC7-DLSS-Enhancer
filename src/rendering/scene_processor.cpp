#include "scene_processor.h"
#include "ac7_scene_convert.h"
#include "ac7_scene_vertex.h"
#include "ac7_scene_pixel.h"
#include <stdexcept>
#include <string>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Late SR HRESULT "+std::to_string(hr));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
}
SceneProcessor::SceneProcessor(GpuContext&gpu):gpu_(gpu),device11_(gpu.sourceDevice()){
    auto*d=device11_.Get();
    d->GetImmediateContext(&context_);check(d->CreateDeferredContext(0,&deferred_));
    check(d->CreateComputeShader(ac7_scene_convert,sizeof(ac7_scene_convert),nullptr,&convert_));
    check(d->CreateVertexShader(ac7_scene_vertex,sizeof(ac7_scene_vertex),nullptr,&vertex_));
    check(d->CreatePixelShader(ac7_scene_pixel,sizeof(ac7_scene_pixel),nullptr,&pixel_));
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=32;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(d->CreateBuffer(&cb,nullptr,&constants_));
    CD3D11_SAMPLER_DESC sampler(D3D11_DEFAULT);sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;check(d->CreateSamplerState(&sampler,&sampler_));
    CD3D11_RASTERIZER_DESC raster(D3D11_DEFAULT);raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;check(d->CreateRasterizerState(&raster,&raster_));CD3D11_DEPTH_STENCIL_DESC depth(D3D11_DEFAULT);depth.DepthEnable=FALSE;check(d->CreateDepthStencilState(&depth,&depthState_));CD3D11_BLEND_DESC blend(D3D11_DEFAULT);blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;check(d->CreateBlendState(&blend,&blend_));
}
bool SceneProcessor::drained(){return !bridge_||(!bridge_->busy()&&bridge_->completedValue()>=bridge_->submittedValue());}
void SceneProcessor::invalidate(){prepared_=false;if(models_)models_->invalidateHistory();}
void SceneProcessor::inputLayout(unsigned w,unsigned h){
    normalizer_=std::make_unique<GuideNormalizer>(device11_.Get(),w,h);prepared_=false;color_.Reset();colorSrv_.Reset();colorUav_.Reset();width_=w;height_=h;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;check(device11_->CreateTexture2D(&desc,nullptr,&color_));check(device11_->CreateShaderResourceView(color_.Get(),nullptr,&colorSrv_));check(device11_->CreateUnorderedAccessView(color_.Get(),nullptr,&colorUav_));
}
bool SceneProcessor::reserve(unsigned w,unsigned h,unsigned ow,unsigned oh,bool srEnabled,const NrSettings&settings){
    if(!w||!h||ow<w||oh<h)return false;
    const unsigned nrWidth=edgeOptimizationLayout(w,h,settings).workWidth;
    if(w!=width_||h!=height_||ow!=outWidth_||oh!=outHeight_||srEnabled!=srEnabled_||nrWidth!=nrWidth_||!bridge_){
        if(!drained())return false;
        if(w!=width_||h!=height_)inputLayout(w,h);
        layout(w,h,ow,oh,srEnabled,nrWidth);
    }
    return !bridge_->busy()&&bridge_->completedValue()>=slots_[next_].fence;
}
bool SceneProcessor::prepare(ID3D11DeviceContext*c,const ViewIdentity&id,ID3D11Texture2D*depth,ID3D11Texture2D*motion){
    if(prepared_&&id==view_)return true;
    const unsigned w=unsigned(id.rect[2]),h=unsigned(id.rect[3]);
    if(w!=width_||h!=height_){if(!drained())return false;inputLayout(w,h);}
    ClipToPrevious transform;std::memcpy(transform.data(),id.bytes.data()+110*16,64);normalizer_->record(c,depth,motion,{unsigned(id.rect[0]),unsigned(id.rect[1]),w,h},transform,{2*id.jitter[0]/w,-2*id.jitter[1]/h});
    view_=id;prepared_=true;return true;
}
std::array<ID3D11Texture2D*,2>SceneProcessor::preparedGuides()const{
    if(!prepared_||!normalizer_)throw std::logic_error("Normalized guides requested outside their prepared View");
    return {normalizer_->depth(),normalizer_->motion()};
}
void SceneProcessor::layout(unsigned w,unsigned h,unsigned ow,unsigned oh,bool srEnabled,unsigned nrWidth){
    gpu_.initializeDevice();
    models_.reset();bridge_.reset();outputSrv_.Reset();
    bridge_=std::make_unique<SharedTextureBridge>(device11_.Get(),gpu_.device(),gpu_.queue(),w,h,ow,oh);
    models_=std::make_unique<NgxModels>(gpu_,w,h,ow,oh,srEnabled,nrWidth);models_->initialize();
    for(auto&s:slots_){s.fence=0;if(s.allocator)continue;check(gpu_.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator)));check(gpu_.device()->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list)));check(s.list->Close());}
    check(device11_->CreateShaderResourceView(bridge_->outputTexture(),nullptr,&outputSrv_));outWidth_=ow;outHeight_=oh;srEnabled_=srEnabled;nrWidth_=nrWidth;next_=0;
}
void SceneProcessor::drawCopy(ID3D11RenderTargetView*target,D3D11_VIEWPORT viewport,ID3D11ShaderResourceView*output,ComPtr<ID3D11CommandList>&commands){
    deferred_->OMSetRenderTargets(1,&target,nullptr);deferred_->OMSetBlendState(blend_.Get(),nullptr,0xffffffff);deferred_->OMSetDepthStencilState(depthState_.Get(),0);deferred_->RSSetState(raster_.Get());deferred_->RSSetViewports(1,&viewport);deferred_->IASetInputLayout(nullptr);deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);deferred_->VSSetShader(vertex_.Get(),nullptr,0);deferred_->HSSetShader(nullptr,nullptr,0);deferred_->DSSetShader(nullptr,nullptr,0);deferred_->GSSetShader(nullptr,nullptr,0);deferred_->PSSetShader(pixel_.Get(),nullptr,0);ID3D11ShaderResourceView*views[]={output,colorSrv_.Get()};deferred_->PSSetShaderResources(0,2,views);ID3D11SamplerState*sampler=sampler_.Get();deferred_->PSSetSamplers(0,1,&sampler);deferred_->Draw(3,0);check(deferred_->FinishCommandList(FALSE,&commands));
}
bool SceneProcessor::draw(ID3D11DeviceContext*c,ID3D11Texture2D*source,ID3D11RenderTargetView*target,D3D11_VIEWPORT viewport,uint64_t token,uint64_t sequence,const NrSettings&settings,float sharpness){
    if(!prepared_||c!=context_.Get()||!source||!target)return false;
    const unsigned ow=unsigned(viewport.Width),oh=unsigned(viewport.Height);
    if(ow<width_||oh<height_||!ow||!oh)return false;
    const auto inputDesc=bridge_?bridge_->resource(0)->GetDesc():D3D12_RESOURCE_DESC{};
    if(!bridge_||inputDesc.Width!=width_||inputDesc.Height!=height_||ow!=outWidth_||oh!=outHeight_){if(!drained())return false;layout(width_,height_,ow,oh,srEnabled_,nrWidth_);}
    auto&s=slots_[next_];if(bridge_->completedValue()<s.fence)return false;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
    if(desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)return false;
    srv.Format=desc.Format;
    if(unsigned(view_.rect[0])+width_>desc.Width||unsigned(view_.rect[1])+height_>desc.Height)return false;
    if(source_.Get()!=source){
        ComPtr<ID3D11ShaderResourceView>next;check(device11_->CreateShaderResourceView(source,&srv,&next));
        sourceSrv_=std::move(next);source_=source;
    }
    const unsigned rect[]={unsigned(view_.rect[0]),unsigned(view_.rect[1]),width_,height_,0,0,0,0};deferred_->UpdateSubresource(constants_.Get(),0,nullptr,rect,0,0);deferred_->CSSetShader(convert_.Get(),nullptr,0);ID3D11Buffer*cb=constants_.Get();deferred_->CSSetConstantBuffers(0,1,&cb);auto*in=sourceSrv_.Get();auto*uav=colorUav_.Get();deferred_->CSSetShaderResources(0,1,&in);deferred_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);deferred_->Dispatch((width_+7)/8,(height_+7)/8,1);ComPtr<ID3D11CommandList>convert;check(deferred_->FinishCommandList(FALSE,&convert));context_->ExecuteCommandList(convert.Get(),TRUE);
    check(s.allocator->Reset());check(s.list->Reset(s.allocator.Get(),nullptr));for(unsigned i=0;i<3;i++)transition(s.list.Get(),bridge_->resource(i),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(s.list.Get(),bridge_->resource(3),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    models_->recordScene(s.list.Get(),bridge_->resource(0),bridge_->resource(1),bridge_->resource(2),bridge_->resource(3),token,view_.current[1],view_.previous[1],sequence,view_.cameraCut,view_.jitter,sharpness,settings,next_);
    transition(s.list.Get(),bridge_->resource(3),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    const bool nrEnabled=settings.enabled;
    for(unsigned i=0;i<3;i++)transition(s.list.Get(),bridge_->resource(i),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);check(s.list->Close());const std::array<ID3D11Texture2D*,2>normalized={normalizer_->depth(),normalizer_->motion()};bridge_->uploadScene(token,color_.Get(),normalized);bridge_->submit(s.list.Get());s.fence=bridge_->submittedValue();
    ComPtr<ID3D11CommandList>commands;drawCopy(target,viewport,outputSrv_.Get(),commands);bridge_->queueOutputDraw(token,commands.Get());models_->commit(token,view_.current[1],nrEnabled,settings,sequence);next_=(next_+1)%slots_.size();prepared_=false;return true;
}
}
