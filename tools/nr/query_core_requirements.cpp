// Public NGX loader requirements query. No Init/Create/Evaluate and no game injection.
#include <windows.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
int wmain(int argc,wchar_t** argv) try {
    if(argc!=4)throw std::runtime_error("Usage: query-nr-core RUNTIME_DIRECTORY LOG_DIRECTORY OWN_GUID");
    const auto directory=std::filesystem::absolute(argv[1]),logs=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(logs);
    if(!std::filesystem::is_regular_file(directory/L"nvngx_dlssnr.dll"))throw std::runtime_error("NR runtime absent");
    std::string guid;for(const wchar_t* c=argv[3];*c;c++){if(!((*c>=L'0'&&*c<=L'9')||(*c>=L'a'&&*c<=L'f')||*c==L'-'))throw std::runtime_error("Invalid GUID");guid.push_back(static_cast<char>(*c));}
    if(guid.size()!=36)throw std::runtime_error("Expected own GUID");
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));if(FAILED(hr))throw std::runtime_error("DXGI factory failed");
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;;i++){Microsoft::WRL::ComPtr<IDXGIAdapter1> a;hr=factory->EnumAdapters1(i,&a);if(hr==DXGI_ERROR_NOT_FOUND)break;if(FAILED(hr))throw std::runtime_error("Adapter enumeration failed");DXGI_ADAPTER_DESC1 desc{};if(FAILED(a->GetDesc1(&desc)))throw std::runtime_error("Adapter description failed");if(desc.VendorId==0x10de && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){adapter=a;break;}}
    if(!adapter)throw std::runtime_error("No NVIDIA adapter");
    const wchar_t* paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,1};
    NVSDK_NGX_FeatureDiscoveryInfo info{};info.SDKVersion=NVSDK_NGX_Version_API;info.FeatureID=NVSDK_NGX_Feature_Reserved18;
    info.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;info.Identifier.v.ProjectDesc={guid.c_str(),NVSDK_NGX_ENGINE_TYPE_CUSTOM,"AC7-Native-Adapter requirements probe"};info.ApplicationDataPath=logs.c_str();info.FeatureInfo=&common;
    using Query=decltype(&NVSDK_NGX_D3D11_GetFeatureRequirements);
    Query calls[]={&NVSDK_NGX_D3D11_GetFeatureRequirements,&NVSDK_NGX_D3D12_GetFeatureRequirements};const char* names[]={"D3D11","D3D12"};
    for(unsigned i=0;i<2;i++) {NVSDK_NGX_FeatureRequirement requirements{};requirements.FeatureSupported=static_cast<NVSDK_NGX_Feature_Support_Result>(0xffffffff);requirements.MinHWArchitecture=0xffffffff;
        const auto result=calls[i](adapter.Get(),&info,&requirements);requirements.MinOSVersion[254]=0;
        std::cout<<names[i]<<" result=0x"<<std::hex<<static_cast<unsigned>(result)<<" supportedMask=0x"<<static_cast<unsigned>(requirements.FeatureSupported)<<" minArch=0x"<<requirements.MinHWArchitecture<<std::dec<<" minOS="<<requirements.MinOSVersion<<'\n';}
    if(HMODULE module=GetModuleHandleW(L"nvngx_dlssnr.dll")){wchar_t p[32768]{};GetModuleFileNameW(module,p,32768);std::wcout<<L"LoadedNR="<<p<<L'\n';}
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
