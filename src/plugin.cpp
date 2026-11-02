#include <windows.h>
#include "game/view_resolution.h"
#include "game/render_graph.h"
#include "presentation/dxgi_hooks.h"
#include "game/frame_hooks.h"
#include "nvidia/streamline_session.h"
#include "input/hotkey.h"
#include "settings/config.h"
#include <MinHook.h>
#include <mutex>
#include <filesystem>
#include <cstdio>
#include <string>
#include <exception>
#include <cstdlib>
namespace {
ac7::Hotkey masterHotkey{true},reloadHotkey;
ac7::NrSettings nrSettings;
ac7::FgSettings fgSettings;
ac7::SrSettings srSettings;
bool initialized=false,installed=false;
std::filesystem::path iniPath;
std::mutex lock;
HANDLE logFile=INVALID_HANDLE_VALUE;
bool writeLog(const char*line,int size,size_t capacity){
    DWORD written=0;
    return logFile!=INVALID_HANDLE_VALUE&&size>0&&size_t(size)<capacity&&WriteFile(logFile,line,DWORD(size),&written,nullptr)&&written==DWORD(size);
}
[[noreturn]] void fail(unsigned code,const char*reason)noexcept{
    char line[2048];const auto n=std::snprintf(line,sizeof(line),"event=fatal code=%u reason=%.1900s\r\n",code,reason);
    OutputDebugStringA(line);writeLog(line,n,sizeof(line));
    if(logFile!=INVALID_HANDLE_VALUE)FlushFileBuffers(logFile);
    EXCEPTION_RECORD record{};record.ExceptionCode=0xE0423701;record.ExceptionFlags=EXCEPTION_NONCONTINUABLE;
    record.NumberParameters=1;record.ExceptionInformation[0]=code;
    RaiseFailFastException(&record,nullptr,FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);std::abort();
}
void log(const char*message)noexcept{
    char line[2048];const auto n=std::snprintf(line,sizeof(line),"%.2000s\r\n",message);
    if(!writeLog(line,n,sizeof(line)))fail(12,"Runtime log write failed");
}
void applyFgRequest(){
    auto request=fgSettings;request.enabled=request.enabled&&masterHotkey.enabled;
    const auto applied=ac7::requestStreamlineFg(request);
    if(masterHotkey.enabled)fgSettings=applied;
}
void loadSettings(){
    unsigned nrInvalid=0,srInvalid=0,fgInvalid=0;
    nrSettings=ac7::loadNrSettings(iniPath,nrInvalid,nrSettings);
    const auto next=ac7::loadSrSettings(iniPath,srInvalid,srSettings);
    if(next.enabled!=srSettings.enabled||(next.enabled&&next.qualityMode!=srSettings.qualityMode))ac7::invalidateSceneHistory();
    srSettings=next;
    fgSettings=ac7::loadFgSettings(iniPath,fgInvalid,fgSettings);applyFgRequest();
    if(nrInvalid||srInvalid||fgInvalid)log("Invalid configuration values ignored; previous values retained");
}
void pollControls(){
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    const bool foreground=foregroundPid==GetCurrentProcessId(),modifiers=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000);
    if(masterHotkey.update(modifiers&&(GetAsyncKeyState(VK_F10)&0x8000),foreground)){
        applyFgRequest();ac7::invalidateSceneHistory();
        log(masterHotkey.enabled?"All enhancements enabled":"All enhancements disabled");
    }
    if(reloadHotkey.pressed(modifiers&&(GetAsyncKeyState(VK_F11)&0x8000),foreground)){loadSettings();log("Settings reloaded");}
}
ac7::SceneSettings renderSettings(){
    std::lock_guard<std::mutex>guard(lock);
    const bool enabled=masterHotkey.enabled&&installed;
    const float scale=enabled?(srSettings.enabled?ac7::srScale(srSettings.qualityMode):nrSettings.enabled||srSettings.sharpness>0?1.f:0.f):0.f;
    return {scale,nrSettings,srSettings.sharpness,enabled};
}
[[noreturn]] void sceneFailure(const char*message)noexcept{fail(17,message);}
void runSceneFrame(ac7::SceneFrame*frame)noexcept{
    try{ac7::executeSceneFrame(frame);}
    catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Scene execution failed");}
}
void streamlineLog(unsigned,const char*message)noexcept{log(message);}
DWORD WINAPI bootstrap(void*){
    try{
        std::lock_guard<std::mutex>guard(lock);
        if(!ac7::installRenderGraph(&runSceneFrame,&sceneFailure))fail(15,"Scene hook installation failed: unsupported executable or unavailable RHI hooks");
        if(!ac7::installViewResolutionHook(&renderSettings))fail(15,"Resolution hook installation failed: unsupported executable or unavailable View hooks");
        ac7::installFrameHooks(&sceneFailure);
        if(MH_ApplyQueued()!=MH_OK)fail(4,"Hook activation failed");
        installed=true;
    }catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Bootstrap failed");}
    for(;;){
        try{std::lock_guard<std::mutex>guard(lock);pollControls();}
        catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Controls failed");}
        Sleep(16);
    }
}
}
extern "C" __declspec(dllexport) void WINAPI InitializeASI(){
    try{
        std::lock_guard<std::mutex>guard(lock);if(initialized)return;initialized=true;
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&bootstrap),&module))fail(4,"ASI module pin failed");
        wchar_t path[32768];const auto n=GetModuleFileNameW(module,path,32768);
        if(!n||n==32768)fail(12,"ASI path unavailable");
        const auto folder=std::filesystem::path(path).parent_path();iniPath=folder/L"AC7DLSS.ini";
        const auto name=L"AC7DLSS-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log";
        logFile=CreateFileW((folder/name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(logFile==INVALID_HANDLE_VALUE)fail(12,"Runtime log creation failed");
        if(GetModuleHandleW(L"AC7NativeModels.asi")||GetModuleHandleW(L"AC7NativeNeutral.asi")||GetModuleHandleW(L"AC7NativeTiming.asi")||GetModuleHandleW(L"AC7NativeInterop.asi"))fail(14,"Conflicting earlier AC7 DLSS ASI is loaded; remove the old ASI before starting");
        ac7::initializeStreamline(&streamlineLog);loadSettings();
        const auto result=MH_Initialize();if(result!=MH_OK&&result!=MH_ERROR_ALREADY_INITIALIZED)fail(2,"MinHook initialization failed");
        ac7::installPresentationEntry(&sceneFailure);
        HANDLE thread=CreateThread(nullptr,0,bootstrap,nullptr,0,nullptr);
        if(!thread)fail(1,"Bootstrap thread creation failed");
        CloseHandle(thread);
    }catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("ASI initialization failed");}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
