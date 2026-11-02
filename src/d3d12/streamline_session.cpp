#include "streamline_session.h"
#include "model_context.h"
#include "../d3d11/guide_snapshot_pool.h"
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <cstring>
#include <sl_security.h>
#include <mutex>
#include <stdexcept>
#include <string>

namespace ac7 {
namespace {
using Microsoft::WRL::ComPtr;
struct Session {
    std::mutex lock;
    HMODULE module=nullptr;
    bool ready=false;
    ComPtr<ID3D12Device>device;
    ComPtr<ID3D12Device>proxy;
    PFun_slSetD3DDevice*setDevice=nullptr;
    PFun_slUpgradeInterface*upgrade=nullptr;
    PFun_slGetNativeInterface*native=nullptr;
    PFun_slGetNewFrameToken*frame=nullptr;
    PFun_slSetTagForFrame*tag=nullptr;
    PFun_slSetConstants*constants=nullptr;
    PFun_slIsFeatureLoaded*loaded=nullptr;
    PFun_slIsFeatureSupported*supported=nullptr;
    PFun_slGetFeatureFunction*featureFunction=nullptr;
    PFun_slReflexSleep*sleep=nullptr;
    PFun_slPCLSetMarker*marker=nullptr;
    bool phasesSubmitted=false;
    void(*log)(unsigned,const char*)noexcept=nullptr;
    std::string inputRefusal="No HUDless input received";
    bool previousScene=false;
    uint64_t previousSequence=0;
};
Session&session=*new Session;
void inputStatus(const char*reason){
    const std::string next=reason?reason:"";
    if(session.inputRefusal==next)return;
    session.inputRefusal=next;
    if(session.log)session.log(reason?1u:0u,reason?reason:"Scene inputs and native camera constants submitted for real presentation");
}
void check(sl::Result result,const char*stage){if(result!=sl::Result::eOk)throw std::runtime_error(std::string(stage)+" Streamline result "+std::to_string(unsigned(result)));}
template<class T>T*entry(const char*name){const auto p=GetProcAddress(session.module,name);if(!p)throw std::runtime_error(std::string("Missing Streamline export ")+name);return reinterpret_cast<T*>(p);}
template<class T>T*feature(sl::Feature id,const char*name){
    void*function=nullptr;check(session.featureFunction(id,name,function),name);
    if(!function)throw std::runtime_error(std::string("Streamline returned no function ")+name);
    return reinterpret_cast<T*>(function);
}
constexpr unsigned bit(sl::PCLMarker marker){return 1u<<unsigned(marker);}
void marker(const PresentationFrame&frame,sl::PCLMarker next,sl::PCLMarker required,bool repeat=false){
    if(!frame.pcl)return;
    std::lock_guard<std::mutex>guard(session.lock);
    if(repeat&&(frame.markers&bit(next)))return;
    if(!frame.sdkToken||!session.marker||(frame.markers&bit(next))||!(frame.markers&bit(required)))
        throw std::runtime_error("Invalid native Reflex phase order for marker "+std::to_string(unsigned(next)));
    check(session.marker(next,*frame.sdkToken),"Submit native Reflex phase");frame.markers|=bit(next);
    constexpr auto complete=bit(sl::PCLMarker::eSimulationStart)|bit(sl::PCLMarker::eSimulationEnd)|bit(sl::PCLMarker::eRenderSubmitStart)|
        bit(sl::PCLMarker::eRenderSubmitEnd)|bit(sl::PCLMarker::ePresentStart)|bit(sl::PCLMarker::ePresentEnd);
    if((frame.markers&complete)==complete&&!session.phasesSubmitted){
        session.phasesSubmitted=true;
        if(session.log)session.log(0,"Native simulation/render/present phase markers submitted; low-latency mode off");
    }
}
template<class T>ComPtr<T>upgraded(T*object,const char*stage){
    // The SDK retains the native interface; it does not consume the caller's reference.
    void*result=object;check(session.upgrade(&result),stage);
    ComPtr<T>proxy;if(result==object)proxy=object;else proxy.Attach(static_cast<T*>(result));return proxy;
}
void logMessage(sl::LogType type,const char*message){if(type!=sl::LogType::eInfo&&session.log)session.log(unsigned(type),message?message:"Streamline empty message");}
bool unavailable(sl::Result result){
    switch(result){
        case sl::Result::eErrorDriverOutOfDate:case sl::Result::eErrorOSOutOfDate:case sl::Result::eErrorOSDisabledHWS:
        case sl::Result::eErrorNoSupportedAdapterFound:case sl::Result::eErrorAdapterNotSupported:case sl::Result::eErrorFeatureNotSupported:return true;
        default:return false;
    }
}
}
void initializeStreamline(void(*log)(unsigned,const char*)noexcept){
    std::lock_guard<std::mutex>guard(session.lock);
    if(session.ready)return;
    session.log=log;
    const auto directory=modelRuntimeDirectory();const auto path=directory/L"sl.interposer.dll";
    for(const auto name:{L"sl.interposer.dll",L"sl.common.dll",L"sl.dlss_g.dll",L"sl.reflex.dll",L"sl.pcl.dll",L"nvngx_dlssg.dll"})
        if(!std::filesystem::is_regular_file(directory/name))throw std::runtime_error("Missing Streamline runtime "+(directory/name).u8string());
    if(!sl::security::verifyEmbeddedSignature(path.c_str()))throw std::runtime_error("Streamline interposer is missing or its embedded signature is invalid");
    session.module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!session.module)throw std::runtime_error("Load Streamline interposer Win32 "+std::to_string(GetLastError()));
    const auto init=entry<PFun_slInit>("slInit");
    session.setDevice=entry<PFun_slSetD3DDevice>("slSetD3DDevice");
    session.upgrade=entry<PFun_slUpgradeInterface>("slUpgradeInterface");
    session.native=entry<PFun_slGetNativeInterface>("slGetNativeInterface");
    session.frame=entry<PFun_slGetNewFrameToken>("slGetNewFrameToken");
    session.tag=entry<PFun_slSetTagForFrame>("slSetTagForFrame");
    session.constants=entry<PFun_slSetConstants>("slSetConstants");
    session.loaded=entry<PFun_slIsFeatureLoaded>("slIsFeatureLoaded");
    session.supported=entry<PFun_slIsFeatureSupported>("slIsFeatureSupported");
    session.featureFunction=entry<PFun_slGetFeatureFunction>("slGetFeatureFunction");
    const wchar_t*paths[]={directory.c_str()};
    const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};
    sl::Preferences pref{};pref.pathsToPlugins=paths;pref.numPathsToPlugins=1;
    pref.featuresToLoad=features;pref.numFeaturesToLoad=3;pref.logMessageCallback=&logMessage;
    pref.flags=sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eUseDXGIFactoryProxy|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    pref.engine=sl::EngineType::eCustom;pref.engineVersion="AC7 native NR HDR SR";
    pref.projectId="05882ed0-d4cd-4063-a726-0b4862bad3dc";pref.renderAPI=sl::RenderAPI::eD3D12;
    check(init(pref,sl::kSDKVersion),"Initialize Streamline");session.ready=true;
}
ComPtr<ID3D12Device>bindStreamlineDevice(ID3D12Device*device){
    std::lock_guard<std::mutex>guard(session.lock);
    if(!session.ready||!device)throw std::runtime_error("Streamline device binding before initialization");
    if(session.device){if(session.device.Get()!=device)throw std::runtime_error("Streamline cannot bind a second D3D12 device");return session.proxy;}
    auto luid=device->GetAdapterLuid();sl::AdapterInfo adapter{};adapter.deviceLUID=reinterpret_cast<uint8_t*>(&luid);adapter.deviceLUIDSizeInBytes=sizeof(luid);
    // Query before startup: a failed startup must not be mistaken for unsupported hardware.
    const auto status=session.supported(sl::kFeatureDLSS_G,adapter);
    if(!unavailable(status))check(status,"Query DLSSG support");
    const auto reflexStatus=session.supported(sl::kFeatureReflex,adapter),pclStatus=session.supported(sl::kFeaturePCL,adapter);
    if(!unavailable(reflexStatus))check(reflexStatus,"Query Reflex support");
    if(!unavailable(pclStatus))check(pclStatus,"Query PCL support");
    check(session.setDevice(device),"Bind Streamline D3D12 device");
    auto proxy=upgraded(device,"Upgrade Streamline D3D12 device");
    bool loaded=false;
    if(unavailable(status)){
        loaded=false;
        if(session.log){const auto message="DLSSG unsupported for current system, Streamline result "+std::to_string(unsigned(status))+"; real-frame presentation only";session.log(1,message.c_str());}
    }else {
        check(session.loaded(sl::kFeatureDLSS_G,loaded),"Query DLSSG plugin");
        if(!loaded)throw std::runtime_error("Supported DLSSG plugin failed to load");
    }
    if(loaded){
        void*function=nullptr;check(session.featureFunction(sl::kFeatureDLSS_G,"slDLSSGSetOptions",function),"Get DLSSG options function");
        if(!function)throw std::runtime_error("Streamline returned no DLSSG options function");
        sl::DLSSGOptions options{};options.mode=sl::DLSSGMode::eOff;
        check(reinterpret_cast<PFun_slDLSSGSetOptions*>(function)(sl::ViewportHandle(0),options),"Keep DLSSG disabled");
    }
    if(!unavailable(pclStatus)){
        bool present=false;check(session.loaded(sl::kFeaturePCL,present),"Query PCL plugin");
        if(!present)throw std::runtime_error("Supported PCL plugin failed to load");
        auto*setOptions=feature<PFun_slPCLSetOptions>(sl::kFeaturePCL,"slPCLSetOptions");
        sl::PCLOptions options{};check(setOptions(options),"Initialize PCL options");
        session.marker=feature<PFun_slPCLSetMarker>(sl::kFeaturePCL,"slPCLSetMarker");
    }else if(session.log)session.log(1,"PCL unsupported; native phase markers unavailable");
    if(!unavailable(reflexStatus)){
        if(!session.marker)throw std::runtime_error("Supported Reflex requires PCL phase markers");
        bool present=false;check(session.loaded(sl::kFeatureReflex,present),"Query Reflex plugin");
        if(!present)throw std::runtime_error("Supported Reflex plugin failed to load");
        auto*setOptions=feature<PFun_slReflexSetOptions>(sl::kFeatureReflex,"slReflexSetOptions");
        sl::ReflexOptions options{};options.mode=sl::ReflexMode::eOff;
        check(setOptions(options),"Initialize Reflex with low-latency mode off");
        session.sleep=feature<PFun_slReflexSleep>(sl::kFeatureReflex,"slReflexSleep");
    }else if(session.log)session.log(1,"Reflex unsupported; low-latency sleep unavailable");
    if(session.log&&session.marker)session.log(0,session.sleep?"Native Reflex phases ready; low-latency mode off":"Native PCL phases ready; Reflex sleep unsupported");
    if(session.log)session.log(0,loaded?"D3D12 device bound; DLSSG plugin loaded, generation disabled":"D3D12 device bound; DLSSG plugin unavailable, generation disabled");
    session.device=device;session.proxy=proxy;return proxy;
}
ComPtr<ID3D12CommandQueue>nativeStreamlineQueue(ID3D12CommandQueue*queue){
    std::lock_guard<std::mutex>guard(session.lock);
    ComPtr<ID3D12CommandQueue>native;check(session.native(queue,reinterpret_cast<void**>(native.GetAddressOf())),"Get native presenting queue");return native;
}
ComPtr<IDXGIFactory2>streamlineFactory(IDXGIFactory*factory){
    std::lock_guard<std::mutex>guard(session.lock);
    if(!session.ready)throw std::runtime_error("Streamline factory before initialization");
    ComPtr<IDXGIFactory2>native;const auto hr=factory->QueryInterface(IID_PPV_ARGS(&native));
    if(FAILED(hr))throw std::runtime_error("Presentation requires IDXGIFactory2 HRESULT "+std::to_string(unsigned(hr)));
    return upgraded(native.Get(),"Upgrade private presentation factory");
}
std::shared_ptr<const PresentationFrame>acquireStreamlineFrame(uint64_t engineFrame){
    std::lock_guard<std::mutex>guard(session.lock);
    if(!session.device)return {};
    sl::FrameToken*token=nullptr;check(session.frame(token,nullptr),"Acquire native main-frame token");
    if(!token)throw std::runtime_error("Streamline returned no native main-frame token");
    return std::make_shared<const PresentationFrame>(PresentationFrame{engineFrame,token,session.marker!=nullptr});
}
void sleepStreamlineFrame(const PresentationFrame&frame){
    if(!frame.pcl)return;
    PFun_slReflexSleep*sleep=nullptr;
    {std::lock_guard<std::mutex>guard(session.lock);
        if(!frame.sdkToken||frame.markers||!session.marker)throw std::runtime_error("Invalid native Reflex sleep phase");
        sleep=session.sleep;}
    // The preceding frame must be able to submit/Present while this driver call waits.
    if(sleep)check(sleep(*frame.sdkToken),"Sleep before native input sampling");
}
void startStreamlineSimulation(const PresentationFrame&frame){
    if(!frame.pcl)return;
    std::lock_guard<std::mutex>guard(session.lock);
    if(!frame.sdkToken||frame.markers||!session.marker)throw std::runtime_error("Invalid native Reflex simulation start");
    check(session.marker(sl::PCLMarker::eSimulationStart,*frame.sdkToken),"Start native simulation");frame.markers|=bit(sl::PCLMarker::eSimulationStart);
}
void endStreamlineSimulation(const PresentationFrame&frame){marker(frame,sl::PCLMarker::eSimulationEnd,sl::PCLMarker::eSimulationStart);}
void startStreamlineRender(const PresentationFrame&frame){marker(frame,sl::PCLMarker::eRenderSubmitStart,sl::PCLMarker::eSimulationStart,true);}
void endStreamlineRender(const PresentationFrame&frame){marker(frame,sl::PCLMarker::eRenderSubmitEnd,sl::PCLMarker::eRenderSubmitStart);}
void markStreamlinePresentStart(const PresentationFrame&frame){marker(frame,sl::PCLMarker::ePresentStart,sl::PCLMarker::eRenderSubmitEnd);}
void markStreamlinePresentEnd(const PresentationFrame&frame){marker(frame,sl::PCLMarker::ePresentEnd,sl::PCLMarker::ePresentStart);}
void beginStreamlinePresent(ID3D12Resource*backbuffer,const PresentationFrame*frame,const ScenePresentation*scene){
    std::lock_guard<std::mutex>guard(session.lock);
    auto*hudless=scene?scene->hudless:nullptr;
    const auto*region=scene?&scene->region:nullptr;
    const auto*guides=scene?scene->guides:nullptr;
    if(scene&&(!hudless||!guides||!scene->camera||!scene->sequence))throw std::runtime_error("Incomplete same-frame scene presentation");
    if(!backbuffer)throw std::invalid_argument("Missing real presentation backbuffer");
    const auto desc=backbuffer->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||!desc.Width||desc.Width>UINT_MAX||!desc.Height)
        throw std::runtime_error("Invalid real presentation backbuffer extent");
    if(scene&&!frame)throw std::runtime_error("Scene presentation has no native main-frame association");
    sl::FrameToken*token=frame?frame->sdkToken:nullptr;
    // Untracked loading/movie presents still clear tags, but never acquire a simulation identity.
    if(!frame)check(session.frame(token,nullptr),"Acquire untracked real presentation token");
    if(!token)throw std::runtime_error("Streamline returned no real frame token");
    sl::Extent extent{};extent.width=UINT(desc.Width);extent.height=desc.Height;
    sl::Extent guideExtent{};
    if(hudless){
        if(!guides||!guides->depth()||!guides->motion())throw std::runtime_error("HUDless input has no matching guide snapshot");
        if(!region||!(*region)[2]||!(*region)[3]||uint64_t((*region)[0])+(*region)[2]>desc.Width||uint64_t((*region)[1])+(*region)[3]>desc.Height)
            throw std::runtime_error("HUDless region does not fit the real presentation backbuffer");
        const auto layout=hudless->GetDesc();
        if(layout.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||layout.Width!=desc.Width||layout.Height!=desc.Height||layout.Format!=desc.Format)
            throw std::runtime_error("HUDless and real presentation layouts diverged");
        extent.left=(*region)[0];extent.top=(*region)[1];extent.width=(*region)[2];extent.height=(*region)[3];
        const auto depth=guides->depth()->GetDesc(),motion=guides->motion()->GetDesc();
        if(depth.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||depth.Format!=DXGI_FORMAT_R32_FLOAT||motion.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||motion.Format!=DXGI_FORMAT_R16G16_FLOAT||
            !depth.Width||depth.Width>UINT_MAX||!depth.Height||depth.Width!=motion.Width||depth.Height!=motion.Height)
            throw std::runtime_error("Invalid shared depth/motion snapshot layout");
        guideExtent.width=UINT(depth.Width);guideExtent.height=depth.Height;
    }
    if(guides&&!hudless)throw std::runtime_error("Guides submitted without a matching final Scene View");
    if(scene){
        const auto&camera=*scene->camera;
        sl::Constants values{};
        static_assert(sizeof(values.cameraViewToClip)==sizeof(camera.viewToClip));
        std::memcpy(&values.cameraViewToClip,camera.viewToClip.data(),sizeof(values.cameraViewToClip));
        std::memcpy(&values.clipToCameraView,camera.clipToView.data(),sizeof(values.clipToCameraView));
        std::memcpy(&values.clipToPrevClip,camera.clipToPrevious.data(),sizeof(values.clipToPrevClip));
        std::memcpy(&values.prevClipToClip,camera.previousToClip.data(),sizeof(values.prevClipToClip));
        // This mapper accepts only the actual centered perspective with Panini disabled.
        const float noLens[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        std::memcpy(&values.clipToLensClip,noLens,sizeof(values.clipToLensClip));
        values.cameraPinholeOffset={0,0};values.jitterOffset={camera.jitter[0],camera.jitter[1]};
        values.mvecScale={1.f/guideExtent.width,1.f/guideExtent.height};
        values.cameraPos={camera.position[0],camera.position[1],camera.position[2]};
        values.cameraUp={camera.up[0],camera.up[1],camera.up[2]};
        values.cameraRight={camera.right[0],camera.right[1],camera.right[2]};
        values.cameraFwd={camera.forward[0],camera.forward[1],camera.forward[2]};
        values.cameraNear=camera.nearPlane;values.cameraFar=camera.farPlane;
        values.cameraFOV=camera.verticalFov;values.cameraAspectRatio=camera.aspect;
        values.depthInverted=sl::Boolean::eTrue;values.cameraMotionIncluded=sl::Boolean::eTrue;
        values.motionVectors3D=sl::Boolean::eFalse;values.motionVectorsDilated=sl::Boolean::eFalse;
        values.motionVectorsJittered=sl::Boolean::eFalse;values.orthographicProjection=sl::Boolean::eFalse;
        values.reset=camera.cameraCut||!session.previousScene||scene->sequence!=session.previousSequence?sl::Boolean::eTrue:sl::Boolean::eFalse;
        check(session.constants(values,*token,sl::ViewportHandle(0)),"Submit actual native camera constants");
    }
    // The swapchain proxy supplies the actual resource; this tag specifies its real region.
    sl::Resource resource(sl::ResourceType::eTex2d,hudless,D3D12_RESOURCE_STATE_COMMON);
    sl::Resource depth(sl::ResourceType::eTex2d,guides?guides->depth():nullptr,D3D12_RESOURCE_STATE_COMMON);
    sl::Resource motion(sl::ResourceType::eTex2d,guides?guides->motion():nullptr,D3D12_RESOURCE_STATE_COMMON);
    const sl::ResourceTag tags[]={
        {nullptr,sl::kBufferTypeBackbuffer,sl::ResourceLifecycle::eValidUntilPresent,&extent},
        {hudless?&resource:nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,hudless?&extent:nullptr},
        {guides?&depth:nullptr,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,guides?&guideExtent:nullptr},
        {guides?&motion:nullptr,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,guides?&guideExtent:nullptr}
    };
    check(session.tag(*token,sl::ViewportHandle(0),tags,4,nullptr),"Tag same-frame scene, depth and motion inputs");
    session.previousScene=scene!=nullptr;if(scene)session.previousSequence=scene->sequence;
    if(hudless)inputStatus(nullptr);
}
void reportStreamlineInputRejection(const char*reason){
    std::lock_guard<std::mutex>guard(session.lock);
    inputStatus(reason);
}
}
