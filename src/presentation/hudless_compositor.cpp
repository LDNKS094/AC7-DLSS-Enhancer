#include "hudless_compositor.h"
#include "../game/frame_hooks.h"
#include "swap_chain_bridge.h"
#include "../nvidia/streamline_session.h"
#include <MinHook.h>
#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>

namespace ac7 {
namespace {
using Microsoft::WRL::ComPtr;
using Draw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using DrawIndexed=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
struct Hooks {
    std::mutex lock;
    std::atomic<Draw>draw{nullptr};
    std::atomic<DrawIndexed>indexed{nullptr};
    void*drawTarget=nullptr;void*indexedTarget=nullptr;
    void(*failure)(const char*)noexcept=nullptr;
};
Hooks&hooks=*new Hooks;
struct Neutral {
    D3D11_TEXTURE2D_DESC texture{};
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    ComPtr<ID3D11Device>device;
    ComPtr<ID3D11ShaderResourceView>srv;
};
struct Capture {
    std::shared_ptr<const FinalCompositeInputs>inputs;
    bool active=false,captured=false,ambiguous=false,replaying=false;
    const char*refusal="Final compositor did not draw matching scene and UI inputs";
    ComPtr<ID3D11RenderTargetView>originalTarget;
    std::array<Neutral,4>neutral;
};
// Pin cached D3D objects with the session; do not release them from DLL thread-detach.
thread_local Capture&capture=*new Capture;
[[noreturn]] void fatal(const char*message)noexcept{
    if(hooks.failure)hooks.failure(message);
    OutputDebugStringA(message);RaiseFailFastException(nullptr,nullptr,0);std::terminate();
}
void check(HRESULT hr,const char*stage){if(FAILED(hr))throw std::runtime_error(std::string(stage)+" HRESULT "+std::to_string(unsigned(hr)));}
ID3D11ShaderResourceView*neutralView(ID3D11DeviceContext*context,ID3D11ShaderResourceView*original,unsigned role){
    ComPtr<ID3D11Resource>resource;original->GetResource(&resource);
    ComPtr<ID3D11Texture2D>texture;check(resource.As(&texture),"Get UI neutral texture layout");
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};original->GetDesc(&view);
    if(desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1||view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Texture2D.MostDetailedMip!=0){
        capture.refusal="HUDless requires single-mip, single-sample 2D UI inputs";return nullptr;
    }
    auto&cached=capture.neutral[role];ComPtr<ID3D11Device>device;texture->GetDevice(&device);
    if(cached.srv&&cached.device==device&&cached.texture.Width==desc.Width&&cached.texture.Height==desc.Height&&cached.texture.Format==desc.Format&&cached.view.Format==view.Format&&cached.view.Texture2D.MipLevels==view.Texture2D.MipLevels)return cached.srv.Get();
    auto layout=desc;layout.Usage=D3D11_USAGE_DEFAULT;layout.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;layout.CPUAccessFlags=layout.MiscFlags=0;
    ComPtr<ID3D11Texture2D>black;check(device->CreateTexture2D(&layout,nullptr,&black),"Create neutral UI texture");
    D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=view.Format;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView>target;check(device->CreateRenderTargetView(black.Get(),&rtv,&target),"Create neutral UI target");
    ComPtr<ID3D11ShaderResourceView>srv;check(device->CreateShaderResourceView(black.Get(),&view,&srv),"Create neutral UI view");
    const float zero[4]{};context->ClearRenderTargetView(target.Get(),zero);
    cached.texture=desc;cached.view=view;cached.device=std::move(device);cached.srv=std::move(srv);return cached.srv.Get();
}
template<class F>void replayHudless(ID3D11DeviceContext*context,F&&draw){
    if(!capture.active||capture.replaying)return;
    struct Replay {Replay(){capture.replaying=true;}~Replay(){capture.replaying=false;}}replay;
    // Any later compositor write invalidates an earlier View's snapshot, even on rejection.
    ComPtr<ID3D11RenderTargetView>currentTarget;context->OMGetRenderTargets(1,&currentTarget,nullptr);discardHudlessTarget(currentTarget.Get());
    if(capture.captured||capture.ambiguous){
        discardHudlessTarget(capture.originalTarget.Get());capture.captured=false;capture.ambiguous=true;
        capture.refusal="Final compositor has multiple draws; HUDless input is ambiguous";return;
    }
    if(!capture.inputs)return;
    if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){capture.refusal="HUDless compositor is not on the immediate RHI context";return;}
    const auto&frame=*capture.inputs;
    ComPtr<ID3D11Predicate>predicate;BOOL predication=FALSE;context->GetPredication(&predicate,&predication);
    if(predicate){capture.refusal="HUDless compositor uses GPU predication";return;}
    std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT>streamOutputs{};context->SOGetTargets(UINT(streamOutputs.size()),streamOutputs.data());
    bool hasStreamOutput=false;for(auto*buffer:streamOutputs)if(buffer){hasStreamOutput=true;buffer->Release();}
    if(hasStreamOutput){capture.refusal="HUDless compositor has stream-output side effects";return;}
    constexpr unsigned count=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    std::array<ID3D11ShaderResourceView*,count>original{};context->PSGetShaderResources(0,count,original.data());
    std::array<ComPtr<ID3D11ShaderResourceView>,count>views;
    for(unsigned i=0;i<count;i++)views[i].Attach(original[i]);
    auto replaced=original;bool scene=false,ui=false;
    std::array<int,count>roles;roles.fill(-1);
    for(unsigned i=0;i<count;i++)if(original[i]){
        ComPtr<ID3D11Resource>resource;original[i]->GetResource(&resource);
        ComPtr<ID3D11Texture2D>texture;if(FAILED(resource.As(&texture)))continue;
        scene=scene||texture==frame.scene;
        for(unsigned role=0;role<frame.ui.size();role++)if(frame.ui[role]&&texture==frame.ui[role]){roles[i]=int(role);ui=true;break;}
    }
    bool expectedUi=false;for(const auto&input:frame.ui)expectedUi=expectedUi||bool(input);
    if(!scene||(expectedUi&&!ui))return;
    ComPtr<ID3D11BlendState>blend;float factors[4]{};UINT mask=0;context->OMGetBlendState(&blend,factors,&mask);
    if(!(mask&1)){capture.refusal="HUDless compositor masks out the output sample";return;}
    if(blend){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);if(desc.AlphaToCoverageEnable||desc.RenderTarget[0].BlendEnable){capture.refusal="HUDless compositor requires opaque output blending";return;}}
    std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>originalTargets{};
    std::array<ID3D11UnorderedAccessView*,D3D11_PS_CS_UAV_REGISTER_COUNT>uavs{};
    ComPtr<ID3D11DepthStencilView>depth;context->OMGetRenderTargets(UINT(originalTargets.size()),originalTargets.data(),&depth);
    std::array<ComPtr<ID3D11RenderTargetView>,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>targets;
    for(unsigned i=0;i<targets.size();i++)targets[i].Attach(originalTargets[i]);
    context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,UINT(uavs.size()),uavs.data());
    bool hasUav=false;for(auto*uav:uavs)if(uav){hasUav=true;uav->Release();}
    if(hasUav){capture.refusal="HUDless compositor has pixel UAV side effects";return;}
    if(!targets[0]){capture.refusal="Final compositor has no color target";return;}
    for(unsigned i=1;i<targets.size();i++)if(targets[i]){capture.refusal="HUDless compositor has multiple color outputs";return;}
    if(depth){
        ComPtr<ID3D11DepthStencilState>state;UINT stencil=0;context->OMGetDepthStencilState(&state,&stencil);
        if(!state){capture.refusal="HUDless compositor uses the default depth state";return;}
        D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);
        if(desc.DepthEnable||desc.StencilEnable){capture.refusal="HUDless compositor writes or tests depth/stencil";return;}
    }
    D3D11_VIEWPORT viewport{};UINT viewports=1;context->RSGetViewports(&viewports,&viewport);
    if(viewports!=1||viewport.TopLeftX>float(frame.output.x)||viewport.TopLeftY>float(frame.output.y)||viewport.TopLeftX+viewport.Width<float(frame.output.x+frame.output.width)||viewport.TopLeftY+viewport.Height<float(frame.output.y+frame.output.height)){
        capture.refusal="Final compositor viewport does not cover its actual View output";return;
    }
    ComPtr<ID3D11RasterizerState>rasterizer;context->RSGetState(&rasterizer);
    if(rasterizer){
        D3D11_RASTERIZER_DESC desc{};rasterizer->GetDesc(&desc);
        if(desc.FillMode!=D3D11_FILL_SOLID){capture.refusal="HUDless compositor does not fill its output";return;}
        if(desc.ScissorEnable){D3D11_RECT rect{};UINT count=1;context->RSGetScissorRects(&count,&rect);
            if(count!=1||rect.left>LONG(frame.output.x)||rect.top>LONG(frame.output.y)||rect.right<LONG(frame.output.x+frame.output.width)||rect.bottom<LONG(frame.output.y+frame.output.height)){
                capture.refusal="Final compositor scissors do not cover its actual View output";return;
            }
        }
    }
    std::array<ComPtr<ID3D11ShaderResourceView>,count>neutralViews;
    for(unsigned i=0;i<count;i++)if(roles[i]>=0){
        neutralViews[i]=neutralView(context,original[i],unsigned(roles[i]));if(!neutralViews[i])return;replaced[i]=neutralViews[i].Get();
    }
    auto output=prepareHudlessTarget(context,targets[0].Get(),capture.inputs);
    if(!output){capture.refusal="Final compositor does not write the matching window backbuffer or output format";return;}
    ID3D11RenderTargetView*target=output.Get();context->OMSetRenderTargets(1,&target,nullptr);
    context->PSSetShaderResources(0,count,replaced.data());
    draw();
    context->OMSetRenderTargets(1,originalTargets.data(),depth.Get());
    context->PSSetShaderResources(0,count,original.data());
    capture.originalTarget=targets[0];capture.captured=true;
}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext*context,UINT vertices,UINT first)noexcept{
    const auto original=hooks.draw.load(std::memory_order_acquire);
    if(!original)fatal("HUDless Draw original was not published");
    try{replayHudless(context,[&]{original(context,vertices,first);});original(context,vertices,first);}
    catch(const std::exception&e){fatal(e.what());}catch(...){fatal("HUDless Draw failed");}
}
void STDMETHODCALLTYPE indexed(ID3D11DeviceContext*context,UINT indices,UINT first,INT base)noexcept{
    const auto original=hooks.indexed.load(std::memory_order_acquire);
    if(!original)fatal("HUDless DrawIndexed original was not published");
    try{replayHudless(context,[&]{original(context,indices,first,base);});original(context,indices,first,base);}
    catch(const std::exception&e){fatal(e.what());}catch(...){fatal("HUDless DrawIndexed failed");}
}
template<class T>void install(void*target,void*detour,void*&installed,std::atomic<T>&original){
    if(installed){if(installed!=target)throw std::runtime_error("Multiple native HUDless draw implementations are unsupported");return;}
    void*trampoline=nullptr;auto result=MH_CreateHook(target,detour,&trampoline);
    if(result!=MH_OK)throw std::runtime_error(std::string("Create HUDless draw hook: ")+MH_StatusToString(result));
    original.store(reinterpret_cast<T>(trampoline),std::memory_order_release);installed=target;
    result=MH_EnableHook(target);if(result!=MH_OK)throw std::runtime_error(std::string("Enable HUDless draw hook: ")+MH_StatusToString(result));
}
}
void installFinalCompositeDraws(ID3D11DeviceContext*context,void(*failure)(const char*)noexcept){
    std::lock_guard<std::mutex>guard(hooks.lock);hooks.failure=failure;
    auto**table=*reinterpret_cast<void***>(context);
    install(table[13],reinterpret_cast<void*>(&draw),hooks.drawTarget,hooks.draw);
    install(table[12],reinterpret_cast<void*>(&indexed),hooks.indexedTarget,hooks.indexed);
}
void beginFinalComposite(std::shared_ptr<const FinalCompositeInputs>inputs){
    if(capture.active)throw std::runtime_error("Nested final compositor on the native RHI thread");
    if(inputs&&(!inputs->guides||!inputs->guides->snapshot||!inputs->guides->sequence||inputs->guides->nativeCounter!=inputs->nativeCounter||!(inputs->guides->view==inputs->view)))
        throw std::runtime_error("Final compositor guide lease is not from its same Scene View");
    if(inputs&&!inputs->guides->camera){reportStreamlineInputRejection(inputs->guides->cameraRefusal);inputs.reset();}
    if(inputs&&!inputs->guides->presentation){reportStreamlineInputRejection("Scene View has no native main-frame association");inputs.reset();}
    if(inputs&&inputs->guides->presentation!=currentPresentationFrame())throw std::runtime_error("Final compositor crossed native main frames");
    capture.active=true;capture.inputs=std::move(inputs);capture.originalTarget.Reset();capture.captured=capture.ambiguous=false;capture.refusal="Final compositor did not draw matching scene and UI inputs";
}
void endFinalComposite(){
    if(!capture.active)throw std::runtime_error("Final compositor ended without its RHI boundary");
    if(capture.inputs&&!capture.captured)reportStreamlineInputRejection(capture.refusal);
    capture.inputs.reset();capture.originalTarget.Reset();capture.active=false;
}
}
