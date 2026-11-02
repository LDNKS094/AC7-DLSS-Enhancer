#include <string>
#include "render_graph.h"
#include "../rendering/scene_processor.h"
#include "rhi_commands.h"
#include "../rendering/motion_blur.h"
#include "../presentation/hudless_compositor.h"
#include "camera_reader.h"
#include "frame_hooks.h"
#include "../nvidia/streamline_session.h"
#include <MinHook.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <exception>
#include <vector>

extern "C" {
void*ac7_temporal_original_build=nullptr;
void*ac7_temporal_original_color=nullptr;
void ac7_temporal_build_thunk();
void ac7_temporal_color_thunk();
}
namespace ac7 {
using Microsoft::WRL::ComPtr;
namespace {
unsigned char*base=nullptr;
template<class T>T&field(void*p,size_t n){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+n);}
template<class T>T&field(const void*p,size_t n){return field<T>(const_cast<void*>(p),n);}
template<class F>F native(size_t n){return reinterpret_cast<F>(base+n);}
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Temporal graph HRESULT "+std::to_string(unsigned(hr)));}
void(*reportError)(const char*)noexcept=nullptr;
void(*runFrame)(SceneFrame*)noexcept=nullptr;
[[noreturn]] void error(const char*s)noexcept{
    if(reportError)reportError(s);
    OutputDebugStringA(s);RaiseFailFastException(nullptr,nullptr,0);std::terminate();
}
void*scene(){return base+0x3c3ac10;}
void retain(void*p){if(p)reinterpret_cast<void(__fastcall*)(void*)>(field<void**>(p,0)[5])(p);}
void release(void*p){if(p)reinterpret_cast<void(__fastcall*)(void*)>(field<void**>(p,0)[6])(p);}
ComPtr<ID3D11Texture2D>texture(void*rhi){
    if(!rhi)return {};
    auto*resource=reinterpret_cast<IUnknown*(__fastcall*)(void*)>(field<void**>(rhi,0)[6])(rhi);
    if(!resource)return {};
    ComPtr<ID3D11Texture2D>result;check(resource->QueryInterface(IID_PPV_ARGS(&result)));return result;
}
struct Images {ComPtr<ID3D11Texture2D>target,resolved;};
Images images(void*p){
    if(!p)throw std::runtime_error("Missing native pooled color resource");
    auto*t=field<void*>(p,8);auto*s=field<void*>(p,16);
    Images result{texture(t?t:s),texture(s?s:t)};
    if(!result.target||!result.resolved)throw std::runtime_error("Missing native texture");
    return result;
}
ViewRect rectangle(const std::array<int,4>&r){
    if(r[0]<0||r[1]<0||r[2]<=r[0]||r[3]<=r[1])throw std::runtime_error("Invalid View rect");
    return {unsigned(r[0]),unsigned(r[1]),unsigned(r[2]-r[0]),unsigned(r[3]-r[1])};
}
struct ViewConstants {
    ComPtr<ID3D11Buffer>buffer;
    D3D11_BUFFER_DESC desc{};
    std::array<unsigned char,4096>bytes{};
};
struct ViewState {
    std::array<int,4>rect{};
    std::array<std::array<float,4>,2>projection{};
    std::array<float,2>pixels{};
    std::shared_ptr<const ViewConstants>constants;
};
void recompute(void*m){
    alignas(16)std::array<float,16>inverse{};
    auto*r=native<void*(__fastcall*)(void*,void*)>(0xfbc150)(inverse.data(),m);
    std::memcpy(static_cast<unsigned char*>(m)+0x40,r,64);
    native<void(__fastcall*)(void*)>(0x100b960)(m);
}
ViewState saveView(unsigned char*v,unsigned bytes,ID3D11Buffer*buffer,const D3D11_BUFFER_DESC&desc){
    ViewState s;std::memcpy(s.rect.data(),v+0x70,16);std::memcpy(s.pixels.data(),v+0xab4,8);
    for(unsigned i=0;i<2;i++){auto*m=v+(i?0x21e0:0xc0);s.projection[i]={field<float>(m,32),field<float>(m,36),field<float>(m,0x360),field<float>(m,0x364)};}
    auto*p=field<void*>(v,0x1418);if(!p)throw std::runtime_error("Missing View parameters");
    auto constants=std::make_shared<ViewConstants>();constants->buffer=buffer;constants->desc=desc;
    std::memcpy(constants->bytes.data(),p,bytes);s.constants=std::move(constants);return s;
}
void restoreView(unsigned char*v,const ViewState&s,unsigned bytes){
    for(unsigned i=0;i<2;i++){
        auto*m=v+(i?0x21e0:0xc0);const auto&p=s.projection[i];
        field<float>(m,32)=p[0];field<float>(m,36)=p[1];field<float>(m,0x360)=p[2];field<float>(m,0x364)=p[3];recompute(m);
    }
    std::memcpy(v+0x70,s.rect.data(),16);std::memcpy(v+0xab4,s.pixels.data(),8);
    std::memcpy(field<void*>(v,0x1418),s.constants->bytes.data(),bytes);
}
struct Backend {
    ComPtr<ID3D11Device>device;
    ComPtr<ID3D11DeviceContext>context;
    std::unique_ptr<SceneProcessor>sr;
    std::unique_ptr<MotionBlur>color;
    std::unique_ptr<GuideSnapshotPool>guideSnapshots;
    std::unique_ptr<GuideNormalizer>nativeGuides;
    unsigned guideWidth=0,guideHeight=0;
    ViewIdentity lastView{};
    const void*stream=nullptr;
    uint64_t token=0,sequence=0;
    unsigned nativeCounter=0;
    bool pending=false,seen=false;
};
Backend&backend=*new Backend;
std::mutex backendLock;
std::atomic<bool>invalidate{false};
struct OutputRef {void*pass=nullptr;unsigned index=0,pad=0;};
static_assert(sizeof(OutputRef)==16);
void*input(void*pass,unsigned index){return reinterpret_cast<void*(__fastcall*)(void*,unsigned)>(field<void**>(pass,0)[1])(pass,index);}
void*output(const OutputRef&r){return r.pass?reinterpret_cast<void*(__fastcall*)(void*,unsigned)>(field<void**>(r.pass,0)[7])(r.pass,r.index):nullptr;}
Images inputImages(void*pass,unsigned index){
    auto*r=static_cast<OutputRef*>(input(pass,index));auto*o=r?output(*r):nullptr;
    if(!o)throw std::runtime_error("Missing temporal graph input");
    return images(field<void*>(o,0x50));
}
}
struct SceneFrame {
    NrSettings nrSettings;
    float sharpness=0;
    Images color,output;
    ComPtr<ID3D11Texture2D>depth,motion;
    ComPtr<ID3D11RenderTargetView>target;
    ViewState lowState,highState;
    alignas(16)std::array<unsigned char,0x50>descriptor{};
    ViewIdentity identity{};
    std::shared_ptr<SceneGuideFrame>presentationGuides=std::make_shared<SceneGuideFrame>();
    ViewRect low{},high{};
    std::array<unsigned,2>allocation{};
    std::array<float,2>jitter{};
    const void*stream=nullptr;
    unsigned nativeCounter=0,constantBytes=0;
    bool validIdentity=false,models=true,panini=false;
};
namespace {
struct alignas(16) ModelPass {
    std::array<unsigned char,0xe0>native{};
    std::shared_ptr<SceneFrame>frame;
    explicit ModelPass(std::shared_ptr<SceneFrame>f):frame(std::move(f)){ac7::native<void(__fastcall*)(void*)>(0xe9e070)(native.data());}
    ~ModelPass(){ac7::native<void*(__fastcall*)(void*,unsigned)>(0xff3590)(native.data(),0);}
};
struct Job {std::shared_ptr<SceneFrame>frame;};
struct Scope {
    void*renderer;void*rhi;unsigned char*view=nullptr;
    std::shared_ptr<SceneFrame>frame;
    std::unique_ptr<ModelPass>prepared;
    ModelPass*model=nullptr;
    OutputRef sceneSource{};
    std::unique_ptr<Job>job;
    std::unique_ptr<SceneWork>command;
    std::vector<void*>pools;
    std::array<void*,3>uiPools{};
    std::vector<std::pair<void*,bool>>motionModes;
    bool built=false,queued=false,statesReady=false,cpuHigh=false,gpuHigh=false,post=false,uiActive=false;
};
thread_local Scope*scope=nullptr;
std::array<void*,17>modelVtable{};
using Render=void(__fastcall*)(void*,void*);
using Post=void(__fastcall*)(void*,void*,unsigned char*,void**);
using Gather=void(__fastcall*)(void*);
using Process=void(__fastcall*)(void*,const OutputRef*,void*);
using MotionProcess=void(__fastcall*)(void*,void*);
using Descriptor=void*(__fastcall*)(void*,void*,unsigned);
using UiBegin=bool(__fastcall*)(void*,void*,unsigned char*,bool,bool,bool);
using UiEnd=void(__fastcall*)(void*,void*,unsigned char*,bool,bool);
Render originalRender=nullptr;
Post originalPost=nullptr;
Gather originalGather=nullptr;
Process originalProcess=nullptr;
MotionProcess originalMotion=nullptr;
MotionProcess originalComposite=nullptr;
Descriptor originalMotionDescriptor=nullptr;
UiBegin originalUiBegin=nullptr;
UiEnd originalUiEnd=nullptr;
void(__fastcall*originalReset)(void*)=nullptr;
bool high(const OutputRef&r){
    auto p=r;
    while(p.pass){
        if(scope&&p.pass==scope->model)return p.index==0;
        // Independent UI color enters the graph without passing through scene SR.
        if(field<void*>(p.pass,0)==base+0x2ac5cc0){
            auto*pool=field<void*>(p.pass,0xa8);
            if(scope&&pool&&p.index==0)for(auto*ui:scope->uiPools)
                if(pool==ui)return true;
            return false;
        }
        auto*i=static_cast<OutputRef*>(input(p.pass,0));if(!i||i->pass==p.pass)return false;p=*i;
    }
    return false;
}
bool sceneBranch(OutputRef ref){
    while(ref.pass){
        if(ref.pass==scope->model)return ref.index==0;
        if(ref.pass==scope->sceneSource.pass&&ref.index==scope->sceneSource.index)return true;
        if(field<void*>(ref.pass,0)==base+0x2ac5cc0)return false;
        auto*next=static_cast<OutputRef*>(input(ref.pass,0));if(!next||next->pass==ref.pass)return false;ref=*next;
    }
    return false;
}
bool uiBranch(OutputRef ref){
    while(ref.pass){
        if(field<void*>(ref.pass,0)==base+0x2ac5cc0){
            auto*pool=field<void*>(ref.pass,0xa8);if(!pool)return false;
            return std::find(scope->uiPools.begin(),scope->uiPools.end(),pool)!=scope->uiPools.end();
        }
        if(ref.pass==scope->model)return false;
        if(ref.pass==scope->sceneSource.pass&&ref.index==scope->sceneSource.index)return false;
        auto*next=static_cast<OutputRef*>(input(ref.pass,0));if(!next||next->pass==ref.pass)return false;ref=*next;
    }
    return false;
}
struct CompositeBoundary {std::shared_ptr<const FinalCompositeInputs>inputs;bool begin;};
void cancelComposite(void*payload)noexcept{delete static_cast<CompositeBoundary*>(payload);error("Final compositor RHI boundary cancelled");}
void runComposite(void*payload)noexcept{
    std::unique_ptr<CompositeBoundary>work(static_cast<CompositeBoundary*>(payload));
    try{if(work->begin)beginFinalComposite(std::move(work->inputs));else endFinalComposite();}
    catch(const std::exception&e){error(e.what());}catch(...){error("Final compositor RHI boundary failed");}
}
void __fastcall composite(void*pass,void*context){
    try{
        auto*rhi=field<void*>(context,0x28);
        if(!canQueueSceneWork(rhi)){originalComposite(pass,context);return;}
        std::shared_ptr<FinalCompositeInputs>inputs;
        if(scope&&scope->built&&scope->post&&scope->view==field<void*>(context,0)&&scope->rhi==rhi){
            auto*source=static_cast<OutputRef*>(input(pass,0));
            if(source&&sceneBranch(*source)){
                inputs=std::make_shared<FinalCompositeInputs>();inputs->view=scope->frame->identity;
                inputs->output=scope->frame->high;inputs->nativeCounter=scope->frame->nativeCounter;inputs->scene=inputImages(pass,0).resolved;
                inputs->guides=scope->frame->presentationGuides;
                for(unsigned i=1;i<=inputs->ui.size();i++){
                    auto*ref=static_cast<OutputRef*>(input(pass,i));
                    if(ref&&ref->pass){
                        if(!uiBranch(*ref)){reportStreamlineInputRejection("Final compositor auxiliary input is not associated with this View UI");inputs.reset();break;}
                        inputs->ui[i-1]=inputImages(pass,i).resolved;
                    }
                }
                if(inputs)for(const auto&ui:inputs->ui)if(ui&&ui==inputs->scene){
                    reportStreamlineInputRejection("Final compositor scene and UI inputs alias");inputs.reset();break;
                }
                if(inputs&&!scope->frame->models&&!scope->queued){
                    // Dependencies are ready here; freeze guides before the compositor records its draw.
                    enqueueSceneWork(rhi,scope->command.release());scope->job.release();scope->queued=true;
                }
            }
        }
        // Slot5 runs after dependencies are ready and before the dispatcher releases their pools.
        auto begin=std::make_unique<CompositeBoundary>(CompositeBoundary{inputs,true});
        auto end=std::make_unique<CompositeBoundary>(CompositeBoundary{inputs,false});
        auto first=prepareSceneWork(begin.get(),runComposite,cancelComposite);
        auto last=prepareSceneWork(end.get(),runComposite,cancelComposite);
        enqueueSceneWork(rhi,first.release());begin.release();
        originalComposite(pass,context);
        enqueueSceneWork(rhi,last.release());end.release();
    }catch(const std::exception&e){error(e.what());}catch(...){error("Final compositor input association failed");}
}
void upload(const ViewConstants&constants){
    auto*buffer=constants.buffer.Get();const auto&desc=constants.desc;const auto&bytes=constants.bytes;
    if(desc.Usage==D3D11_USAGE_DYNAMIC){
        D3D11_MAPPED_SUBRESOURCE mapped{};check(backend.context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped));
        std::memcpy(mapped.pData,bytes.data(),desc.ByteWidth);backend.context->Unmap(buffer,0);
    }else backend.context->UpdateSubresource(buffer,0,nullptr,bytes.data(),0,0);
}
struct Upload {std::shared_ptr<const ViewConstants>constants;};
void cancelUpload(void*p)noexcept{delete static_cast<Upload*>(p);error("Temporal View upload cancelled");}
void runUpload(void*p)noexcept{
    std::unique_ptr<Upload>u(static_cast<Upload*>(p));
    try{std::lock_guard<std::mutex>g(backendLock);upload(*u->constants);}catch(const std::exception&e){error(e.what());}
}
void cpuDomain(bool desired){
    if(!scope||!scope->statesReady||scope->cpuHigh==desired)return;
    waitForSceneTasks(scope->rhi);
    restoreView(scope->view,desired?scope->frame->highState:scope->frame->lowState,scope->frame->constantBytes);
    scope->cpuHigh=desired;
}
void gpuDomain(bool desired){
    if(!scope||scope->gpuHigh==desired)return;
    auto&f=*scope->frame;
    auto payload=std::make_unique<Upload>(Upload{desired?f.highState.constants:f.lowState.constants});
    auto command=prepareSceneWork(payload.get(),runUpload,cancelUpload);
    enqueueSceneWork(scope->rhi,command.release());payload.release();scope->gpuHigh=desired;
}
void cancelJob(void*p)noexcept{
    std::unique_ptr<Job>j(static_cast<Job*>(p));{std::lock_guard<std::mutex>g(backendLock);backend.pending=false;invalidate=true;}
    error("Temporal SR command cancelled");
}
void runJob(void*p)noexcept{std::unique_ptr<Job>j(static_cast<Job*>(p));runFrame(j->frame.get());}
void __fastcall modelProcess(void*pass,void*context){
    if(!scope||pass!=scope->model){error("Temporal node executed outside its admitted producer scope");return;}
    try{
        if(field<void*>(context,0)!=scope->view||field<void*>(context,0x28)!=scope->rhi)throw std::runtime_error("Temporal graph View association lost");
        auto&f=*scope->frame;f.color=inputImages(pass,0);
        D3D11_TEXTURE2D_DESC d{};f.color.resolved->GetDesc(&d);
        if(d.Width!=f.allocation[0]||d.Height!=f.allocation[1]){
            throw std::runtime_error("Temporal color input grid differs from reserved model input");
        }
        // The graph releases its input lease on return; payload already owns independent COM references.
        enqueueSceneWork(scope->rhi,scope->command.release());scope->job.release();scope->queued=true;scope->gpuHigh=true;
    }catch(const std::exception&e){
        error(e.what());
    }
}
void*__fastcall modelDescriptor(void*pass,void*result,unsigned index){
    auto*p=reinterpret_cast<ModelPass*>(pass);
    if(index!=0)return nullptr;
    std::memcpy(result,p->frame->descriptor.data(),p->frame->descriptor.size());return result;
}
void __fastcall modelRelease(void*pass){
    if(scope&&scope->model==pass)scope->model=nullptr;
    delete reinterpret_cast<ModelPass*>(pass);
}
void prepareStates(Scope&s){
    auto&f=*s.frame;waitForSceneTasks(s.rhi);
    auto*u=field<void*>(s.view,0x10);
    if(!u||field<void*>(u,0x18)!=base+0x3caa6c8)throw std::runtime_error("Unsupported temporal View uniform layout");
    auto*b=field<IUnknown*>(u,0x20);if(!b)throw std::runtime_error("Missing temporal View buffer");ComPtr<ID3D11Buffer>buffer;check(b->QueryInterface(IID_PPV_ARGS(&buffer)));
    D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
    if(desc.ByteWidth>4096||desc.ByteWidth<f.constantBytes||(desc.Usage!=D3D11_USAGE_DYNAMIC&&desc.Usage!=D3D11_USAGE_DEFAULT)||(desc.Usage==D3D11_USAGE_DYNAMIC&&!(desc.CPUAccessFlags&D3D11_CPU_ACCESS_WRITE)))throw std::runtime_error("Unsupported temporal View buffer");
    // UI and SR preparation publish separate immutable generations for queued uploads.
    f.lowState=saveView(s.view,f.constantBytes,buffer.Get(),desc);
    for(unsigned i=0;i<2;i++){
        auto*m=s.view+(i?0x21e0:0xc0);field<float>(m,32)-=field<float>(m,0x360);field<float>(m,36)-=field<float>(m,0x364);
        field<float>(m,0x360)=0;field<float>(m,0x364)=0;recompute(m);
    }
    std::array<int,4>rect{int(f.high.x),int(f.high.y),int(f.high.x+f.high.width),int(f.high.y+f.high.height)};
    std::memcpy(s.view+0x70,rect.data(),16);const float zero[2]{};std::memcpy(s.view+0xab4,zero,8);
    alignas(16)unsigned char bounds[64]{};
    native<void(__fastcall*)(void*,void*,void*,void*,void*,unsigned,void*)>(0x11346d0)(s.view,scene(),s.view+0xc0,s.view+0x21e0,bounds,2,field<void*>(s.view,0x1418));
    f.highState=saveView(s.view,f.constantBytes,buffer.Get(),desc);
    restoreView(s.view,f.lowState,f.constantBytes);s.statesReady=true;
}
void leaveUi(){
    cpuDomain(false);gpuDomain(false);
    // This native recorder uses only the RHI list at +0x28 and cached rect at +0x30.
    std::array<unsigned char,0x40>context{};field<void*>(context.data(),0x28)=scope->rhi;
    native<void(__fastcall*)(void*,const int*,float,float)>(0xec8dd0)(context.data(),scope->frame->lowState.rect.data(),0.f,1.f);
    scope->uiActive=false;
}
bool __fastcall uiBegin(void*targets,void*rhi,unsigned char*view,bool clear,bool layer,bool extra){
    if(!scope||scope->view!=view||scope->rhi!=rhi||!scope->frame->models)return originalUiBegin(targets,rhi,view,clear,layer,extra);
    try{
        if(scope->uiActive)throw std::runtime_error("Nested native UI target");
        if(!scope->statesReady)prepareStates(*scope);
        cpuDomain(true);gpuDomain(true);scope->uiActive=true;
    }catch(const std::exception&e){error(e.what());}
    const bool ready=originalUiBegin(targets,rhi,view,clear,layer,extra);
    if(!ready){try{leaveUi();}catch(const std::exception&e){error(e.what());}}
    return ready;
}
void __fastcall uiEnd(void*targets,void*rhi,unsigned char*view,bool layer,bool extra){
    originalUiEnd(targets,rhi,view,layer,extra);
    if(scope&&scope->view==view&&scope->rhi==rhi&&scope->uiActive){
        try{leaveUi();}catch(const std::exception&e){error(e.what());}
    }
}
void build(void*context,unsigned char*view,bool*screenPercentage,const float*panini)noexcept{
    if(!scope||scope->view!=view||scope->built||!scope->post)return;
    try{
        auto source=field<OutputRef>(context,0x28);if(!source.pass)throw std::runtime_error("Missing temporal color graph reference");
        auto&f=*scope->frame;
        if(f.models)prepareStates(*scope);
        f.jitter={field<float>(view,0x420)*f.low.width*.5f,-field<float>(view,0x424)*f.low.height*.5f};
        f.panini=*screenPercentage&&panini&&*panini>0.f;
        // UI preparation is earlier; identity comes from this actual scene boundary.
        if(f.models)f.validIdentity=decodeViewIdentity(f.lowState.constants->bytes.data(),f.lowState.constants->bytes.size(),f.identity);
        else{
            const auto*parameters=field<void*>(view,0x1418);
            if(!parameters)throw std::runtime_error("Missing native presentation View parameters");
            std::memcpy(f.identity.bytes.data(),parameters,f.constantBytes);
            f.validIdentity=decodeViewIdentity(f.identity.bytes.data(),f.identity.bytes.size(),f.identity);
        }
        f.validIdentity=f.validIdentity&&f.identity.rect==std::array<float,4>{float(f.low.x),float(f.low.y),float(f.low.width),float(f.low.height)}&&f.identity.allocation==f.allocation;
        if(!f.validIdentity)throw std::runtime_error("Temporal input has no matching same-frame View identity");
        auto*depthPool=field<void*>(scene(),0x60);f.depth=images(depthPool).resolved;
        if(!f.models&&std::find(scope->pools.begin(),scope->pools.end(),depthPool)==scope->pools.end()){
            retain(depthPool);scope->pools.push_back(depthPool);
        }
        if(!f.models){scope->sceneSource=source;scope->built=true;return;}
        auto*node=scope->prepared.get();field<void**>(node,0)=modelVtable.data();
        auto*out=output({node,0});if(!out)throw std::runtime_error("Missing temporal output slot");
        native<void*(__fastcall*)(void*,void*)>(0xe93670)(field<void*>(context,8),node);
        reinterpret_cast<void(__fastcall*)(void*,unsigned,const OutputRef&)>(modelVtable[3])(node,0,source);
        field<OutputRef>(context,0x28)={node,0};scope->model=node;scope->prepared.release();scope->built=true;
        // Preserve Panini's projection work; ordinary screen-percentage scaling is now owned by this node.
        *screenPercentage=*screenPercentage&&panini&&*panini>0.f;
    }catch(const std::exception&e){error(e.what());}
}
void colorBuild(void*context)noexcept{
    if(!scope||!scope->built||!scope->frame->models||field<void*>(context,0x10)!=scope->view)return;
    try{cpuDomain(true);}catch(const std::exception&e){error(e.what());}
}
void __fastcall gather(void*pass){
    if(!scope||!scope->built||!scope->post||!scope->frame->models){originalGather(pass);return;}
    const bool previous=scope->cpuHigh;
    cpuDomain(high({pass,0}));originalGather(pass);cpuDomain(previous);
}
void __fastcall process(void*graph,const OutputRef*ref,void*context){
    if(!scope||!scope->built||!scope->post||!scope->frame->models||field<void*>(context,0)!=scope->view||field<void*>(context,0x28)!=scope->rhi){originalProcess(graph,ref,context);return;}
    const bool previous=scope->cpuHigh,desired=high(*ref);
    cpuDomain(desired);gpuDomain(desired);originalProcess(graph,ref,context);cpuDomain(previous);gpuDomain(previous);
}
struct ColorWork {ComPtr<ID3D11CommandList>commands;};
void cancelColor(void*p)noexcept{delete static_cast<ColorWork*>(p);error("Temporal color command cancelled");}
void runColor(void*p)noexcept{
    std::unique_ptr<ColorWork>w(static_cast<ColorWork*>(p));
    try{std::lock_guard<std::mutex>g(backendLock);backend.context->ExecuteCommandList(w->commands.Get(),TRUE);}catch(const std::exception&e){error(e.what());}
}
void*__fastcall motionDescriptor(void*pass,void*result,unsigned index){
    if(scope&&scope->built&&scope->frame->models&&high({pass,index})){
        auto&entries=scope->motionModes;
        if(std::none_of(entries.begin(),entries.end(),[&](const auto&x){return x.first==pass;}))entries.emplace_back(pass,field<unsigned char>(pass,0xa)!=0);
        field<unsigned char>(pass,0xa)=0;
    }
    return originalMotionDescriptor(pass,result,index);
}
void __fastcall motion(void*pass,void*context){
    if(!scope||!scope->built||!scope->post||!scope->frame->models||!high({pass,0})){originalMotion(pass,context);return;}
    std::unique_ptr<ColorWork>work;
    try{
        auto&f=*scope->frame;
        auto*o=output({pass,0});
        native<void*(__fastcall*)(void*,void*)>(0x10b4870)(o,context);
        auto target=images(field<void*>(o,0x50));
        auto color=inputImages(pass,0);work=std::make_unique<ColorWork>();
        auto flat=inputImages(pass,2),tiles=inputImages(pass,3);
        const unsigned quality=field<unsigned>(pass,0xd8);const int phase=field<int>(pass,0xdc);
        if(quality<1||quality>4||phase< -1||phase>1)throw std::runtime_error("Unsupported native MotionBlur quality or phase");
        const float samples[]={4,6,8,16};float scale=1.f;
        if(phase==0)scale=1.f-.5f/samples[quality-1];
        if(phase==1)scale=field<float>(field<void*>(base,0x3c34e70),4)/samples[quality-1];
        const auto*state=field<void*>(scope->view,8);
        const float time=state?field<float>(state,0x9a0):1.f;
        const float amount=field<float>(scope->view,0x116c),maximum=field<float>(scope->view,0x1170);
        const float pixels=scale*f.low.width*.5f,blur=.5f*time*amount;
        std::array<float,4>parameters{float(f.low.height)/f.low.width,pixels*blur,pixels*blur/16.f,std::abs(pixels)*maximum/100.f};
        const auto mode=std::find_if(scope->motionModes.begin(),scope->motionModes.end(),[&](const auto&x){return x.first==pass;});
        if(mode==scope->motionModes.end())throw std::runtime_error("Missing native MotionBlur output mode");
        const unsigned effectiveQuality=phase>0?(mode->second?3:std::min(quality,3u)):quality;
        {std::lock_guard<std::mutex>g(backendLock);if(!backend.color)backend.color=std::make_unique<MotionBlur>(backend.device.Get());work->commands=backend.color->motionBlur(color.resolved.Get(),flat.resolved.Get(),tiles.resolved.Get(),target.target.Get(),target.resolved.Get(),f.low,f.high,f.jitter,parameters,effectiveQuality);}
        auto command=prepareSceneWork(work.get(),runColor,cancelColor);enqueueSceneWork(scope->rhi,command.release());work.release();
    }catch(const std::exception&e){error(e.what());}
}
void __fastcall post(void*self,void*rhi,unsigned char*view,void**velocity){
    if(!scope||scope->view!=view||scope->rhi!=rhi){originalPost(self,rhi,view,velocity);return;}
    if(scope->uiActive)error("Native UI target was not closed");
    scope->post=true;
    // Native graph construction clears the scene's UI slots before Gather/Process.
    constexpr size_t uiOffsets[]={0x1c0,0x1c8,0x1d0};
    for(size_t i=0;i<scope->uiPools.size();i++){
        auto*pool=field<void*>(scene(),uiOffsets[i]);retain(pool);
        release(scope->uiPools[i]);scope->uiPools[i]=pool;
    }
    if(velocity&&*velocity){
        try{
            scope->frame->motion=images(*velocity).resolved;
            if(!scope->frame->models){retain(*velocity);scope->pools.push_back(*velocity);}
        }catch(const std::exception&e){error(e.what());}
    }
    originalPost(self,rhi,view,velocity);
    // Later drawing consumes the delivered color domain, not the geometry producer's low domain.
    if(scope->statesReady){cpuDomain(scope->queued);gpuDomain(scope->queued);}
    scope->post=false;
}
void __fastcall render(void*renderer,void*rhi){
    Scope local{renderer,rhi};auto*previous=scope;scope=&local;
    originalRender(renderer,rhi);
    if(local.view){
        try{
            if(local.uiActive)error("Native UI target was not closed");
            if(local.statesReady)cpuDomain(local.queued);
        }catch(const std::exception&e){error(e.what());}
        if(!local.queued){std::lock_guard<std::mutex>g(backendLock);backend.pending=false;}
        if(!local.queued){
            if(local.frame->models)error("Admitted View did not execute its temporal graph node");
            reportStreamlineInputRejection("Native View did not reach an associated final scene compositor");
        }
    }
    for(auto*p:local.pools)release(p);
    for(auto*p:local.uiPools)release(p);
    scope=previous;
}
void __fastcall reset(void*list){discardSceneWork(list);originalReset(list);}
}
extern "C" void ac7_temporal_build(void*context,unsigned char*view,bool*percentage,const float*panini)noexcept{build(context,view,percentage,panini);}
extern "C" void ac7_temporal_color(void*context)noexcept{colorBuild(context);}
void invalidateSceneHistory()noexcept{invalidate=true;}
bool admitSceneView(void*renderer,void*rhi,unsigned char*view,SceneSettings&settings)noexcept{
    if(!scope||scope->renderer!=renderer||scope->rhi!=rhi||scope->view||!canQueueSceneWork(rhi)||!settings.enabled)return false;
    try{
        auto*family=field<void*>(view,0);
        if(!family||!field<unsigned char>(renderer,0x7e)||!(field<unsigned char>(family,0x38)&1)||field<unsigned>(view,0x13e8)<2||field<unsigned>(view,0x9b0)!=0)return false;
        auto depth=images(field<void*>(scene(),0x60));
        ComPtr<ID3D11Device>device;depth.target->GetDevice(&device);
        std::lock_guard<std::mutex>g(backendLock);
        if(backend.pending||(backend.seen&&backend.stream==field<void*>(view,0x1410)&&backend.nativeCounter==field<unsigned>(family,0x68)))return false;
        if(backend.device&&backend.device!=device)throw std::runtime_error("Temporal device changed");
        if(!backend.device){backend.device=device;device->GetImmediateContext(&backend.context);}
        const auto capabilities=GpuContext::acquire(device.Get()).modelCapabilities(settings.scale>0&&settings.scale<1.f,settings.nr.enabled);
        settings.nr.enabled=capabilities.nr;
        if(!capabilities.sr)settings.scale=capabilities.nr?1.f:0.f;
        const bool models=settings.scale>0;
        if(!models){
            settings.sharpness=0;
            if(!streamlineGenerationRequested()){settings.enabled=false;return false;}
        }
        const float scale=models?settings.scale:1.f;
        if(!(scale>0&&scale<=1))return false;
        std::array<int,4>output{},low{};std::memcpy(output.data(),view+0x80,16);std::memcpy(low.data(),view+0x70,16);
        if(models&&low!=output)return false;
        const auto high=rectangle(output);if(models)for(unsigned i=0;i<4;i++)low[i]=int(output[i]*scale);const auto input=rectangle(low);
        const unsigned aw=field<unsigned>(scene(),0x208),ah=field<unsigned>(scene(),0x20c),bytes=field<unsigned>(base,0x3caa6c8);
        if(input.x+input.width>aw||input.y+input.height>ah||(models&&(high.x+high.width>aw||high.y+high.height>ah))||bytes<138*16||bytes>0xcf0)return false;
        D3D11_TEXTURE2D_DESC d{};depth.target->GetDesc(&d);
        if(d.Width!=aw||d.Height!=ah||d.Format!=DXGI_FORMAT_R32G8X24_TYPELESS||d.SampleDesc.Count!=1||d.ArraySize!=1||d.MipLevels!=1)return false;
        auto frame=std::make_shared<SceneFrame>();frame->low=input;frame->high=high;frame->models=models;frame->allocation={aw,ah};frame->constantBytes=bytes;
        frame->nrSettings=settings.nr;frame->sharpness=settings.sharpness;frame->depth=depth.resolved;
        frame->stream=field<void*>(view,0x1410);frame->nativeCounter=field<unsigned>(family,0x68);
        auto job=std::make_unique<Job>(Job{frame});auto command=prepareSceneWork(job.get(),runJob,cancelJob);
        scope->pools.reserve(3);scope->motionModes.reserve(2);
        if(!models){
            auto*pool=field<void*>(scene(),0x60);retain(pool);scope->pools.push_back(pool);
            scope->frame=std::move(frame);scope->job=std::move(job);scope->command=std::move(command);
            backend.pending=true;scope->view=view;return true;
        }
        if(!backend.sr)backend.sr=std::make_unique<SceneProcessor>(GpuContext::acquire(device.Get()));
        if(!backend.sr->reserve(input.width,input.height,high.width,high.height,scale<1.f,settings.nr))return false;
        native<void(__fastcall*)(void*,void*)>(0x1095010)(scene(),rhi);
        auto*slot=native<void**(__fastcall*)(void*)>(0x109f1c0)(scene());
        if(!slot||slot==reinterpret_cast<void**>(base+0x368bf98)||!*slot)throw std::runtime_error("No native SceneColor for temporal admission");
        frame->color=images(*slot);
        D3D11_TEXTURE2D_DESC colorDesc{};frame->color.resolved->GetDesc(&colorDesc);
        if(colorDesc.Width!=aw||colorDesc.Height!=ah)return false;
        for(auto*p:{*slot,field<void*>(scene(),0x60)}){retain(p);scope->pools.push_back(p);}
        std::memcpy(frame->descriptor.data(),static_cast<unsigned char*>(*slot)+0x50,0x50);
        auto&desc=frame->descriptor;field<unsigned>(desc.data(),0x30)&=0x7fefffffu;field<unsigned short>(desc.data(),0x28)=1;field<unsigned>(desc.data(),0x2c)=10;field<unsigned>(desc.data(),0x34)=(field<unsigned>(desc.data(),0x34)&~0x10001u)|1u;
        field<unsigned char>(desc.data(),0x38)=0;field<unsigned char>(desc.data(),0x48)=0;field<const wchar_t*>(desc.data(),0x40)=L"AC7TemporalSR";
        void*pool=nullptr;
        native<bool(__fastcall*)(void*,void*,const void*,void**,const wchar_t*,bool,bool)>(0x109bb20)(base+0x368ba80,rhi,desc.data(),&pool,L"AC7TemporalSR",false,true);
        if(!pool)throw std::runtime_error("Temporal color output allocation failed");scope->pools.push_back(pool);
        frame->output=images(pool);D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        check(device->CreateRenderTargetView(frame->output.target.Get(),&rtv,&frame->target));
        auto node=std::make_unique<ModelPass>(frame);field<void**>(node.get(),0)=modelVtable.data();
        std::memcpy(node->native.data()+0x50,desc.data(),desc.size());retain(pool);field<void*>(node.get(),0xa0)=pool;
        scope->frame=std::move(frame);scope->prepared=std::move(node);scope->job=std::move(job);scope->command=std::move(command);
        backend.pending=true;scope->view=view;std::memcpy(view+0x70,low.data(),16);return true;
    }catch(const std::exception&e){error(e.what());return false;}
}
void executeSceneFrame(SceneFrame*f){
    if(!f)throw std::invalid_argument("Missing temporal frame");
    const auto&settings=f->nrSettings;const float sharpness=f->sharpness;
    if(!f->validIdentity)throw std::runtime_error("Missing temporal View identity");
    uint64_t token=0,sequence=0;
    {
        std::lock_guard<std::mutex>g(backendLock);
            check(backend.device->GetDeviceRemovedReason());
            if(f->models&&invalidate.exchange(false))backend.sr->invalidate();
                const unsigned delta=f->nativeCounter-backend.nativeCounter;const auto&id=f->identity;
                const bool restart=!backend.seen||f->stream!=backend.stream||!delta||delta>=0x80000000u||id.current[0]<backend.lastView.current[0]||id.current[1]<backend.lastView.current[1]||id.rect!=backend.lastView.rect||id.allocation!=backend.lastView.allocation;
                token=backend.seen?backend.token+(restart?1:delta):f->nativeCounter;sequence=backend.seen?backend.sequence+unsigned(restart):1;
                if(backend.seen&&token<=backend.token)throw std::runtime_error("Temporal token overflow");
                std::array<ID3D11Texture2D*,2>normalized{};
                if(f->models){
                    const float black[4]{};backend.context->ClearRenderTargetView(f->target.Get(),black);
                    if(!backend.sr->prepare(backend.context.Get(),id,f->depth.Get(),f->motion.Get()))throw std::runtime_error("Temporal guides unavailable");
                    normalized=backend.sr->preparedGuides();
                    D3D11_VIEWPORT viewport{float(f->high.x),float(f->high.y),float(f->high.width),float(f->high.height),0,1};
                    if(!backend.sr->draw(backend.context.Get(),f->color.resolved.Get(),f->target.Get(),viewport,token,sequence,settings,sharpness))throw std::runtime_error("Reserved temporal SR unavailable");
                }else{
                    if(!backend.nativeGuides||backend.guideWidth!=f->low.width||backend.guideHeight!=f->low.height){
                        backend.nativeGuides=std::make_unique<GuideNormalizer>(backend.device.Get(),f->low.width,f->low.height);
                        backend.guideWidth=f->low.width;backend.guideHeight=f->low.height;
                    }
                    ClipToPrevious transform;std::memcpy(transform.data(),id.bytes.data()+110*16,64);
                    backend.nativeGuides->record(backend.context.Get(),f->depth.Get(),f->motion.Get(),f->low,transform,
                        {2.f*id.jitter[0]/f->low.width,-2.f*id.jitter[1]/f->low.height});
                    normalized={backend.nativeGuides->depth(),backend.nativeGuides->motion()};
                }
                if(!backend.guideSnapshots){auto&gpu=GpuContext::acquire(backend.device.Get());backend.guideSnapshots=std::make_unique<GuideSnapshotPool>(backend.device.Get(),gpu.device());}
                auto&guides=*f->presentationGuides;
                guides.snapshot=backend.guideSnapshots->copy(backend.context.Get(),normalized[0],normalized[1]);
                guides.view=id;guides.nativeCounter=f->nativeCounter;guides.token=token;guides.sequence=sequence;
                guides.presentation=currentPresentationFrame();
                guides.camera=readViewCamera(id,base,f->panini,guides.cameraRefusal);
                backend.stream=f->stream;backend.nativeCounter=f->nativeCounter;backend.token=token;backend.sequence=sequence;backend.lastView=id;backend.seen=true;
            if(f->models){
                if(f->output.resolved!=f->output.target)backend.context->CopyResource(f->output.resolved.Get(),f->output.target.Get());
                upload(*f->highState.constants);
            }
        backend.pending=false;
    }
}
bool installRenderGraph(void(*run)(SceneFrame*)noexcept,void(*failure)(const char*)noexcept){
    base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));if(!base||!run||!failure||!initializeSceneRhiQueue())return false;
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x100000)return false;
    const auto*pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||pe->OptionalHeader.SizeOfImage<0x3caa6cc)return false;
    constexpr unsigned char renderSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x58,0xfd,0xff,0xff};
    constexpr unsigned char postSig[]={0x4c,0x8b,0xdc,0x55,0x49,0x8d,0xab,0xe8,0xfc,0xff,0xff};
    constexpr unsigned char buildSig[]={0x48,0x8d,0x1d,0xfa,0xab,0x3f,0xff};
    constexpr unsigned char colorSig[]={0x48,0x8b,0x05,0x37,0x5e,0xc3,0x02};
    constexpr unsigned char gatherSig[]={0x4c,0x8b,0xdc,0x57,0x48,0x81,0xec,0x90,0x00,0x00,0x00,0x48};
    constexpr unsigned char processSig[]={0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x53,0x48};
    constexpr unsigned char motionSig[]={0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x8d,0x6c,0x24};
    constexpr unsigned char descriptorSig[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b};
    constexpr unsigned char constructorSig[]={0x48,0x8d,0x05,0x51,0xd6,0xbd,0x01,0xc7,0x41,0x08,0x00,0x00};
    constexpr unsigned char resetSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xc6,0x41,0x10,0};
    constexpr unsigned char uiBeginSig[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xec,0x30};
    constexpr unsigned char uiEndSig[]={0x40,0x57,0x48,0x83,0xec,0x40,0x83,0xb9,0x58,0x02,0,0,0x02};
    constexpr unsigned char viewportSig[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    constexpr unsigned char inputPoolSig[]={0x48,0x8b,0x0a,0x48,0x89,0x8b,0xa8,0,0,0};
    constexpr unsigned char inputVtableSig[]={0x48,0x8d,0x05,0x38,0x3f,0xad,0x01};
    constexpr unsigned char compositeConstructorSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0xc2,0xe3,0xff,0xff};
    if(std::memcmp(base+0xef5cc0,renderSig,sizeof(renderSig))||std::memcmp(base+0xffb900,postSig,sizeof(postSig))||std::memcmp(base+0xffc2cf,buildSig,sizeof(buildSig))||std::memcmp(base+0xffc83a,colorSig,sizeof(colorSig)))return false;
    if(std::memcmp(base+0x10b3cc0,gatherSig,sizeof(gatherSig))||std::memcmp(base+0x10b3f10,processSig,sizeof(processSig))||std::memcmp(base+0x10054e0,motionSig,sizeof(motionSig))||std::memcmp(base+0xff7c70,descriptorSig,sizeof(descriptorSig))||std::memcmp(base+0xe9e070,constructorSig,sizeof(constructorSig))||std::memcmp(base+0x1216a70,resetSig,sizeof(resetSig)))return false;
    if(std::memcmp(base+0x1097d20,uiBeginSig,sizeof(uiBeginSig))||std::memcmp(base+0x109d320,uiEndSig,sizeof(uiEndSig))||std::memcmp(base+0xec8dd0,viewportSig,sizeof(viewportSig)))return false;
    if(std::memcmp(base+0xff1dc5,inputPoolSig,sizeof(inputPoolSig))||std::memcmp(base+0xff1d81,inputVtableSig,sizeof(inputVtableSig)))return false;
    if(std::memcmp(base+0xff2060,compositeConstructorSig,sizeof(compositeConstructorSig))||field<void*>(base+0x2ac7288,5*sizeof(void*))!=base+0xfc86c0)return false;
    std::memcpy(modelVtable.data(),base+0x2ac6a90,sizeof(modelVtable));modelVtable[5]=reinterpret_cast<void*>(&modelProcess);modelVtable[14]=reinterpret_cast<void*>(&modelDescriptor);modelVtable[15]=reinterpret_cast<void*>(&modelRelease);
    void*targets[]={base+0xef5cc0,base+0xffb900,base+0xffc2cf,base+0xffc83a,base+0x10b3cc0,base+0x10b3f10,base+0x10054e0,base+0xff7c70,base+0x1097d20,base+0x109d320,base+0x1216a70,base+0xfc86c0};
    void*detours[]={reinterpret_cast<void*>(&render),reinterpret_cast<void*>(&post),reinterpret_cast<void*>(&ac7_temporal_build_thunk),reinterpret_cast<void*>(&ac7_temporal_color_thunk),reinterpret_cast<void*>(&gather),reinterpret_cast<void*>(&process),reinterpret_cast<void*>(&motion),reinterpret_cast<void*>(&motionDescriptor),reinterpret_cast<void*>(&uiBegin),reinterpret_cast<void*>(&uiEnd),reinterpret_cast<void*>(&reset),reinterpret_cast<void*>(&composite)};
    void**originals[]={reinterpret_cast<void**>(&originalRender),reinterpret_cast<void**>(&originalPost),&ac7_temporal_original_build,&ac7_temporal_original_color,reinterpret_cast<void**>(&originalGather),reinterpret_cast<void**>(&originalProcess),reinterpret_cast<void**>(&originalMotion),reinterpret_cast<void**>(&originalMotionDescriptor),reinterpret_cast<void**>(&originalUiBegin),reinterpret_cast<void**>(&originalUiEnd),reinterpret_cast<void**>(&originalReset),reinterpret_cast<void**>(&originalComposite)};
    runFrame=run;reportError=failure;size_t installed=0;
    for(;installed<std::size(targets);installed++)if(MH_CreateHook(targets[installed],detours[installed],originals[installed])!=MH_OK)break;
    if(installed!=std::size(targets)){while(installed)MH_RemoveHook(targets[--installed]);return false;}
    for(auto*t:targets)if(MH_QueueEnableHook(t)!=MH_OK)return false;
    if(MH_ApplyQueued()!=MH_OK)return false;
    return true;
}
}
