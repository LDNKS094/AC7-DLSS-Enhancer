#include "frame_routing.h"
#include "scene_rhi_queue.h"
#include "../d3d12/streamline_session.h"
#include <MinHook.h>
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

extern "C" {
void*ac7_frame_original_simulation_end=nullptr;
void ac7_frame_simulation_end_thunk();
}
namespace ac7 {
namespace {
using Frame=std::shared_ptr<const PresentationFrame>;
using Tick=void(__fastcall*)(void*);
using Construct=void*(__fastcall*)(void*,void*,unsigned);
using Execute=void(__fastcall*)(void*,void*,unsigned);
using Begin=void(__fastcall*)(void*,unsigned,void*);
using Destroy=void*(__fastcall*)(void*,unsigned);
using BeginViewport=void(__fastcall*)(void*,void*,void*);
Tick originalTick=nullptr;
Construct originalConstruct=nullptr;
Execute originalExecute=nullptr;
Begin originalBegin=nullptr;
Destroy originalDestroy=nullptr;
Tick originalTickRate=nullptr;
BeginViewport originalBeginViewport=nullptr;
unsigned char*image=nullptr;
void(*reportFailure)(const char*)noexcept=nullptr;
struct Tasks {std::mutex lock;std::unordered_map<void*,Frame>frames;};
Tasks&tasks=*new Tasks;
thread_local const Frame*simulation=nullptr;
thread_local const Frame*render=nullptr;
thread_local Frame rhiFrame;
struct Binding {
    const Frame*&slot;const Frame*previous;
    Binding(const Frame*&target,const Frame&frame):slot(target),previous(target){slot=&frame;}
    ~Binding(){slot=previous;}
};
[[noreturn]]void fail(const char*message)noexcept{
    if(reportFailure)reportFailure(message);
    OutputDebugStringA(message);RaiseFailFastException(nullptr,nullptr,0);std::terminate();
}
void recordGameState(const PresentationFrame&frame){
    const float*limit=nullptr;std::memcpy(&limit,image+0x3cbc570,sizeof(limit));
    if(!limit)throw std::runtime_error("Native t.MaxFPS data unavailable");
    void*world=nullptr;std::memcpy(&world,image+0x3cbdd88,sizeof(world));
    using WorldPlayer=void*(__fastcall*)(void*);
    using WorldPaused=bool(__fastcall*)(void*);
    using InHangar=bool(__fastcall*)();
    const bool allowed=frame.fg.enabled&&frame.fg.multiplier!=1&&world&&
        !reinterpret_cast<InHangar>(image+0x3ce380)()&&
        !reinterpret_cast<WorldPaused>(image+0x1841300)(world)&&
        reinterpret_cast<WorldPlayer>(image+0x3e8e70)(world)!=nullptr;
    recordStreamlineGameState(frame,*limit,allowed);
}
void __fastcall tick(void*loop){
    try{
        if(simulation)throw std::runtime_error("Nested native main frame");
        uint64_t counter;std::memcpy(&counter,image+0x3ac5520,sizeof(counter));
        const auto frame=acquireStreamlineFrame(counter);
        Binding bound(simulation,frame);originalTick(loop);
    }catch(const std::exception&e){fail(e.what());}catch(...){fail("Native main-frame routing failed");}
}
void __fastcall tickRate(void*engine){
    const bool main=simulation&&_ReturnAddress()==image+0x3a410b;
    // Driver sleep precedes the engine's real delta-time calculation; neither wait is simulation.
    try{if(main&&*simulation){
        recordGameState(**simulation);
        sleepStreamlineFrame(**simulation);
    }}
    catch(const std::exception&e){fail(e.what());}catch(...){fail("Native Reflex sleep failed");}
    originalTickRate(engine);
    try{if(main&&*simulation)startStreamlineSimulation(**simulation);}
    catch(const std::exception&e){fail(e.what());}catch(...){fail("Native simulation start failed");}
}
void __fastcall beginViewport(void*context,void*viewport,void*target){
    try{if(rhiFrame)startStreamlineRender(*rhiFrame);}
    catch(const std::exception&e){fail(e.what());}catch(...){fail("Native render submission start failed");}
    originalBeginViewport(context,viewport,target);
}
void*__fastcall construct(void*task,void*completion,unsigned dependencies){
    auto*result=originalConstruct(task,completion,dependencies);
    try{
        std::lock_guard<std::mutex>guard(tasks.lock);
        // Recycled native task storage cannot inherit an earlier frame association.
        tasks.frames.erase(task);
        if(simulation&&*simulation)tasks.frames.emplace(task,*simulation);
    }catch(const std::exception&e){fail(e.what());}catch(...){fail("Native render-task association failed");}
    return result;
}
void __fastcall execute(void*task,void*completion,unsigned thread){
    try{
        Frame frame;
        {std::lock_guard<std::mutex>guard(tasks.lock);const auto found=tasks.frames.find(task);
            if(found!=tasks.frames.end()){frame=std::move(found->second);tasks.frames.erase(found);}}
        Binding bound(render,frame);originalExecute(task,completion,thread);
    }catch(const std::exception&e){fail(e.what());}catch(...){fail("Native render-frame routing failed");}
}
void*__fastcall destroy(void*task,unsigned flags){
    try{std::lock_guard<std::mutex>guard(tasks.lock);tasks.frames.erase(task);}
    catch(...){fail("Native render-task retirement failed");}
    return originalDestroy(task,flags);
}
void publish(void*payload)noexcept{
    std::unique_ptr<Frame>owned(static_cast<Frame*>(payload));rhiFrame=std::move(*owned);
}
void cancel(void*payload)noexcept{delete static_cast<Frame*>(payload);}
void __fastcall begin(void*task,unsigned thread,void*completion){
    try{
        auto*list=image+0x3c78c78;
        if(!canQueueSceneWork(list))throw std::runtime_error("Frame routing requires the native immediate RHI producer");
        auto frame=std::make_unique<Frame>(render?*render:simulation?*simulation:Frame{});
        auto command=prepareSceneWork(frame.get(),&publish,&cancel);
        // Publish on the same command list before the native BeginFrame is recorded.
        enqueueSceneWork(list,command.release());frame.release();
        originalBegin(task,thread,completion);
    }catch(const std::exception&e){fail(e.what());}catch(...){fail("Native RHI-frame routing failed");}
}
}
extern "C" void ac7_frame_simulation_end()noexcept{
    try{if(simulation&&*simulation){
        // Input handling may have paused or changed the world since the pre-simulation sample.
        recordGameState(**simulation);endStreamlineSimulation(**simulation);
    }}
    catch(const std::exception&e){fail(e.what());}catch(...){fail("Native simulation end failed");}
}
Frame currentPresentationFrame(){return rhiFrame;}
Frame takePresentationFrame(){return std::exchange(rhiFrame,{});}
void installFrameRouting(void(*failure)(const char*)noexcept){
    if(!failure)throw std::invalid_argument("Missing frame-routing error callback");
    image=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));reportFailure=failure;
    if(!image)throw std::runtime_error("Missing native frame-routing image");
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x100000)throw std::runtime_error("Unsupported frame-routing image");
    const auto*pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||pe->OptionalHeader.SizeOfImage<0x3cbdd90)
        throw std::runtime_error("Unsupported frame-routing PE");
    constexpr unsigned char tickSig[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18};
    constexpr unsigned char constructSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0xc7,0x41,0x08,0xff,0,0,0};
    constexpr unsigned char executeSig[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};
    constexpr unsigned char beginSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
    constexpr unsigned char rateSig[]={0x40,0x55,0x53,0x57,0x48,0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec,0xc0,0,0,0};
    constexpr unsigned char viewportSig[]={0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x50,0x48,0x8b,0x05,0x51,0x87,0xa8,0x02};
    constexpr unsigned char endSig[]={0x48,0x8b,0x5c,0x24,0x28,0x48,0x85,0xdb,0x74,0x36};
    constexpr unsigned char fpsSig[]={0x48,0x8b,0x05,0x67,0x3e,0x1f,0x02,0xf3,0x0f,0x10,0x00};
    if(std::memcmp(image+0x1ac8702,fpsSig,sizeof(fpsSig)))throw std::runtime_error("Unsupported native t.MaxFPS consumer ABI");
    constexpr unsigned char playerSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x92,0x46,0x1a,0x00,0x80,0xb8};
    constexpr unsigned char hangarSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x0d,0xfb,0xf9,0x8e,0x03,0xe8,0x0e,0x7e};
    constexpr unsigned char pauseSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x41,0xb0,0x01,0x33,0xd2,0x48,0x8b,0xd9,0xe8,0x6d};
    if(std::memcmp(image+0x3e8e70,playerSig,sizeof(playerSig))||std::memcmp(image+0x3ce380,hangarSig,sizeof(hangarSig))||
        std::memcmp(image+0x1841300,pauseSig,sizeof(pauseSig)))throw std::runtime_error("Unsupported native FG game-state ABI");
    void*destructor=nullptr;std::memcpy(&destructor,image+0x257bc78,sizeof(destructor));
    if(std::memcmp(image+0x3a3f70,tickSig,sizeof(tickSig))||std::memcmp(image+0x3a5a20,constructSig,sizeof(constructSig))||
        std::memcmp(image+0x3a8b60,executeSig,sizeof(executeSig))||std::memcmp(image+0x3a8610,beginSig,sizeof(beginSig))||destructor!=image+0x3a6c20||
        std::memcmp(image+0x1adbcb0,rateSig,sizeof(rateSig))||std::memcmp(image+0xe2f8a0,viewportSig,sizeof(viewportSig))||std::memcmp(image+0x3a4528,endSig,sizeof(endSig)))
        throw std::runtime_error("Unsupported native frame/task routing ABI");
    void*targets[]={image+0x3a3f70,image+0x3a5a20,image+0x3a8b60,image+0x3a8610,destructor,image+0x1adbcb0,image+0xe2f8a0,image+0x3a4528};
    void*detours[]={reinterpret_cast<void*>(&tick),reinterpret_cast<void*>(&construct),reinterpret_cast<void*>(&execute),reinterpret_cast<void*>(&begin),reinterpret_cast<void*>(&destroy),reinterpret_cast<void*>(&tickRate),reinterpret_cast<void*>(&beginViewport),reinterpret_cast<void*>(&ac7_frame_simulation_end_thunk)};
    void**originals[]={reinterpret_cast<void**>(&originalTick),reinterpret_cast<void**>(&originalConstruct),reinterpret_cast<void**>(&originalExecute),reinterpret_cast<void**>(&originalBegin),reinterpret_cast<void**>(&originalDestroy),reinterpret_cast<void**>(&originalTickRate),reinterpret_cast<void**>(&originalBeginViewport),&ac7_frame_original_simulation_end};
    size_t installed=0;
    for(;installed<std::size(targets);installed++)if(MH_CreateHook(targets[installed],detours[installed],originals[installed])!=MH_OK)break;
    if(installed!=std::size(targets)){while(installed)MH_RemoveHook(targets[--installed]);throw std::runtime_error("Create native frame-routing hooks failed");}
    for(auto*target:targets)if(MH_QueueEnableHook(target)!=MH_OK)throw std::runtime_error("Queue native frame-routing hooks failed");
}
}
