// Native scene SR ASI. Process-pinned hooks; DllMain performs no D3D work.
#include <windows.h>
#include "d3d11/resolution_hook.h"
#include "d3d11/scene_sr.h"
#include "d3d11/presentation_entry.h"
#include "d3d11/frame_routing.h"
#include "d3d12/model_context.h"
#include "d3d12/streamline_session.h"
#include "d3d11/nr_hotkey.h"
#include "settings/sr_settings.h"
#include "settings/fg_settings.h"
#include "d3d12/nr_capture.h"
#include <MinHook.h>
#include <mutex>
#include <filesystem>
#include <cstdio>
#include <string>
#include <cstdlib>
namespace {
ac7::NrHotkey effectHotkey{true},fgHotkey,reloadHotkey,captureHotkey;
ac7::NrSettings nrSettings;
ac7::FgSettings fgSettings;
ac7::SrMode srMode=ac7::SrMode::quality;
bool srEnabled=true;
float srSharpness=0;
unsigned invalidNrSettings=0,invalidSrSettings=0;
unsigned invalidFgSettings=0;
std::filesystem::path nrIniPath;
ac7::NrCapture*capture=nullptr;
bool captureRequested=false;
std::mutex lock;
HANDLE traceFile=INVALID_HANDLE_VALUE;
struct RuntimeState {
    bool bootstrap=false,installed=false,resolutionReady=false;
    unsigned counter=0;
    uint64_t frames=0,srFrames=0,token=0,sequence=0;
} state;
bool writeTrace(const char*line,int size,size_t capacity){if(traceFile==INVALID_HANDLE_VALUE)return false;DWORD written=0;return size>0&&size_t(size)<capacity&&WriteFile(traceFile,line,DWORD(size),&written,nullptr)&&written==DWORD(size);}
bool trace(const char*event){char line[512];const auto n=std::snprintf(line,sizeof(line),"event=%s ms=%llu installed=%u effect=%u resolutionReady=%u nrConfigured=%u srConfigured=%u counter=%u identity=%llu viewSequence=%llu processedFrames=%llu srFrames=%llu\r\n",event,GetTickCount64(),unsigned(state.installed),unsigned(effectHotkey.enabled),unsigned(state.resolutionReady),unsigned(nrSettings.enabled),unsigned(srEnabled),state.counter,state.token,state.sequence,state.frames,state.srFrames);return writeTrace(line,n,sizeof(line));}
[[noreturn]] void fail(unsigned code,const char*reason=nullptr)noexcept{
    // No runtime lock here: callers may already hold it, or be failing while unwinding.
    if(!reason)switch(code){
        case 1:reason="Bootstrap thread creation failed";break;
        case 2:reason="MinHook initialization failed";break;
        case 4:reason="Module pin or hook activation failed";break;
        case 12:reason="Runtime log initialization or write failed";break;
        case 14:reason="Conflicting earlier AC7 DLSS ASI is loaded; remove the old ASI before starting";break;
        default:reason="Unexpected runtime failure";break;
    }
    char line[2048];const auto n=std::snprintf(line,sizeof(line),"event=fatal code=%u reason=%s\r\n",code,reason);
    OutputDebugStringA(line);writeTrace(line,n,sizeof(line));
    if(traceFile!=INVALID_HANDLE_VALUE)FlushFileBuffers(traceFile);
    EXCEPTION_RECORD record{};record.ExceptionCode=0xE0423701;record.ExceptionFlags=EXCEPTION_NONCONTINUABLE;
    record.NumberParameters=1;record.ExceptionInformation[0]=code;
    RaiseFailFastException(&record,nullptr,FAIL_FAST_GENERATE_EXCEPTION_ADDRESS);
    std::abort();
}
bool traceNrConfig(const char*reason,bool changed){char line[512];const auto n=std::snprintf(line,sizeof(line),"event=nr-config read=%s changed=%u enabled=%u effectStrength=%.9g toneStrength=%.9g detailStrength=%.9g preset=%u optimizeEdges=%u centerAspectRatio=%.9g edgeQuality=%.9g blendWidth=%.9g invalid=%u\r\n",reason,unsigned(changed),unsigned(nrSettings.enabled),nrSettings.intensity,nrSettings.tone,nrSettings.structure,nrSettings.style,unsigned(nrSettings.peripheralCompression),nrSettings.centerAspect,nrSettings.edgeScale,nrSettings.transitionWidth,invalidNrSettings);return writeTrace(line,n,sizeof(line));}
bool traceSrConfig(const char*reason,bool changed){char line[256];const auto n=std::snprintf(line,sizeof(line),"event=sr-config read=%s changed=%u enabled=%u mode=%s scale=%.9g sharpness=%.9g invalid=%u aa=game-settings\r\n",reason,unsigned(changed),unsigned(srEnabled),ac7::srModeName(srMode),srEnabled?ac7::srScale(srMode):1.f,srSharpness,invalidSrSettings);return writeTrace(line,n,sizeof(line));}
bool traceFgConfig(const char*reason,bool changed){char line[256];const auto n=std::snprintf(line,sizeof(line),"event=fg-config read=%s changed=%u enabled=%u multiplier=%u invalid=%u\r\n",reason,unsigned(changed),unsigned(fgSettings.enabled),fgSettings.multiplier,invalidFgSettings);return writeTrace(line,n,sizeof(line));}
void pollCapture(){if(!capture)return;const auto result=capture->poll();if(result.state==ac7::NrCapture::Result::none)return;char line[2048];const auto path=result.folder.u8string();const auto n=std::snprintf(line,sizeof(line),"event=%s frame=%llu folder=\"%s\" error=\"%s\"\r\n",result.state==ac7::NrCapture::Result::saved?"capture-saved":"capture-error",result.frame,path.c_str(),result.error.c_str());if(!writeTrace(line,n,sizeof(line)))fail(12);if(result.state!=ac7::NrCapture::Result::saved)fail(18,result.error.c_str());}
bool startTrace(){
    HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&startTrace),&module))return false;
    wchar_t path[32768];const DWORD n=GetModuleFileNameW(module,path,32768);if(!n||n==32768)return false;
    const auto folder=std::filesystem::path(path).parent_path();nrIniPath=folder/L"AC7DLSS.ini";nrSettings=ac7::loadNrSettings(nrIniPath,invalidNrSettings);srMode=ac7::loadSrMode(nrIniPath,invalidSrSettings);srEnabled=ac7::loadSrEnabled(nrIniPath,invalidSrSettings);srSharpness=ac7::loadSrSharpness(nrIniPath,invalidSrSettings);capture=new ac7::NrCapture(folder/L"AC7DLSSCaptures");
    const auto name=L"AC7DLSS-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log";
    traceFile=CreateFileW((folder/name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    fgSettings=ac7::loadFgSettings(nrIniPath,invalidFgSettings);
    if(!trace("bootstrap")||!traceNrConfig("startup-once",true)||!traceSrConfig("startup-once",true)||!traceFgConfig("startup-once",true)||!trace("temporal-hdr-nr-peripheral"))return false;
    if(GetModuleHandleW(L"AC7NativeModels.asi")||GetModuleHandleW(L"AC7NativeNeutral.asi")||GetModuleHandleW(L"AC7NativeTiming.asi")||GetModuleHandleW(L"AC7NativeInterop.asi")){fail(14);return false;}return true;
}
void pollControls(){
    DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);const bool foreground=foregroundPid==GetCurrentProcessId(),modifiers=(GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000);
    if(fgHotkey.pressed(modifiers&&(GetAsyncKeyState(VK_F9)&0x8000),foreground)){
        fgSettings.enabled=!fgSettings.enabled;fgSettings=ac7::requestStreamlineFg(fgSettings);
        if(!traceFgConfig("manual-toggle",true))fail(12);
    }
    if(effectHotkey.update(modifiers&&(GetAsyncKeyState(VK_F10)&0x8000),foreground)){if(captureRequested&&!trace("capture-cancelled-mod-switch"))fail(12);captureRequested=false;ac7::invalidateSceneSr();if(!trace(effectHotkey.enabled?"mod-on":"mod-off"))fail(12);}
    if(reloadHotkey.pressed(modifiers&&(GetAsyncKeyState(VK_F11)&0x8000),foreground)){
        const auto next=ac7::loadNrSettings(nrIniPath,invalidNrSettings,nrSettings);const bool changed=!(next==nrSettings);nrSettings=next;
        const auto nextMode=ac7::loadSrMode(nrIniPath,invalidSrSettings,srMode);const bool srChanged=nextMode!=srMode;srMode=nextMode;
        const bool nextEnabled=ac7::loadSrEnabled(nrIniPath,invalidSrSettings,srEnabled);const bool enabledChanged=nextEnabled!=srEnabled;srEnabled=nextEnabled;
        const auto nextSharpness=ac7::loadSrSharpness(nrIniPath,invalidSrSettings,srSharpness);const bool sharpnessChanged=nextSharpness!=srSharpness;srSharpness=nextSharpness;
        const auto nextFg=ac7::loadFgSettings(nrIniPath,invalidFgSettings,fgSettings);const bool fgChanged=!(nextFg==fgSettings);fgSettings=nextFg;
        fgSettings=ac7::requestStreamlineFg(fgSettings);
        if((srChanged&&srEnabled)||enabledChanged)ac7::invalidateSceneSr();
        if(enabledChanged||(!srEnabled&&!nrSettings.enabled&&srSharpness==0)){if(captureRequested&&!trace(enabledChanged?"capture-cancelled-sr-switch":"capture-cancelled-effects-off"))fail(12);captureRequested=false;}
        if(!traceNrConfig("manual-reload",changed)||!traceSrConfig("manual-reload",srChanged||enabledChanged||sharpnessChanged)||!traceFgConfig("manual-reload",fgChanged))fail(12);
    }
    if(captureHotkey.pressed(modifiers&&(GetAsyncKeyState(VK_F12)&0x8000),foreground)){
        const char*event=!state.resolutionReady?"capture-no-resolution-entry":!effectHotkey.enabled?"capture-mod-off":!srEnabled&&!nrSettings.enabled&&srSharpness==0?"capture-no-active-effect":captureRequested||capture->busy()?"capture-busy":"capture-request";
        if(std::strcmp(event,"capture-request")==0)captureRequested=true;if(!trace(event))fail(12);
    }
}
ac7::SceneSettings renderSettings(){std::lock_guard<std::mutex>guard(lock);const float scale=effectHotkey.enabled&&state.resolutionReady&&state.installed?(srEnabled?ac7::srScale(srMode):nrSettings.enabled||srSharpness>0?1.f:0.f):0.f;return {scale,nrSettings,srSharpness};}
[[noreturn]] void sceneFailure(const char*message)noexcept{fail(17,message);}
void runSceneFrame(ac7::SceneSrFrame*frame)noexcept{
    try{
        std::lock_guard<std::mutex>guard(lock);auto result=ac7::executeSceneSr(frame,captureRequested?capture:nullptr);if(!result.submitted)sceneFailure("Scene execution returned without submission");
        state.sequence=result.sequence;state.frames++;state.srFrames+=unsigned(result.srEnabled);state.counter=result.view.counter;state.token=result.token;if(result.modelProcessed)captureRequested=false;
        static bool previousNr=false,previousSr=false;static float previousSharpness=0;static unsigned previousNrWidth=0;const bool changed=previousNr!=result.nrEnabled||previousSr!=result.srEnabled||previousSharpness!=result.sharpness||previousNrWidth!=result.nrWidth;previousNr=result.nrEnabled;previousSr=result.srEnabled;previousSharpness=result.sharpness;previousNrWidth=result.nrWidth;
        if(state.frames<=8||state.frames%256==0||changed){const auto&r=result.view.rect;char line[768];const auto n=std::snprintf(line,sizeof(line),"event=%s identity=%llu counter=%u nativeFrame=%u sequence=%llu input=%u,%u output=%u,%u allocation=%u,%u jitter=%.9g,%.9g sr=%u nr=%u nrInput=%u,%u compression=%u rcas=%.9g stage=temporal-graph\r\n",!result.modelProcessed?"fg-scene-inputs":result.srEnabled?"sr-scene":"scene-frame",result.token,result.view.counter,result.nativeCounter,result.sequence,unsigned(r[2]),unsigned(r[3]),result.output.width,result.output.height,result.view.allocation[0],result.view.allocation[1],result.view.jitter[0],result.view.jitter[1],unsigned(result.srEnabled),unsigned(result.nrEnabled),result.nrWidth,result.nrEnabled?unsigned(r[3]):0u,unsigned(result.nrEnabled&&result.nrWidth<unsigned(r[2])),result.sharpness);if(!writeTrace(line,n,sizeof(line)))fail(12);}
    }catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Scene execution failed");}
}
void streamlineLog(unsigned level,const char*message)noexcept{
    char line[2048];const auto n=std::snprintf(line,sizeof(line),"event=streamline level=%u message=%.1800s\r\n",level,message);
    if(!writeTrace(line,n,sizeof(line)))fail(12);
}
void presentationCreated(ID3D11Device*device,HWND window,unsigned width,unsigned height)noexcept{
    try{
        ac7::ModelContext::acquire(device);
        std::lock_guard<std::mutex>guard(lock);
        char line[256];const auto n=std::snprintf(line,sizeof(line),"event=presentation-source api=d3d11 window=%p size=%u,%u backend=d3d12 generation=off context=shared\r\n",reinterpret_cast<void*>(window),width,height);
        if(!writeTrace(line,n,sizeof(line)))fail(12);
    }catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Presentation source adoption failed");}
}
DWORD WINAPI bootstrap(void*){
    try{
        std::lock_guard<std::mutex>guard(lock);
        if(!ac7::installSceneSr(&runSceneFrame,&sceneFailure))fail(15,"Scene hook installation failed: unsupported executable or unavailable RHI hooks");
        if(!ac7::installResolutionHook(&renderSettings))fail(15,"Resolution hook installation failed: unsupported executable or unavailable View hooks");
        ac7::installFrameRouting(&sceneFailure);
        state.resolutionReady=true;
        if(MH_ApplyQueued()!=MH_OK){fail(4);return 0;}state.installed=true;if(!trace("installed")){fail(12);return 0;}
    }catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Bootstrap failed");}
    // Controls and capture completion remain independent of View admission or SR submission.
    for(;;){try{std::lock_guard<std::mutex>guard(lock);pollControls();pollCapture();}catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("Controls failed");}Sleep(16);}
}
}
extern "C" __declspec(dllexport) void WINAPI InitializeASI(){
    try{
        std::lock_guard<std::mutex>guard(lock);if(state.bootstrap)return;state.bootstrap=true;
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&bootstrap),&pinned))fail(4,"ASI module pin failed");
        if(!startTrace())fail(12);
        ac7::initializeStreamline(&streamlineLog);
        fgSettings=ac7::requestStreamlineFg(fgSettings);
        const auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)fail(2);
        ac7::installPresentationEntry(&presentationCreated,&sceneFailure);
        if(!trace("presentation-entry-installed"))fail(12);
        HANDLE thread=CreateThread(nullptr,0,bootstrap,nullptr,0,nullptr);if(thread)CloseHandle(thread);else fail(1);
    }catch(const std::exception&e){sceneFailure(e.what());}catch(...){sceneFailure("ASI early initialization failed");}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
