// Bounded pre-initialization requirements query for a user-specified NR runtime.
// This does not initialize NGX, create a feature, dispatch GPU work, or impersonate a game.
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <nvsdk_ngx_defs.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv) try {
    if(argc!=4)throw std::runtime_error("Usage: query-nr-api RUNTIME_DLL LOG_DIRECTORY OWN_PROJECT_GUID");
    const auto library=std::filesystem::absolute(argv[1]);
    if(!std::filesystem::is_regular_file(library))throw std::runtime_error("Runtime file absent");
    const auto logs=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(logs);
    std::wstring wideGuid=argv[3];std::string guid;
    for(wchar_t c:wideGuid){if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F')||c==L'-'))throw std::runtime_error("Invalid project GUID character");guid.push_back(static_cast<char>(c));}
    if(guid.size()!=36)throw std::runtime_error("Expected own project GUID");
    ComPtr<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))throw std::runtime_error("DXGI factory failed");
    ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;;i++) {ComPtr<IDXGIAdapter1> candidate;if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
        DXGI_ADAPTER_DESC1 desc{};if(FAILED(candidate->GetDesc1(&desc)))throw std::runtime_error("Adapter description failed");
        if(desc.VendorId==0x10de && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=candidate;break;}}
    if(!adapter)throw std::runtime_error("No NVIDIA adapter");
    HMODULE module=LoadLibraryExW(library.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module)throw std::runtime_error("NR DLL load failed: "+std::to_string(GetLastError()));
    wchar_t actual[32768]{};const DWORD length=GetModuleFileNameW(module,actual,32768);
    if(!length || length>=32768 || !std::filesystem::equivalent(library,actual))throw std::runtime_error("Loaded runtime path mismatch");
    using Query=NVSDK_NGX_Result (__cdecl*)(IDXGIAdapter*,const NVSDK_NGX_FeatureDiscoveryInfo*,NVSDK_NGX_FeatureRequirement*);
    NVSDK_NGX_FeatureDiscoveryInfo info{};info.SDKVersion=NVSDK_NGX_Version_API;info.FeatureID=NVSDK_NGX_Feature_Reserved18;
    info.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
    info.Identifier.v.ProjectDesc={guid.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,"AC7-Native-Adapter requirements probe"};info.ApplicationDataPath=logs.c_str();
    for(const char* api:{"D3D11","D3D12"}) {
        const std::string name="NVSDK_NGX_"+std::string(api)+"_GetFeatureRequirements";
        const auto query=reinterpret_cast<Query>(GetProcAddress(module,name.c_str()));if(!query)throw std::runtime_error("Required query export missing");
        NVSDK_NGX_FeatureRequirement requirements{};requirements.FeatureSupported=static_cast<NVSDK_NGX_Feature_Support_Result>(0xffffffff);requirements.MinHWArchitecture=0xffffffff;
        const auto result=query(adapter.Get(),&info,&requirements);requirements.MinOSVersion[254]=0;
        std::cout<<api<<" result=0x"<<std::hex<<static_cast<unsigned>(result)<<" supportedMask=0x"<<static_cast<unsigned>(requirements.FeatureSupported)
            <<" minArch=0x"<<requirements.MinHWArchitecture<<std::dec<<" minOS="<<requirements.MinOSVersion<<'\n';
    }
    FreeLibrary(module);return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
