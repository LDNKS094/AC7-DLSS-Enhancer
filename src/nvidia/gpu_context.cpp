#include "gpu_context.h"
#include "streamline_session.h"
#include <dxgi1_2.h>
#include <nvsdk_ngx.h>
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
    device_=std::move(device);queue_=std::move(native);presentationQueue_=std::move(queue);
}
void GpuContext::initializeNgx(){
    initializeDevice();
    std::lock_guard<std::mutex>guard(initializationLock_);
    if(ngxInitialized_)return;
    const auto directory=modelRuntimeDirectory();const auto cache=directory/L"cache";
    std::filesystem::create_directories(cache);
    const wchar_t*paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,1};
    const auto result=NVSDK_NGX_D3D12_Init_with_ProjectID("05882ed0-d4cd-4063-a726-0b4862bad3dc",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"AC7 DLSS Enhancer",cache.c_str(),device_.Get(),&common);
    if(NVSDK_NGX_FAILED(result))throw std::runtime_error("Model context NGX Init result "+std::to_string(unsigned(result)));
    ngxInitialized_=true;
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
