// Standalone loader conformance test; no AC7 executable, game launch or deployment.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#include "../../tools/neutral-asi/interface.h"
using Microsoft::WRL::ComPtr;namespace fs=std::filesystem;
void require(bool v,const char*why){if(!v)throw std::runtime_error(why);}
std::vector<unsigned char>bytes(const fs::path&p){std::ifstream f(p,std::ios::binary|std::ios::ate);require(bool(f),"Shader fixture missing");const auto n=f.tellg();require(n>0&&n<8192,"Shader fixture outside bound");std::vector<unsigned char>b(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),b.size());require(bool(f),"Shader read failed");return b;}
int wmain(int argc,wchar_t**argv)try{
    require(argc==5||(argc==6&&std::wstring(argv[5])==L"--enabled"),"Usage: loader-probe ISOLATED_DINPUT8_DLL PREWARP_DXBC GUIDE_DXBC NEW_REPORT [--enabled]");const bool enabled=argc==6;const fs::path loader=fs::absolute(argv[1]),report=argv[4];require(!fs::exists(report),"Refusing previous report");require(fs::exists(loader.parent_path()/L"AC7NativeNeutral.asi"),"Isolated ASI missing");require(fs::exists(loader.parent_path()/L"AC7NativeNeutral.enable")==enabled,"Loader marker does not match expected startup mode");
    auto dll=LoadLibraryExW(loader.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);require(dll!=nullptr,"Loader load failed");STARTUPINFOW startup{};GetStartupInfoW(&startup);
    auto create=reinterpret_cast<decltype(&DirectInput8Create)>(GetProcAddress(dll,"DirectInput8Create"));require(create!=nullptr,"Public DirectInput8Create missing");ComPtr<IDirectInput8W>input;require(SUCCEEDED(create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(input.GetAddressOf()),nullptr)),"Public DirectInput initialization failed");
    HMODULE asi=nullptr;AC7NeutralQuery query=nullptr;AC7NeutralStatus status;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    for(;;){asi=GetModuleHandleW(L"AC7NativeNeutral.asi");if(asi){query=reinterpret_cast<AC7NeutralQuery>(GetProcAddress(asi,"AC7NeutralGetStatus"));require(query&&query(&status),"ASI status unavailable");require(!status.error,"ASI bootstrap error");if(status.installed)break;}require(std::chrono::steady_clock::now()<deadline,"Loader did not initialize ASI before timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(status.bootstrap&&bool(status.enabled)==enabled&&!status.shaderMatches&&!status.guideShaderMatches,"Loader initial state not expected/early");
    ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&level,1,D3D11_SDK_VERSION,&d,nullptr,&c)),"Host device failed");
    for(unsigned i=0;i<2;i++){const auto code=bytes(argv[i?2:3]);ComPtr<ID3D11PixelShader>ps;require(SUCCEEDED(d->CreatePixelShader(code.data(),code.size(),nullptr,&ps)),"Target shader creation failed");}
    require(query(&status)&&status.shaderMatches==1&&status.guideShaderMatches==1&&bool(status.enabled)==enabled&&!status.error,"Loader hooks missed target shaders");auto stop=reinterpret_cast<AC7NeutralStopFn>(GetProcAddress(asi,"AC7NeutralStop"));require(stop&&stop()&&query(&status)&&status.drained,"Loader host stop failed");
    std::ofstream out(report);out<<"{\"kind\":\"ACTUAL_INSTALLED_UAL_ISOLATED_HOST\",\"manualInitializeAsiCalled\":false,\"actualLoaderLoadedAsi\":true,\"actualLoaderInitializedAsi\":true,\"defaultDisabled\":"<<(enabled?"false":"true")<<",\"startupMarkerEnabled\":"<<(enabled?"true":"false")<<",\"hookInstalledBeforeHostDeviceAndTargetShaders\":true,\"guideCreatedBeforePrewarp\":true,\"targetShaderMatches\":2,\"noModels\":true,\"actualGame\":false,\"actualGameStartupTimingQualified\":false}";out.close();require(bool(out),"Loader report write failed");std::cout<<"PASS installed UAL/actual ASI auto-load+InitializeASI/installed before host shaders/guide-first/startupEnabled="<<enabled<<"/model0/game0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
