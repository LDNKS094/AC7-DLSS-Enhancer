#include "presentation_bridge.h"
#include "final_composite.h"
#include "frame_routing.h"
#include "../d3d12/model_context.h"
#include "../d3d12/streamline_session.h"
#include <d3d11_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ac7 {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char*stage){if(FAILED(hr))throw std::runtime_error(std::string(stage)+" HRESULT "+std::to_string(unsigned(hr)));}
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;};
DXGI_FORMAT presentFormat(DXGI_FORMAT format){
    if(format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return DXGI_FORMAT_B8G8R8A8_UNORM;
    return format;
}
class PresentationBridge;
struct Presenters {std::mutex lock;std::vector<PresentationBridge*>values;};
Presenters&presenters=*new Presenters;
class PresentationBridge final:public IDXGISwapChain4 {
    struct Slot {
        ComPtr<ID3D11Texture2D>shared11;
        ComPtr<ID3D12Resource>shared12;
        ComPtr<ID3D12CommandAllocator>allocator;
        ComPtr<ID3D12GraphicsCommandList>commands;
        ComPtr<ID3D11Texture2D>hudless11,sourceTarget;
        ComPtr<ID3D12Resource>hudless12;
        ComPtr<ID3D11RenderTargetView>hudlessView;
        std::shared_ptr<const FinalCompositeInputs>frame;
        StreamlineInputCompletion inputs;
        UINT64 retired=0;
    };
    std::atomic<ULONG>references_{1};
    std::mutex lock_;
    ModelContext&gpu_;
    HWND window_,hidden_;
    ComPtr<IDXGISwapChain>source_;
    ComPtr<IDXGISwapChain1>source1_;
    ComPtr<IDXGISwapChain2>source2_;
    ComPtr<IDXGISwapChain3>source3_;
    ComPtr<IDXGISwapChain4>source4_;
    ComPtr<IDXGISwapChain4>present_;
    ComPtr<ID3D11DeviceContext4>context_;
    ComPtr<ID3D12Fence>fence12_;
    ComPtr<ID3D12Fence>producer12_;
    ComPtr<ID3D11Fence>fence11_;
    Handle event_;
    UINT64 serial_=0;
    bool applicationWaitable_=false;
    std::vector<Slot>slots_;
    void(*failure_)(const char*)noexcept;
    [[noreturn]] void fatal(const char*message)noexcept{
        failure_(message);OutputDebugStringA(message);RaiseFailFastException(nullptr,nullptr,0);std::terminate();
    }
    template<class F>HRESULT invoke(F&&f)noexcept{
        try{std::lock_guard<std::mutex>guard(lock_);const auto hr=f();
            if(FAILED(hr)&&hr!=DXGI_ERROR_INVALID_CALL&&hr!=E_INVALIDARG&&hr!=DXGI_ERROR_WAS_STILL_DRAWING&&hr!=E_NOINTERFACE)check(hr,"D3D12 presentation");
            return hr;
        }catch(const std::exception&e){fatal(e.what());}catch(...){fatal("D3D12 presentation failed");}
    }
    UINT64 next(){if(serial_==std::numeric_limits<UINT64>::max())throw std::runtime_error("Presentation fence overflow");return ++serial_;}
    bool complete(ID3D12Fence*fence,UINT64 value){
        if(!fence){if(value)throw std::runtime_error("Presentation retirement has no fence");return true;}
        const auto n=fence->GetCompletedValue();if(n==UINT64_MAX)throw std::runtime_error("Presentation reader device removed");return n>=value;
    }
    void wait(ID3D12Fence*fence,UINT64 value){
        if(complete(fence,value))return;
        check(fence->SetEventOnCompletion(value,event_.value),"Set presentation retirement event");
        if(WaitForSingleObject(event_.value,INFINITE)!=WAIT_OBJECT_0)throw std::runtime_error("Wait for presentation retirement failed");
        if(!complete(fence,value))throw std::runtime_error("Presentation retired without completing its fence");
    }
    bool complete(const Slot&slot){return complete(fence12_.Get(),slot.retired)&&complete(slot.inputs.fence.Get(),slot.inputs.value);}
    void wait(Slot&slot){
        wait(fence12_.Get(),slot.retired);wait(slot.inputs.fence.Get(),slot.inputs.value);
        slot.inputs={};
    }
    void drain(){
        // Signal after all presenting work, not merely after our upload copy.
        const auto value=next();check(gpu_.presentationQueue()->Signal(fence12_.Get(),value),"Signal presentation drain");wait(fence12_.Get(),value);
        for(auto&slot:slots_)wait(slot);
    }
    void createSlots(){
        DXGI_SWAP_CHAIN_DESC1 desc{};check(present_->GetDesc1(&desc),"Get presenting layout");
        DXGI_SWAP_CHAIN_DESC source{};check(source_->GetDesc(&source),"Get source transfer layout");
        D3D11_TEXTURE2D_DESC texture{};texture.Width=desc.Width;texture.Height=desc.Height;texture.Format=source.BufferDesc.Format;
        texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_DEFAULT;
        texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        texture.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        slots_.resize(desc.BufferCount);
        for(auto&slot:slots_){
            check(gpu_.sourceDevice()->CreateTexture2D(&texture,nullptr,&slot.shared11),"Create presentation transfer texture");
            ComPtr<IDXGIResource1>resource;check(slot.shared11.As(&resource),"Get presentation shared resource");Handle shared;
            check(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared.value),"Export presentation texture");
            check(gpu_.device()->OpenSharedHandle(shared.value,IID_PPV_ARGS(&slot.shared12)),"Open presentation texture");
            check(gpu_.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)),"Create presentation allocator");
            check(gpu_.device()->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.commands)),"Create presentation command list");
            check(slot.commands->Close(),"Close initial presentation command list");
        }
    }
    HRESULT present(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS*parameters){
        if(flags&DXGI_PRESENT_TEST)return parameters?present_->Present1(interval,flags,parameters):present_->Present(interval,flags);
        if(streamlineGenerationNeedsRelease()){
            suspendStreamlineGeneration();drain();releaseStreamlineGenerationResources();
        }
        const auto index=present_->GetCurrentBackBufferIndex();
        if(index>=slots_.size())throw std::runtime_error("Presentation back-buffer index exceeds layout");
        auto&slot=slots_[index];
        if((flags&DXGI_PRESENT_DO_NOT_WAIT)&&!complete(slot))return DXGI_ERROR_WAS_STILL_DRAWING;
        wait(slot);
        ComPtr<ID3D11Texture2D>input;
        // Blt chains always render into buffer zero; flip chains expose their current index.
        DXGI_SWAP_CHAIN_DESC sourceDesc{};check(source_->GetDesc(&sourceDesc),"Get source presentation layout");
        const bool flip=sourceDesc.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL||sourceDesc.SwapEffect==DXGI_SWAP_EFFECT_FLIP_DISCARD;
        const auto sourceIndex=flip&&source3_?source3_->GetCurrentBackBufferIndex():0;
        check(source_->GetBuffer(sourceIndex,IID_PPV_ARGS(&input)),"Get D3D11 presentation input");
        D3D11_TEXTURE2D_DESC inputDesc{};input->GetDesc(&inputDesc);
        D3D11_TEXTURE2D_DESC sharedDesc{};slot.shared11->GetDesc(&sharedDesc);
        if(inputDesc.Width!=sharedDesc.Width||inputDesc.Height!=sharedDesc.Height||inputDesc.Format!=sharedDesc.Format)
            throw std::runtime_error("D3D11/D3D12 presentation layouts diverged");
        if(inputDesc.SampleDesc.Count>1)context_->ResolveSubresource(slot.shared11.Get(),0,input.Get(),0,sharedDesc.Format);
        else context_->CopyResource(slot.shared11.Get(),input.Get());
        const auto produced=next();check(context_->Signal(fence11_.Get(),produced),"Signal final D3D11 image");
        context_->Flush();check(gpu_.presentationQueue()->Wait(producer12_.Get(),produced),"Wait for final D3D11 image");
        ComPtr<ID3D12Resource>output;check(present_->GetBuffer(index,IID_PPV_ARGS(&output)),"Get D3D12 presentation output");
        check(slot.allocator->Reset(),"Reset presentation allocator");check(slot.commands->Reset(slot.allocator.Get(),nullptr),"Reset presentation command list");
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for(auto&b:barriers){b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        barriers[0].Transition={slot.shared12.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};
        barriers[1].Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST};
        slot.commands->ResourceBarrier(2,barriers);slot.commands->CopyResource(output.Get(),slot.shared12.Get());
        for(auto&b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
        slot.commands->ResourceBarrier(2,barriers);check(slot.commands->Close(),"Close presentation copy");
        ID3D12CommandList*lists[]={slot.commands.Get()};gpu_.presentationQueue()->ExecuteCommandLists(1,lists);
        std::array<unsigned,4>region{};
        const bool hudless=slot.frame&&slot.sourceTarget==input;
        auto guides=hudless?slot.frame->guides->snapshot:std::shared_ptr<GuideSnapshot>{};
        if(hudless){const auto&r=slot.frame->output;region={r.x,r.y,r.width,r.height};}
        std::optional<ScenePresentation>scene;
        if(hudless)scene=ScenePresentation{slot.hudless12.Get(),guides.get(),&slot.frame->guides->camera.value(),region,slot.frame->guides->sequence};
        const auto frame=takePresentationFrame();
        if(hudless&&slot.frame->guides->presentation!=frame)throw std::runtime_error("Scene presentation crossed native main frames");
        beginStreamlinePresent(output.Get(),unsigned(slots_.size()),frame.get(),scene?&*scene:nullptr);
        if(frame)endStreamlineRender(*frame);
        slot.frame.reset();slot.sourceTarget.Reset();
        if(frame)markStreamlinePresentStart(*frame);
        const auto result=parameters?present_->Present1(interval,flags,parameters):present_->Present(interval,flags);
        if(frame)markStreamlinePresentEnd(*frame);
        auto inputs=finishStreamlinePresent();
        if(guides&&inputs.fence){
            guides->consumedBy(inputs.fence.Get(),inputs.value);
            slot.inputs=std::move(inputs);
        }
        // Signal through the official queue proxy after Present so SDK queue blocking precedes retirement.
        slot.retired=next();check(gpu_.presentationQueue()->Signal(fence12_.Get(),slot.retired),"Signal real-frame presentation retirement");
        if(guides)guides->consumedBy(fence12_.Get(),slot.retired);
        if(FAILED(result)&&result!=DXGI_ERROR_WAS_STILL_DRAWING)check(result,"Present final D3D12 image");
        if(SUCCEEDED(result)){
            const auto hidden=source_->Present(0,flags&DXGI_PRESENT_ALLOW_TEARING);
            if(FAILED(hidden))check(hidden,"Advance hidden D3D11 back buffers");
        }
        return result;
    }
    HRESULT resize(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags){
        RECT client{};if((!width||!height)&&!GetClientRect(window_,&client))throw std::runtime_error("Get presentation client extent failed");
        if(!width)width=UINT(std::max<LONG>(1,client.right-client.left));if(!height)height=UINT(std::max<LONG>(1,client.bottom-client.top));
        suspendStreamlineGeneration();drain();releaseStreamlineGenerationResources();slots_.clear();
        const auto result=source_->ResizeBuffers(count,width,height,format,flags);
        if(FAILED(result)){createSlots();return result;}
        DXGI_SWAP_CHAIN_DESC desc{};check(source_->GetDesc(&desc),"Get resized source layout");
        check(present_->ResizeBuffers(std::max(2u,desc.BufferCount),desc.BufferDesc.Width,desc.BufferDesc.Height,presentFormat(desc.BufferDesc.Format),flags),"Resize D3D12 presentation buffers");
        createSlots();return result;
    }
public:
    PresentationBridge(IDXGIFactory*factory,ID3D11Device*device,HWND window,IDXGISwapChain*source,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC&fullscreen,IDXGIOutput*restrictOutput,void(*failure)(const char*)noexcept)
        :gpu_(ModelContext::acquire(device)),window_(window),hidden_(nullptr),source_(source),failure_(failure){
        gpu_.initializeDevice();
        source_.As(&source1_);source_.As(&source2_);source_.As(&source3_);source_.As(&source4_);
        DXGI_SWAP_CHAIN_DESC original{};check(source_->GetDesc(&original),"Get hidden source layout");hidden_=original.OutputWindow;
        applicationWaitable_=(original.Flags&DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)!=0;
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=original.BufferDesc.Width;desc.Height=original.BufferDesc.Height;
        desc.Format=presentFormat(original.BufferDesc.Format);desc.SampleDesc.Count=1;desc.BufferUsage=original.BufferUsage;
        desc.BufferCount=std::max(2u,original.BufferCount);desc.Scaling=DXGI_SCALING_STRETCH;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;desc.Flags=original.Flags;
        auto proxy=streamlineFactory(factory);ComPtr<IDXGISwapChain1>present;
        check(proxy->CreateSwapChainForHwnd(gpu_.presentationQueue(),window,&desc,&fullscreen,restrictOutput,&present),"Create Streamline real-frame swapchain");
        check(present.As(&present_),"Get presenting IDXGISwapChain4");
        ComPtr<ID3D11DeviceContext>context;device->GetImmediateContext(&context);check(context.As(&context_),"Get D3D11 presentation context");
        check(gpu_.device()->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence12_)),"Create presentation fence");
        // Separate timelines: a later D11 upload must not complete an earlier D12 retirement.
        check(gpu_.device()->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&producer12_)),"Create presentation producer fence");
        Handle shared;check(gpu_.device()->CreateSharedHandle(producer12_.Get(),nullptr,GENERIC_ALL,nullptr,&shared.value),"Export presentation producer fence");
        ComPtr<ID3D11Device5>device5;check(device->QueryInterface(IID_PPV_ARGS(&device5)),"Get D3D11 presentation fence device");
        check(device5->OpenSharedFence(shared.value,IID_PPV_ARGS(&fence11_)),"Open presentation fence");
        event_.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event_.value)throw std::runtime_error("Create presentation retirement event failed");
        createSlots();
        installFinalCompositeDraws(context_.Get(),failure_);
    }
    ~PresentationBridge(){
        {std::lock_guard<std::mutex>guard(presenters.lock);auto&all=presenters.values;all.erase(std::remove(all.begin(),all.end(),this),all.end());}
        try{suspendStreamlineGeneration();drain();releaseStreamlineGenerationResources();BOOL fullscreen=FALSE;check(present_->GetFullscreenState(&fullscreen,nullptr),"Get final fullscreen state");if(fullscreen)check(present_->SetFullscreenState(FALSE,nullptr),"Leave fullscreen presentation");}
        catch(const std::exception&e){fatal(e.what());}catch(...){fatal("Presentation retirement failed");}
        slots_.clear();present_.Reset();source4_.Reset();source3_.Reset();source2_.Reset();source1_.Reset();source_.Reset();closePresentationWindow(hidden_);
    }
    ComPtr<ID3D11RenderTargetView>hudlessTarget(ID3D11DeviceContext*context,ID3D11RenderTargetView*target,std::shared_ptr<const FinalCompositeInputs>frame){
        ComPtr<ID3D11Device>device;context->GetDevice(&device);if(device.Get()!=gpu_.sourceDevice())return {};
        std::lock_guard<std::mutex>guard(lock_);
        ComPtr<ID3D11Resource>resource;target->GetResource(&resource);ComPtr<ID3D11Texture2D>destination;
        check(resource.As(&destination),"Get final compositor destination");
        DXGI_SWAP_CHAIN_DESC sourceDesc{};check(source_->GetDesc(&sourceDesc),"Get HUDless source swapchain layout");
        const bool flip=sourceDesc.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL||sourceDesc.SwapEffect==DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<ID3D11Texture2D>buffer;check(source_->GetBuffer(flip&&source3_?source3_->GetCurrentBackBufferIndex():0,IID_PPV_ARGS(&buffer)),"Get HUDless window backbuffer");
        if(buffer!=destination)return {};
        D3D11_TEXTURE2D_DESC desc{};destination->GetDesc(&desc);
        D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);
        if(desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1||view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||view.Texture2D.MipSlice!=0||view.Format!=presentFormat(desc.Format))return {};
        const auto&r=frame->output;
        if(!r.width||!r.height||uint64_t(r.x)+r.width>desc.Width||uint64_t(r.y)+r.height>desc.Height)return {};
        const auto index=present_->GetCurrentBackBufferIndex();if(index>=slots_.size())throw std::runtime_error("HUDless slot exceeds presenting layout");
        auto&slot=slots_[index];wait(slot);
        if(!slot.hudlessView){
            auto layout=desc;layout.Format=presentFormat(desc.Format);layout.Usage=D3D11_USAGE_DEFAULT;
            layout.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;layout.CPUAccessFlags=0;
            layout.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            check(device->CreateTexture2D(&layout,nullptr,&slot.hudless11),"Create shared HUDless texture");
            check(device->CreateRenderTargetView(slot.hudless11.Get(),&view,&slot.hudlessView),"Create shared HUDless target");
            ComPtr<IDXGIResource1>sharedResource;check(slot.hudless11.As(&sharedResource),"Get HUDless shared resource");Handle shared;
            check(sharedResource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared.value),"Export HUDless texture");
            check(gpu_.device()->OpenSharedHandle(shared.value,IID_PPV_ARGS(&slot.hudless12)),"Open HUDless texture");
        }
        slot.frame=std::move(frame);slot.sourceTarget=std::move(buffer);return slot.hudlessView;
    }
    void discardHudless(ID3D11RenderTargetView*target){
        std::lock_guard<std::mutex>guard(lock_);
        if(std::none_of(slots_.begin(),slots_.end(),[](const Slot&s){return bool(s.frame);}))return;
        ComPtr<ID3D11Resource>resource;target->GetResource(&resource);ComPtr<ID3D11Texture2D>texture;
        if(FAILED(resource.As(&texture)))return;
        for(auto&slot:slots_)if(slot.sourceTarget==texture){slot.frame.reset();slot.sourceTarget.Reset();}
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIDeviceSubObject)||id==__uuidof(IDXGISwapChain)||
            (id==__uuidof(IDXGISwapChain1)&&source1_)||(id==__uuidof(IDXGISwapChain2)&&source2_)||
            (id==__uuidof(IDXGISwapChain3)&&source3_)||(id==__uuidof(IDXGISwapChain4)&&source4_)){
            *out=static_cast<IDXGISwapChain4*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--references_;if(!count)delete this;return count;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT size,const void*data)override{return source_->SetPrivateData(id,size,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown*object)override{return source_->SetPrivateDataInterface(id,object);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT*size,void*data)override{return source_->GetPrivateData(id,size,data);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void**out)override{return source_->GetParent(id,out);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void**out)override{return source_->GetDevice(id,out);}
    HRESULT STDMETHODCALLTYPE Present(UINT interval,UINT flags)override{return invoke([&]{return present(interval,flags,nullptr);});}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID id,void**out)override{return source_->GetBuffer(index,id,out);}
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL state,IDXGIOutput*output)override{return invoke([&]{suspendStreamlineGeneration();drain();releaseStreamlineGenerationResources();return present_->SetFullscreenState(state,output);});}
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL*state,IDXGIOutput**output)override{return present_->GetFullscreenState(state,output);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC*desc)override{
        const auto hr=source_->GetDesc(desc);if(SUCCEEDED(hr)){desc->OutputWindow=window_;BOOL fullscreen=FALSE;const auto r=present_->GetFullscreenState(&fullscreen,nullptr);if(FAILED(r))return r;desc->Windowed=!fullscreen;}return hr;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags)override{return invoke([&]{return resize(count,width,height,format,flags);});}
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC*mode)override{return invoke([&]{suspendStreamlineGeneration();drain();releaseStreamlineGenerationResources();return present_->ResizeTarget(mode);});}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput**output)override{return present_->GetContainingOutput(output);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*stats)override{return present_->GetFrameStatistics(stats);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT*count)override{return present_->GetLastPresentCount(count);}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1*desc)override{return source1_?source1_->GetDesc1(desc):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC*desc)override{return present_->GetFullscreenDesc(desc);}
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND*out)override{if(!out)return E_POINTER;*out=window_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID id,void**out)override{return present_->GetCoreWindow(id,out);}
    HRESULT STDMETHODCALLTYPE Present1(UINT interval,UINT flags,const DXGI_PRESENT_PARAMETERS*parameters)override{if(!parameters)return E_INVALIDARG;return invoke([&]{return present(interval,flags,parameters);});}
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported()override{return present_->IsTemporaryMonoSupported();}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput**output)override{return present_->GetRestrictToOutput(output);}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA*color)override{return invoke([&]{return present_->SetBackgroundColor(color);});}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA*color)override{return present_->GetBackgroundColor(color);}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION rotation)override{return invoke([&]{return present_->SetRotation(rotation);});}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION*rotation)override{return present_->GetRotation(rotation);}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT width,UINT height)override{return invoke([&]{if(!source2_)return E_NOINTERFACE;const auto hr=source2_->SetSourceSize(width,height);if(SUCCEEDED(hr))check(present_->SetSourceSize(width,height),"Set presentation source size");return hr;});}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT*width,UINT*height)override{return source2_?source2_->GetSourceSize(width,height):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency)override{return invoke([&]{return applicationWaitable_?present_->SetMaximumFrameLatency(latency):source2_?source2_->SetMaximumFrameLatency(latency):E_NOINTERFACE;});}
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT*latency)override{return applicationWaitable_?present_->GetMaximumFrameLatency(latency):source2_?source2_->GetMaximumFrameLatency(latency):E_NOINTERFACE;}
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject()override{return applicationWaitable_?present_->GetFrameLatencyWaitableObject():source2_?source2_->GetFrameLatencyWaitableObject():nullptr;}
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F*matrix)override{return invoke([&]{return present_->SetMatrixTransform(matrix);});}
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F*matrix)override{return present_->GetMatrixTransform(matrix);}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex()override{return source3_?source3_->GetCurrentBackBufferIndex():0;}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE space,UINT*support)override{return present_->CheckColorSpaceSupport(space,support);}
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE space)override{return invoke([&]{return present_->SetColorSpace1(space);});}
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags,const UINT*masks,IUnknown*const*queues)override{
        if(masks||queues)return DXGI_ERROR_INVALID_CALL;return ResizeBuffers(count,width,height,format,flags);
    }
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type,UINT size,void*data)override{return invoke([&]{return present_->SetHDRMetaData(type,size,data);});}
};
}
HWND createPresentationWindow(){
    static std::once_flag registered;static HINSTANCE module=nullptr;
    std::call_once(registered,[]{
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&createPresentationWindow),&module))throw std::runtime_error("Get presentation window module failed");
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=module;cls.lpszClassName=L"AC7NativePresentationSource";
        if(!RegisterClassW(&cls))throw std::runtime_error("Register presentation source window failed");
    });
    const auto window=CreateWindowExW(WS_EX_TOOLWINDOW,L"AC7NativePresentationSource",L"",WS_POPUP,0,0,1,1,nullptr,nullptr,module,nullptr);
    if(!window)throw std::runtime_error("Create hidden presentation source window failed");return window;
}
void closePresentationWindow(HWND window)noexcept{
    if(!window)return;
    if(GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId())DestroyWindow(window);
    else PostMessageW(window,WM_CLOSE,0,0);
}
IDXGISwapChain4*createPresentationBridge(IDXGIFactory*factory,ID3D11Device*device,HWND window,IDXGISwapChain*source,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC&fullscreen,IDXGIOutput*restrictOutput,void(*failure)(const char*)noexcept){
    auto bridge=std::make_unique<PresentationBridge>(factory,device,window,source,fullscreen,restrictOutput,failure);
    {std::lock_guard<std::mutex>guard(presenters.lock);presenters.values.push_back(bridge.get());}
    return bridge.release();
}
ComPtr<ID3D11RenderTargetView>prepareHudlessTarget(ID3D11DeviceContext*context,ID3D11RenderTargetView*target,std::shared_ptr<const FinalCompositeInputs>frame){
    std::lock_guard<std::mutex>guard(presenters.lock);
    for(auto*presenter:presenters.values)if(auto output=presenter->hudlessTarget(context,target,frame))return output;
    return {};
}
void discardHudlessTarget(ID3D11RenderTargetView*target){
    if(!target)return;std::lock_guard<std::mutex>guard(presenters.lock);
    for(auto*presenter:presenters.values)presenter->discardHudless(target);
}
}
