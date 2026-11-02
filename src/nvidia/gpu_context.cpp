#include "gpu_context.h"
#include "streamline_session.h"
#include <dxgi1_2.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <exception>
#include <memory>
#include <vector>

namespace ac7 {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const char*stage){if(FAILED(hr))throw std::runtime_error(std::string(stage)+" HRESULT "+std::to_string(unsigned(hr)));}
void ngx(NVSDK_NGX_Result result,const char*stage){if(result!=NVSDK_NGX_Result_Success)throw std::runtime_error(std::string(stage)+" NGX result "+std::to_string(unsigned(result)));}
constexpr char projectId[]="05882ed0-d4cd-4063-a726-0b4862bad3dc";
constexpr char engineVersion[]="AC7 DLSS Enhancer";
void unavailable(const char*feature,const char*stage,unsigned result){
    const auto message=std::string(feature)+" disabled: "+stage+", result "+std::to_string(result);
    reportCapabilityFallback(message.c_str());
}
}
std::filesystem::path modelRuntimeDirectory(){
    HMODULE module=nullptr;
    check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&modelRuntimeDirectory),&module)?S_OK:HRESULT_FROM_WIN32(GetLastError()),"Model module");
    wchar_t path[32768];const auto n=GetModuleFileNameW(module,path,32768);
    if(!n||n>=32768)throw std::runtime_error("Native model module path unavailable");
    return std::filesystem::path(path).parent_path()/L"AC7DLSS";
}
GpuContext::GpuContext(ID3D11Device*source):source_(source){
    if(!source)throw std::invalid_argument("Missing model context D3D11 device");
}
GpuContext& GpuContext::acquire(ID3D11Device*source){
    struct Owners {std::mutex lock;std::vector<std::unique_ptr<GpuContext>>contexts;};
    static auto&owners=*new Owners;
    std::lock_guard<std::mutex>guard(owners.lock);
    for(const auto&context:owners.contexts)if(context->sourceDevice()==source)return *context;
    auto context=std::unique_ptr<GpuContext>(new GpuContext(source));
    owners.contexts.push_back(std::move(context));return *owners.contexts.back();
}
void GpuContext::initializeDevice(){
    std::lock_guard<std::mutex>guard(initializationLock_);
    if(device_)return;
    ComPtr<IDXGIDevice>dxgi;check(source_->QueryInterface(IID_PPV_ARGS(&dxgi)),"Model context DXGI device");
    ComPtr<IDXGIAdapter>adapter;check(dxgi->GetAdapter(&adapter),"Model context adapter");
    ComPtr<ID3D12Device>device;ComPtr<ID3D12CommandQueue>queue;
    check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Model context D3D12 device");
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    const auto proxy=bindStreamlineDevice(device.Get());
    check(proxy->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)),"Model context DIRECT queue");
    auto native=nativeStreamlineQueue(queue.Get());
    adapter_=std::move(adapter);device_=std::move(device);queue_=std::move(native);presentationQueue_=std::move(queue);
}
void GpuContext::initializeNgx(){
    initializeDevice();
    std::lock_guard<std::mutex>guard(initializationLock_);
    if(!initializeNgxLocked()||capabilityApiUnavailable_)throw std::runtime_error("NGX model requested after capability rejection");
}
bool GpuContext::initializeNgxLocked(){
    if(ngxInitialized_)return true;
    if(ngxUnavailable_)return false;
    const auto directory=modelRuntimeDirectory();const auto cache=directory/L"cache";
    std::filesystem::create_directories(cache);
    const wchar_t*paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,1};
    const auto result=NVSDK_NGX_D3D12_Init_with_ProjectID(projectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,engineVersion,cache.c_str(),device_.Get(),&common);
    if(result==NVSDK_NGX_Result_FAIL_FeatureNotSupported){
        ngxUnavailable_=true;sr_=nr_={true,false};unavailable("SR/NR","NGX initialization unsupported",unsigned(result));return false;
    }
    ngx(result,"Initialize NGX");ngxInitialized_=true;return true;
}
ModelCapabilities GpuContext::modelCapabilities(bool requestSr,bool requestNr){
    initializeDevice();
    std::lock_guard<std::mutex>guard(initializationLock_);
    if((!requestSr||sr_.known)&&(!requestNr||nr_.known))return {requestSr&&sr_.available,requestNr&&nr_.available};
    NVSDK_NGX_Parameter*parameters=nullptr;
    const auto query=[&](Capability&capability,bool neural){
        if(capability.known)return;
        if(ngxUnavailable_||capabilityApiUnavailable_){capability={true,false};return;}
        const char*name=neural?"NR":"SR";
        const auto directory=modelRuntimeDirectory(),cache=directory/L"cache";
        const auto model=directory/(neural?L"nvngx_dlssnr.dll":L"nvngx_dlss.dll");
        if(!std::filesystem::is_regular_file(model))throw std::runtime_error("Missing requested model "+model.u8string());
        std::filesystem::create_directories(cache);
        const wchar_t*paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,1};
        NVSDK_NGX_FeatureDiscoveryInfo discovery{};discovery.SDKVersion=NVSDK_NGX_Version_API;
        discovery.FeatureID=neural?NVSDK_NGX_Feature_DLSSNR:NVSDK_NGX_Feature_SuperSampling;
        discovery.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
        discovery.Identifier.v.ProjectDesc={projectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,engineVersion};
        discovery.ApplicationDataPath=cache.c_str();discovery.FeatureInfo=&common;
        NVSDK_NGX_FeatureRequirement requirements{};
        const auto discovered=NVSDK_NGX_D3D12_GetFeatureRequirements(adapter_.Get(),&discovery,&requirements);
        if(discovered==NVSDK_NGX_Result_FAIL_FeatureNotSupported){
            capability={true,false};unavailable(name,"GetFeatureRequirements unsupported",unsigned(discovered));return;
        }
        if(discovered!=NVSDK_NGX_Result_FAIL_NotImplemented){
            ngx(discovered,"Get NGX feature requirements");
            constexpr unsigned unsupported=NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported|NVSDK_NGX_FeatureSupportResult_AdapterUnsupported|NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported;
            constexpr unsigned unknownCheck=NVSDK_NGX_FeatureSupportResult_CheckNotPresent|NVSDK_NGX_FeatureSupportResult_NotImplemented;
            const auto reasons=unsigned(requirements.FeatureSupported);
            if(reasons&~(unsupported|unknownCheck))throw std::runtime_error("Unknown NGX feature support bits "+std::to_string(reasons));
            if(reasons&unsupported){
                capability={true,false};std::string reason=std::string(name)+" disabled: GetFeatureRequirements";
                if(reasons&NVSDK_NGX_FeatureSupportResult_AdapterUnsupported)reason+="; adapter unsupported";
                if(reasons&NVSDK_NGX_FeatureSupportResult_DriverVersionUnsupported)reason+="; driver unsupported";
                if(reasons&NVSDK_NGX_FeatureSupportResult_OSVersionBelowMinimumSupported)reason+="; OS below minimum";
                reason+="; support bits "+std::to_string(reasons);reportCapabilityFallback(reason.c_str());return;
            }
        }
        // An absent discovery check still requires the feature's authoritative availability value.
        if(!initializeNgxLocked()){capability={true,false};return;}
        if(!parameters){
            NVSDK_NGX_Parameter*queried=nullptr;
            const auto result=NVSDK_NGX_D3D12_GetCapabilityParameters(&queried);
            if(result==NVSDK_NGX_Result_FAIL_OutOfDate){
                capabilityApiUnavailable_=true;sr_=nr_={true,false};
                unavailable("SR/NR","GetCapabilityParameters requires newer driver/runtime",unsigned(result));return;
            }
            ngx(result,"Get NGX capabilities");
            parameters=queried;
            if(!parameters)throw std::runtime_error("NGX returned no capability parameters");
        }
        int available=0;
        ngx(parameters->Get(neural?NVSDK_NGX_Parameter_DLSSNR_Available:NVSDK_NGX_Parameter_SuperSampling_Available,&available),neural?"Query NR availability":"Query SR availability");
        int driverUpdate=0,initResult=0;
        const bool needsDriver=NVSDK_NGX_SUCCEED(parameters->Get(neural?NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver:NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&driverUpdate))&&driverUpdate!=0;
        const bool hasInitResult=NVSDK_NGX_SUCCEED(parameters->Get(neural?NVSDK_NGX_Parameter_DLSSNR_FeatureInitResult:NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,&initResult));
        if(hasInitResult&&NVSDK_NGX_FAILED(NVSDK_NGX_Result(initResult))){
            if(available||(NVSDK_NGX_Result(initResult)!=NVSDK_NGX_Result_FAIL_FeatureNotSupported&&NVSDK_NGX_Result(initResult)!=NVSDK_NGX_Result_FAIL_OutOfDate))
                throw std::runtime_error(std::string(name)+" capability feature initialization failed, NGX result "+std::to_string(unsigned(initResult)));
        }
        if(available&&needsDriver)throw std::runtime_error(std::string(name)+" capability contradicts its driver requirement");
        capability={true,available!=0};
        if(!capability.available){
            std::string reason=std::string(name)+" disabled: "+(needsDriver?"driver update required":"SDK reports unavailable");
            if(hasInitResult)reason+=", feature init result "+std::to_string(unsigned(initResult));
            int major=0,minor=0;
            if(needsDriver&&NVSDK_NGX_SUCCEED(parameters->Get(neural?NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor:NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,&major))&&
                NVSDK_NGX_SUCCEED(parameters->Get(neural?NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor:NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,&minor)))
                reason+="; minimum driver "+std::to_string(major)+"."+std::to_string(minor);
            reportCapabilityFallback(reason.c_str());
        }
    };
    try{
        if(requestSr)query(sr_,false);
        if(requestNr)query(nr_,true);
    }catch(...){if(parameters)NVSDK_NGX_D3D12_DestroyParameters(parameters);throw;}
    if(parameters)ngx(NVSDK_NGX_D3D12_DestroyParameters(parameters),"Release NGX capabilities");
    return {requestSr&&sr_.available,requestNr&&nr_.available};
}
GpuContext::~GpuContext(){
    if(!ngxInitialized_)return;
    const auto result=NVSDK_NGX_D3D12_Shutdown1(device_.Get());
    if(NVSDK_NGX_FAILED(result)){
        OutputDebugStringA("AC7 model context NGX shutdown failed");
        EXCEPTION_RECORD record{};record.ExceptionCode=0xE0423701;record.ExceptionFlags=EXCEPTION_NONCONTINUABLE;
        record.NumberParameters=1;record.ExceptionInformation[0]=unsigned(result);
        RaiseFailFastException(&record,nullptr,FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);std::terminate();
    }
}
}
