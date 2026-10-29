// Independent public-loader NR tests and SR capability/HDR-creation probes. No game injection.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int evaluateCapturedFrame(ID3D12Device*,ID3D12CommandQueue*,NVSDK_NGX_Handle*,NVSDK_NGX_Parameter*,const std::filesystem::path&,const std::filesystem::path&,bool,bool,int);

static bool validGuid(const std::wstring& value) {
    if(value.size()!=36) return false;
    for(size_t i=0;i<value.size();++i) {
        const wchar_t c=value[i];
        if(i==8||i==13||i==18||i==23) { if(c!=L'-') return false; }
        else if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F'))) return false;
    }
    return true;
}
static void result(const char* stage,NVSDK_NGX_Result r) {
    std::cout<<stage<<" result=0x"<<std::hex<<static_cast<unsigned>(r)<<std::dec<<std::endl;
}
static void NVSDK_CONV logMessage(const char* text,NVSDK_NGX_Logging_Level,NVSDK_NGX_Feature feature) {
    std::fprintf(stderr,"NGX[%u] %s\n",static_cast<unsigned>(feature),text);
}
struct Session {
    bool dx12=false,initialized=false;
    NVSDK_NGX_Parameter* parameters=nullptr;
    NVSDK_NGX_Handle* feature=nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    ~Session() {
        if(feature) result("ReleaseFeature",NVSDK_NGX_D3D12_ReleaseFeature(feature));
        if(parameters) result("DestroyParameters",dx12?NVSDK_NGX_D3D12_DestroyParameters(parameters):NVSDK_NGX_D3D11_DestroyParameters(parameters));
        if(initialized) result("Shutdown",dx12?NVSDK_NGX_D3D12_Shutdown1(device12.Get()):NVSDK_NGX_D3D11_Shutdown1(device11.Get()));
    }
};
int wmain(int argc,wchar_t** argv) try {
    if(argc==2 && std::wstring(argv[1])==L"--self-test") {
        if(!validGuid(L"05882ed0-d4cd-4063-a726-0b4862bad3dc") || validGuid(L"------------------------------------") || validGuid(L"05882ed0Xd4cd-4063-a726-0b4862bad3dc") || validGuid(L"")) throw std::runtime_error("GUID validation self-test failed");
        std::cout<<"PASS GUID validation; no NGX calls"<<std::endl;return 0;
    }
    const bool nrPair=argc==6 && (std::wstring(argv[1])==L"d3d12-nr-pair-history" || std::wstring(argv[1])==L"d3d12-nr-eight-history" || std::wstring(argv[1])==L"d3d12-nr-eight-shared-history");
    const int sequenceMode=argc==6 && (std::wstring(argv[1])==L"d3d12-nr-eight-shared-history" || std::wstring(argv[1])==L"d3d12-sr-eight-shared-history")?5:
        argc==6 && std::wstring(argv[1])==L"d3d12-sr-eight-shared-reset-control"?6:
        argc==6 && std::wstring(argv[1])==L"d3d12-nr-eight-history"?3:
        argc==6 && std::wstring(argv[1])==L"d3d12-sr-eight-history"?3:
        argc==6 && std::wstring(argv[1])==L"d3d12-sr-eight-reset-control"?4:
        nrPair?1:argc==6 && std::wstring(argv[1])==L"d3d12-sr-pair-history"?1:
        argc==6 && std::wstring(argv[1])==L"d3d12-sr-pair-reset-control"?2:0;
    const bool srDiagnostic=(!nrPair && sequenceMode) || (argc==6 && std::wstring(argv[1])==L"d3d12-sr-reset-diagnostic");
    const bool sr=srDiagnostic || (argc==5 && (std::wstring(argv[1])==L"d3d11-sr" || std::wstring(argv[1])==L"d3d12-sr" || std::wstring(argv[1])==L"d3d12-sr-create"));
    const bool evaluate=nrPair || srDiagnostic || (argc==6 && (std::wstring(argv[1])==L"d3d12-evaluate" || std::wstring(argv[1])==L"d3d12-hdr-neutral"));
    if((!evaluate && argc!=5) || (std::wstring(argv[1])!=L"d3d11" && std::wstring(argv[1])!=L"d3d12" && std::wstring(argv[1])!=L"d3d12-create" && !evaluate && !sr) || !validGuid(argv[4])) throw std::runtime_error("Usage: preflight-nr d3d11|d3d12|d3d12-create|d3d12-evaluate|d3d12-hdr-neutral|d3d11-sr|d3d12-sr|d3d12-sr-create|d3d12-sr-reset-diagnostic|d3d12-sr-pair-history|d3d12-sr-pair-reset-control|d3d12-nr-pair-history|d3d12-nr-eight-history|d3d12-sr-eight-history|d3d12-sr-eight-reset-control|d3d12-nr-eight-shared-history|d3d12-sr-eight-shared-history|d3d12-sr-eight-shared-reset-control RUNTIME_DIRECTORY LOG_DIRECTORY OWN_GUID [PREPARED_FRAME_DIRECTORY]");
    const auto directory=std::filesystem::absolute(argv[2]),logs=std::filesystem::absolute(argv[3]);
    const wchar_t* runtimeName=sr?L"nvngx_dlss.dll":L"nvngx_dlssnr.dll";
    if(!std::filesystem::is_regular_file(directory/runtimeName)) throw std::runtime_error("Requested feature runtime absent");
    if(sequenceMode) {
        const unsigned frameCount=sequenceMode>=3?8:2;
        for(unsigned f=0;f<frameCount;++f) {
            const auto frame=L"frame"+std::to_wstring(f);
            if(std::filesystem::exists(logs/frame)) throw std::runtime_error("Refusing prior sequence output before NGX initialization");
            for(const wchar_t* name:{L"color.raw",L"depth.raw",L"motion.raw"})
                if(!std::filesystem::is_regular_file(std::filesystem::path(argv[5])/frame/name)) throw std::runtime_error("Missing sequence input before NGX initialization");
            if(srDiagnostic && sequenceMode>=3 && !std::filesystem::is_regular_file(std::filesystem::path(argv[5])/frame/L"frame-time.bin")) throw std::runtime_error("Missing captured frame time before NGX initialization");
            if(srDiagnostic && !std::filesystem::is_regular_file(std::filesystem::path(argv[5])/frame/L"exposure.raw")) throw std::runtime_error("Missing SR sequence exposure before NGX initialization");
        }
    }
    std::filesystem::create_directories(logs);
    std::string guid;for(const wchar_t c:std::wstring(argv[4])) guid.push_back(static_cast<char>(c));
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) throw std::runtime_error("DXGI factory failed");
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;;++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;HRESULT hr=factory->EnumAdapters1(i,&candidate);
        if(hr==DXGI_ERROR_NOT_FOUND) break;
        if(FAILED(hr)) throw std::runtime_error("Adapter enumeration failed");
        DXGI_ADAPTER_DESC1 desc{};if(FAILED(candidate->GetDesc1(&desc))) throw std::runtime_error("Adapter description failed");
        if(desc.VendorId==0x10de && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) {adapter=candidate;break;}
    }
    if(!adapter) throw std::runtime_error("No NVIDIA hardware adapter");
    if(evaluate) {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {debug->EnableDebugLayer();std::cout<<"D3D12DebugLayer=enabled"<<std::endl;}
        else std::cout<<"D3D12DebugLayer=unavailable"<<std::endl;
    }
    Session session;session.dx12=std::wstring(argv[1])!=L"d3d11" && std::wstring(argv[1])!=L"d3d11-sr";
    HRESULT hr;
    if(session.dx12) hr=D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&session.device12));
    else hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&session.device11,nullptr,nullptr);
    std::cout<<"CreateDevice HRESULT=0x"<<std::hex<<static_cast<unsigned>(hr)<<std::dec<<std::endl;
    if(FAILED(hr)) return 2;
    const wchar_t* paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,1};
    common.LoggingInfo.LoggingCallback=logMessage;common.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
    auto r=session.dx12?NVSDK_NGX_D3D12_Init_with_ProjectID(guid.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,"AC7-Native-Adapter preflight",logs.c_str(),session.device12.Get(),&common):NVSDK_NGX_D3D11_Init_with_ProjectID(guid.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,"AC7-Native-Adapter preflight",logs.c_str(),session.device11.Get(),&common);
    result("Init",r);if(NVSDK_NGX_FAILED(r)) return 3;session.initialized=true;
    r=session.dx12?NVSDK_NGX_D3D12_GetCapabilityParameters(&session.parameters):NVSDK_NGX_D3D11_GetCapabilityParameters(&session.parameters);
    result("GetCapabilityParameters",r);if(NVSDK_NGX_FAILED(r)||!session.parameters) return 4;
    if(HMODULE module=GetModuleHandleW(runtimeName)) {
        wchar_t loaded[32768]{};if(!GetModuleFileNameW(module,loaded,32768)) throw std::runtime_error("Runtime path query failed");
        std::wcout<<(sr?L"LoadedSR=":L"LoadedNR=")<<loaded<<std::endl;
        if(!std::filesystem::equivalent(loaded,directory/runtimeName)) throw std::runtime_error("Unexpected feature runtime loaded");
    } else throw std::runtime_error("Requested feature module not loaded");
    const char* nrKeys[]={NVSDK_NGX_Parameter_DLSSNR_Available,NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver,NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor,NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor,NVSDK_NGX_Parameter_DLSSNR_FeatureInitResult};
    const char* srKeys[]={NVSDK_NGX_Parameter_SuperSampling_Available,NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult};
    const char** keys=sr?srKeys:nrKeys;
    int available=0;bool availableRead=false;
    for(unsigned i=0;i<5;++i) {
        const char* key=keys[i];int value=-1;r=session.parameters->Get(key,&value);result(key,r);
        if(NVSDK_NGX_SUCCEED(r)) {std::cout<<key<<" value=0x"<<std::hex<<static_cast<unsigned>(value)<<std::dec<<std::endl;if(key==keys[0]) {available=value;availableRead=true;}}
    }
    if(!availableRead || available!=1) {std::cout<<"STOP feature not available; no scratch/create/evaluate"<<std::endl;return 5;}
    if(sr && !srDiagnostic && std::wstring(argv[1])!=L"d3d12-sr-create") {std::cout<<"STOP SR capability passed; no scratch/create/evaluate"<<std::endl;return 0;}
    if(sr) {
        // Exact field mapping of the public NGX_D3D12_CREATE_DLSS_EXT helper; no image input/default exposure.
        session.parameters->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);
        session.parameters->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);
        session.parameters->Set(NVSDK_NGX_Parameter_Width,2560u);session.parameters->Set(NVSDK_NGX_Parameter_Height,1440u);
        session.parameters->Set(NVSDK_NGX_Parameter_OutWidth,2560u);session.parameters->Set(NVSDK_NGX_Parameter_OutHeight,1440u);
        session.parameters->Set(NVSDK_NGX_Parameter_PerfQualityValue,static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA));
        const int flags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        session.parameters->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,flags);
        session.parameters->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,0);
        std::cout<<"SR creation test: 2560x1440 -> 2560x1440 DLAA; HDR/MVLowRes/DepthInverted flags="<<flags<<(srDiagnostic?"; explicit standalone Reset diagnostic, not AC7 calibration":"; no evaluate")<<std::endl;
    } else {
        // Dimension keys match the official Vulkan NR helper; the D3D12 generic-create contract is tested separately.
        session.parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Width,2560u);
        session.parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Height,1440u);
    }
    if(std::wstring(argv[1])==L"d3d12-create" || std::wstring(argv[1])==L"d3d12-sr-create" || evaluate) {
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(session.device12->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue))) ||
           FAILED(session.device12->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&allocator))) ||
           FAILED(session.device12->CreateCommandList(0,desc.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list))) ||
           FAILED(session.device12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) throw std::runtime_error("D3D12 command setup failed");
        r=NVSDK_NGX_D3D12_CreateFeature(list.Get(),sr?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_DLSSNR,session.parameters,&session.feature);
        result("CreateFeature",r);
        std::cout<<"FeatureHandle="<<(session.feature?"non-null":"null")<<std::endl;
        if(NVSDK_NGX_FAILED(r)||!session.feature) {std::cout<<"STOP create failed; no submission/evaluate"<<std::endl;return 7;}
        if(FAILED(list->Close())) throw std::runtime_error("Command list close failed");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event) throw std::runtime_error("Fence event creation failed");
        ID3D12CommandList* submitted[]={list.Get()};queue->ExecuteCommandLists(1,submitted);
        const HRESULT signal=queue->Signal(fence.Get(),1);
        const HRESULT completion=fence->SetEventOnCompletion(1,event);
        const DWORD waited=SUCCEEDED(signal)&&SUCCEEDED(completion)?WaitForSingleObject(event,10000):WAIT_FAILED;
        CloseHandle(event);
        if(waited!=WAIT_OBJECT_0 || fence->GetCompletedValue()==UINT64_MAX) {
            std::cerr<<"STOP GPU completion failed; terminating probe without releasing pending resources"<<std::endl;
            ExitProcess(8);
        }
        std::cout<<"GPU fence completed=1"<<std::endl;
        if(evaluate) return evaluateCapturedFrame(session.device12.Get(),queue.Get(),session.feature,session.parameters,argv[5],logs,std::wstring(argv[1])==L"d3d12-hdr-neutral",srDiagnostic,sequenceMode);
        std::cout<<"STOP create/completion passed; no evaluate"<<std::endl;
        return 0;
    }
    size_t bytes=SIZE_MAX;
    r=session.dx12?NVSDK_NGX_D3D12_GetScratchBufferSize(NVSDK_NGX_Feature_DLSSNR,session.parameters,&bytes):NVSDK_NGX_D3D11_GetScratchBufferSize(NVSDK_NGX_Feature_DLSSNR,session.parameters,&bytes);
    result("GetScratchBufferSize",r);if(NVSDK_NGX_FAILED(r)) return 6;
    std::cout<<"ScratchBytes="<<bytes<<"\nSTOP preflight complete; no create/evaluate"<<std::endl;
    return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
