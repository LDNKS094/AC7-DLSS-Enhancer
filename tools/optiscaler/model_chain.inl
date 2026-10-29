// Included after color_probe.cpp's fixed-layout GPU I/O helpers. No NGX loader link.
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include "../nr/evaluation_parameters.h"
#include <iomanip>
#include "../../src/d3d12/chain_publication.h"
#include <memory>
#include <thread>
namespace {
void ngxResult(NVSDK_NGX_Result r,const char*stage){std::cout<<stage<<" result="<<unsigned(r)<<std::endl;need(NVSDK_NGX_SUCCEED(r),stage);}
template<class T>T exported(HMODULE m,const char*name){auto p=GetProcAddress(m,name);need(p!=nullptr,name);return reinterpret_cast<T>(p);}
struct PublicNgx12 {
    HMODULE module=nullptr;
    static constexpr bool direct=
#ifdef AC7_DIRECT_NGX
        true;
#else
        false;
#endif
    decltype(&NVSDK_NGX_D3D12_Init_with_ProjectID) init;
    decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters) get;
    decltype(&NVSDK_NGX_D3D12_CreateFeature) create;
    decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluate;
    decltype(&NVSDK_NGX_D3D12_ReleaseFeature) release;
    decltype(&NVSDK_NGX_D3D12_DestroyParameters) destroy;
    using Shutdown=NVSDK_NGX_Result(NVSDK_CONV*)();Shutdown shutdown;
    PublicNgx12(const fs::path& dll){
#ifdef AC7_DIRECT_NGX
        (void)dll;init=&NVSDK_NGX_D3D12_Init_with_ProjectID;get=&NVSDK_NGX_D3D12_GetCapabilityParameters;create=&NVSDK_NGX_D3D12_CreateFeature;evaluate=&NVSDK_NGX_D3D12_EvaluateFeature;release=&NVSDK_NGX_D3D12_ReleaseFeature;destroy=&NVSDK_NGX_D3D12_DestroyParameters;shutdown=nullptr;
#else
        module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);need(module!=nullptr,"OptiScaler LoadLibrary failed");
        init=exported<decltype(init)>(module,"NVSDK_NGX_D3D12_Init_with_ProjectID");get=exported<decltype(get)>(module,"NVSDK_NGX_D3D12_GetCapabilityParameters");create=exported<decltype(create)>(module,"NVSDK_NGX_D3D12_CreateFeature");evaluate=exported<decltype(evaluate)>(module,"NVSDK_NGX_D3D12_EvaluateFeature");release=exported<decltype(release)>(module,"NVSDK_NGX_D3D12_ReleaseFeature");destroy=exported<decltype(destroy)>(module,"NVSDK_NGX_D3D12_DestroyParameters");shutdown=exported<Shutdown>(module,"NVSDK_NGX_D3D12_Shutdown");
#endif
    }
    NVSDK_NGX_Result close(ID3D12Device* device){
#ifdef AC7_DIRECT_NGX
        return NVSDK_NGX_D3D12_Shutdown1(device);
#else
        (void)device;return shutdown();
#endif
    }
    // No FreeLibrary: process-global package hooks and background work are not hot-unload qualified.
};
struct ChainSession {
    PublicNgx12& api;ID3D12Device* device=nullptr;NVSDK_NGX_Parameter*p[2]{};NVSDK_NGX_Handle*feature[2]{};bool initialized=false;
    explicit ChainSession(PublicNgx12& a):api(a){}
    bool cleanup(){bool ok=true;for(int i=1;i>=0;i--){if(feature[i]){const auto r=api.release(feature[i]);std::cout<<(i?"ReleaseSR":"ReleaseNR")<<" result="<<unsigned(r)<<std::endl;ok&=NVSDK_NGX_SUCCEED(r);feature[i]=nullptr;}if(p[i]){const auto r=api.destroy(p[i]);std::cout<<"DestroyChainParameters index="<<i<<" result="<<unsigned(r)<<std::endl;ok&=NVSDK_NGX_SUCCEED(r);p[i]=nullptr;}}if(initialized){const auto r=api.close(device);std::cout<<"ShutdownChain result="<<unsigned(r)<<std::endl;ok&=NVSDK_NGX_SUCCEED(r);initialized=false;}return ok;}
    ~ChainSession(){if(initialized&&!cleanup())std::cerr<<"Chain cleanup failed\n";}
    void start(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12GraphicsCommandList*l,const fs::path& lib,const fs::path& out){
        device=d;
        const wchar_t* paths[]={lib.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};ngxResult(api.init("05882ed0-d4cd-4063-a726-0b4862bad3dc",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"Independent public fixed-eight chain",out.c_str(),d,&info,NVSDK_NGX_Version_API),"InitChain");initialized=true;
        for(unsigned i=0;i<2;i++){
            ngxResult(api.get(&p[i]),i?"GetSRParameters":"GetNRParameters");need(p[i]!=nullptr,"Missing chain parameters");int available=0;ngxResult(p[i]->Get(i?NVSDK_NGX_Parameter_SuperSampling_Available:NVSDK_NGX_Parameter_DLSSNR_Available,&available),i?"SRAvailableQuery":"NRAvailableQuery");need(available==1,"Chain model unavailable");
            if(!i){p[i]->Set(NVSDK_NGX_Parameter_DLSSNR_Width,W);p[i]->Set(NVSDK_NGX_Parameter_DLSSNR_Height,H);}
            else {p[i]->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);p[i]->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);p[i]->Set(NVSDK_NGX_Parameter_Width,W);p[i]->Set(NVSDK_NGX_Parameter_Height,H);p[i]->Set(NVSDK_NGX_Parameter_OutWidth,W);p[i]->Set(NVSDK_NGX_Parameter_OutHeight,H);p[i]->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_DLAA));p[i]->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));p[i]->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,0);}
            const auto result=api.create(l,i?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_DLSSNR,p[i],&feature[i]);std::cout<<(i?"CreateChainSR":"CreateChainNR")<<" result="<<unsigned(result)<<std::endl;
            if(!NVSDK_NGX_SUCCEED(result)||!feature[i]){finish(d,q,l);need(false,"Chain feature creation failed");}
        }
        finish(d,q,l);
        for(const auto* name:{L"nvngx_dlssnr.dll",L"nvngx_dlss.dll"}){wchar_t path[32768]{};const auto module=GetModuleHandleW(name);if(module&&GetModuleFileNameW(module,path,32768))std::wcout<<L"LoadedChainModel name="<<name<<L" path="<<path<<std::endl;}
    }
};
int modelChain(wchar_t**argv,bool resetSr,bool sharedMode=false,bool faultMode=false){
    const fs::path source=fs::absolute(argv[1]),base=fs::absolute(argv[2]),out=fs::absolute(argv[3]),dll=fs::absolute(argv[4]),lib=fs::absolute(argv[5]);need(!fs::exists(out),"Refusing existing chain output");need((PublicNgx12::direct||fs::is_regular_file(dll))&&fs::is_regular_file(lib/L"nvngx_dlss.dll")&&fs::is_regular_file(lib/L"nvngx_dlssnr.dll"),"Missing chain runtime");
    for(unsigned i=0;i<8;i++){const auto name=L"frame"+std::to_wstring(i);load(source/(name+L"-scene.raw"),5120,H,26,4);gain(source/(name+L"-exposure.raw"));load(base/L"nr-input"/name/L"depth.raw",W,H,41,4);load(base/L"nr-input"/name/L"motion.raw",W,H,34,4);load(base/L"sr-input"/name/L"exposure.raw",1,1,41,4);std::ifstream time(base/L"sr-input"/name/L"frame-time.bin",std::ios::binary|std::ios::ate);need(bool(time)&&time.tellg()==4,"Missing chain actual dt");float dt=0;time.seekg(0);time.read(reinterpret_cast<char*>(&dt),4);need(bool(time)&&std::isfinite(dt)&&dt>0,"Invalid chain preflight dt");}
    if(faultMode)for(unsigned f:{3u,4u,6u})for(const auto* file:{L"nr-output.raw",L"sr-input.raw",L"sr-output.raw"})load(base.parent_path()/L"nonblocking-chain-v1/run-v2"/(L"frame"+std::to_wstring(f))/file,W,H,10,8);
    fs::create_directory(out);std::ofstream(out/L"INCOMPLETE")<<"Model chain not accepted\n";SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    ComPtr<ID3D11Device>d11;ComPtr<ID3D11DeviceContext>context;D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,&level,1,D3D11_SDK_VERSION,&d11,nullptr,&context));PublicNgx12 api(dll);std::cout<<"Route="<<(PublicNgx12::direct?"direct-ngx":"optiscaler")<<std::endl;
    ComPtr<ID3D12Debug>debug;ck(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<IDXGIDevice>dxgi;ComPtr<IDXGIAdapter>adapter;ck(d11.As(&dxgi));ck(dxgi->GetAdapter(&adapter));ComPtr<ID3D12Device>d12;ck(D3D12CreateDevice(adapter.Get(),level,IID_PPV_ARGS(&d12)));ComPtr<ID3D11InfoQueue>info11;ComPtr<ID3D12InfoQueue>info12;ck(d11.As(&info11));ck(d12.As(&info12));
    D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue>q;ck(d12->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&q)));ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;ck(d12->CreateCommandAllocator(queueDesc.Type,IID_PPV_ARGS(&a)));ck(d12->CreateCommandList(0,queueDesc.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
    {
        std::unique_ptr<ac7::SharedTextureBridge> bridge;std::unique_ptr<ac7::ChainPublication> submission;
        if(sharedMode){bridge=std::make_unique<ac7::SharedTextureBridge>(d11.Get(),d12.Get(),q.Get(),W,H,ac7::SharedTextureBridge::ColorInput::ac7NativeHdr);submission=std::make_unique<ac7::ChainPublication>(*bridge,d11.Get());}
        if(sharedMode){bool rejected=false;try{ac7::SharedTextureBridge invalid(d11.Get(),d12.Get(),q.Get(),W-1,H,ac7::SharedTextureBridge::ColorInput::ac7NativeHdr);}catch(const std::runtime_error&){rejected=true;}need(rejected,"Unsupported native bridge extent accepted");std::cout<<"NATIVE_BRIDGE_EXTENT_REFUSED=1"<<std::endl;}
        ac7::HdrProxy12 codec(d12.Get(),W,H);auto hdrRes=sharedMode?ComPtr<ID3D12Resource>(bridge->resource(0)):input12(d12.Get(),5120,26),depthRes=sharedMode?ComPtr<ID3D12Resource>(bridge->resource(1)):input12(d12.Get(),W,41),motionRes=sharedMode?ComPtr<ID3D12Resource>(bridge->resource(2)):input12(d12.Get(),W,34),exposureRes=sharedMode?ComPtr<ID3D12Resource>(bridge->resource(3)):input12(d12.Get(),1,41,1),nrRes=input12(d12.Get(),W,10,H,true),srRes=sharedMode?ComPtr<ID3D12Resource>(bridge->resource(4)):input12(d12.Get(),W,10,H,true);
        Surface hdr(d12.Get(),hdrRes.Get(),true),depth(d12.Get(),depthRes.Get(),true),motion(d12.Get(),motionRes.Get(),true),exposure(d12.Get(),exposureRes.Get(),true),nr(d12.Get(),nrRes.Get(),false),sr(d12.Get(),srRes.Get(),false),proxy(d12.Get(),codec.prepared(),false),restored(d12.Get(),codec.restored(),false),srInput(d12.Get(),codec.srColor(),false);
        ChainSession session(api);session.start(d12.Get(),q.Get(),l.Get(),lib,out);Raw seed{W,H,10,8,std::vector<unsigned char>(size_t(W)*H*8)};for(size_t i=0;i<seed.data.size();i+=2){seed.data[i]=0;seed.data[i+1]=0x7e;}
        unsigned dropped=0,stoppedPending=0,retirementPending=0,preExpired=0;
        for(unsigned f=0;f<8;f++){
            if(faultMode&&f==6){need(submission->stopping()&&submission->drained(),"Stopped owner not drained");submission=std::make_unique<ac7::ChainPublication>(*bridge,d11.Get());}
            const uint64_t token=(uint64_t(1)<<33)+f;
            const bool dualReset=sharedMode?submission->resetFor(token):f==0;
            if(faultMode&&f==1){need(!submission->begin(token,{},nullptr,ac7::FrameSubmission::Clock::time_point::min(),dualReset),"Pre-expired chain submitted");need(submission->drained(),"Pre-expired chain became active");preExpired++;std::cout<<"FAULT_PRE_EXPIRED index=1 modelCalls=0"<<std::endl;continue;}
            const auto name=L"frame"+std::to_wstring(f);const auto dir=out/name;fs::create_directory(dir);auto scene=load(source/(name+L"-scene.raw"),5120,H,26,4),dep=load(base/L"nr-input"/name/L"depth.raw",W,H,41,4),mv=load(base/L"nr-input"/name/L"motion.raw",W,H,34,4),exp=load(base/L"sr-input"/name/L"exposure.raw",1,1,41,4);float dt=0;std::ifstream time(base/L"sr-input"/name/L"frame-time.bin",std::ios::binary);time.read(reinterpret_cast<char*>(&dt),4);need(bool(time)&&std::isfinite(dt)&&dt>0,"Invalid actual chain dt");const float actualGain=gain(source/(name+L"-exposure.raw"));
            const auto deadline=ac7::FrameSubmission::Clock::now()+((faultMode&&(f==2||f==7))?std::chrono::milliseconds(200):std::chrono::milliseconds(5000)); // Diagnostic budget includes D11 initialization, recording and upload; not a production frame budget.
            std::array<ComPtr<ID3D11Texture2D>,4> inputs11;std::array<ID3D11Texture2D*,4> raw11{};ComPtr<ID3D11Texture2D> output11;
            ck(a->Reset());ck(l->Reset(a.Get(),nullptr));
            if(sharedMode){const Raw* originals[]={&scene,&dep,&mv,&exp};for(unsigned i=0;i<4;i++){inputs11[i]=input11(d11.Get(),*originals[i]);raw11[i]=inputs11[i].Get();barrier(l.Get(),bridge->resource(i),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}output11=input11(d11.Get(),seed);}
            else {hdr.put(l.Get(),scene);depth.put(l.Get(),dep);motion.put(l.Get(),mv);exposure.put(l.Get(),exp);}
            nr.put(l.Get(),seed);sr.put(l.Get(),seed);
            codec.prepare(l.Get(),hdrRes.Get(),{1280,0,W,H},actualGain);barrier(l.Get(),codec.prepared(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(l.Get(),nrRes.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            configureCapturedEvaluation(session.p[0],{codec.prepared(),depthRes.Get(),motionRes.Get(),nrRes.Get(),exposureRes.Get()},false,dualReset,false,dt);
            std::cout<<std::setprecision(9)<<"CHAIN_FRAME index="<<f<<" NRReset="<<dualReset<<" SRReset="<<(dualReset||resetSr)<<" dtMs="<<dt<<" gain="<<actualGain<<" NR="<<session.feature[0]<<" SR="<<session.feature[1]<<" proxy="<<codec.prepared()<<" output="<<srRes.Get()<<" token="<<token<<std::endl;
            ngxResult(api.evaluate(l.Get(),session.feature[0],session.p[0],nullptr),"EvaluateChainNR");barrier(l.Get(),codec.prepared(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);barrier(l.Get(),nrRes.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            codec.restore(l.Get(),hdrRes.Get(),{1280,0,W,H},actualGain,nrRes.Get());barrier(l.Get(),nrRes.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);barrier(l.Get(),codec.srColor(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(l.Get(),srRes.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            configureCapturedEvaluation(session.p[1],{codec.srColor(),depthRes.Get(),motionRes.Get(),srRes.Get(),exposureRes.Get()},true,dualReset||resetSr,false,dt);ngxResult(api.evaluate(l.Get(),session.feature[1],session.p[1],nullptr),"EvaluateChainSR");barrier(l.Get(),codec.srColor(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);barrier(l.Get(),srRes.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
            for(auto*s:{&proxy,&nr,&restored,&srInput,&sr,&hdr,&depth,&motion,&exposure})s->take(l.Get());
            bool published=true;
            if(sharedMode){
                for(unsigned i=0;i<4;i++)barrier(l.Get(),bridge->resource(i),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);ck(l->Close());
                if(f==0){auto wrong=raw11;wrong[0]=output11.Get();bool rejected=false;try{bridge->upload(token,wrong);}catch(const std::runtime_error&){rejected=true;}need(rejected&&!bridge->busy(),"Wrong native input accepted or consumed transaction");std::cout<<"NATIVE_BRIDGE_INPUT_REFUSED=1"<<std::endl;}
                ComPtr<ID3D12Fence> gate,completed,gate11Cpu;ComPtr<ID3D11Fence> gate11;ComPtr<ID3D11DeviceContext4> context4;ck(context.As(&context4));
                try{
                    if(faultMode&&(f==2||f==5)){ck(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));ck(q->Wait(gate.Get(),1));}
                    if(faultMode&&f==0){bool rejected=false;try{submission->begin(token,raw11,l.Get(),deadline,!dualReset);}catch(const std::invalid_argument&){rejected=true;}need(rejected,"Mismatched dual Reset accepted");std::cout<<"FAULT_RESET_CONTRACT_REFUSED=1"<<std::endl;}
                    need(submission->begin(token,raw11,l.Get(),deadline,dualReset),"Unexpected shared chain pre-expiry");
                    if(!faultMode&&f==2){const Raw* sources[]={&scene,&dep,&mv,&exp};for(unsigned i=0;i<4;i++){std::vector<unsigned char> zero(sources[i]->data.size());context->UpdateSubresource(inputs11[i].Get(),0,nullptr,zero.data(),sources[i]->w*sources[i]->bpp,0);}std::cout<<"SOURCE_OVERWRITE index=2 layouts=4 afterQueuedInputCopy=1"<<std::endl;}
                    if(faultMode&&(f==2||f==5)){
                        need(submission->poll(output11.Get())==ac7::SharedTextureBridge::Result::pending,"Gated chain not pending");
                        if(f==5){need(!submission->begin(token+1,raw11,l.Get(),deadline,true),"Busy chain accepted next frame");submission->stop();need(!submission->begin(token+2,raw11,l.Get(),deadline,true),"Stopped chain accepted frame");stoppedPending++;}
                        else {while(ac7::FrameSubmission::Clock::now()<deadline){need(submission->poll(output11.Get())==ac7::SharedTextureBridge::Result::pending,"Expired gated chain completed");std::this_thread::sleep_for(std::chrono::milliseconds(1));}need(submission->poll(output11.Get())==ac7::SharedTextureBridge::Result::pending,"Deadline failed while gated");}
                        need(!submission->drained(),"Pending resources released early");ck(gate->Signal(1));
                    }
                    if(faultMode&&f==7){ck(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completed)));ck(q->Signal(completed.Get(),1));const auto watchdog=ac7::FrameSubmission::Clock::now()+std::chrono::seconds(10);while(completed->GetCompletedValue()<1){if(ac7::FrameSubmission::Clock::now()>watchdog)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(ac7::FrameSubmission::Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                    if(faultMode&&f==4){ComPtr<ID3D11Device5>d5;ck(d11.As(&d5));ck(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&gate11Cpu)));HANDLE gateHandle=nullptr;ck(d12->CreateSharedHandle(gate11Cpu.Get(),nullptr,GENERIC_ALL,nullptr,&gateHandle));const auto openGate=d5->OpenSharedFence(gateHandle,IID_PPV_ARGS(&gate11));CloseHandle(gateHandle);ck(openGate);ck(context4->Wait(gate11.Get(),1));const auto watchdog=ac7::FrameSubmission::Clock::now()+std::chrono::seconds(10);while(!submission->hasRetirement()){need(submission->poll(output11.Get())==ac7::SharedTextureBridge::Result::pending,"D11 gated result prematurely published");if(ac7::FrameSubmission::Clock::now()>watchdog)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}need(!submission->drained(),"D11 resources released before copy retirement");for(unsigned i=0;i<8;i++)need(submission->poll(output11.Get())==ac7::SharedTextureBridge::Result::pending,"D11 retirement gate ignored");retirementPending+=8;ck(gate11Cpu->Signal(1));}
                    const auto end=ac7::FrameSubmission::Clock::now()+std::chrono::seconds(10);ac7::SharedTextureBridge::Result result;while((result=submission->poll(output11.Get()))==ac7::SharedTextureBridge::Result::pending){if(ac7::FrameSubmission::Clock::now()>=end)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}published=result==ac7::SharedTextureBridge::Result::published;
                    need(published==!(faultMode&&(f==2||f==5||f==7)),"Unexpected shared chain publication");need(submission->drained(),"Returned before dual API retirement");if(!published){dropped++;std::cout<<"FAULT_DROP index="<<f<<" lateOrStopped=1 unchangedDestinationPendingCheck=1"<<std::endl;}
                }catch(const std::exception&e){if(bridge->busy()||!submission->drained()){std::cerr<<"STOP pending shared chain: "<<e.what()<<std::endl;ExitProcess(10);}throw;}
            }else finish(d12.Get(),q.Get(),l.Get());
            const auto p=proxy.get(),n=nr.get(),r=restored.get(),s=srInput.get(),answer=sr.get();save(dir/L"proxy.raw",10,8,p);save(dir/L"nr-output.raw",10,8,n);save(dir/L"restored.raw",26,4,r);save(dir/L"sr-input.raw",10,8,s);save(dir/L"sr-output.raw",10,8,answer);
            need(p==load(base/L"nr-input"/name/L"color.raw",W,H,10,8).data,"Chain proxy mismatch");
            if(!faultMode||f==0){need(n==load(base/L"nr-output"/name/L"nr-output.raw",W,H,10,8).data,"Chain NR mismatch");need(r==crop(load(base/L"gpu-restore"/name/L"scene-restored.raw",5120,H,26,4)),"Chain HDR restore mismatch");need(s==load(base/L"sr-input"/name/L"color.raw",W,H,10,8).data,"Chain SR input mismatch");need(answer==load(base/(resetSr?L"reset-control":L"history")/name/L"sr-output.raw",W,H,10,8).data,"Chain SR output mismatch");}
            else if(f==3||f==4||f==6){const auto reference=base.parent_path()/L"nonblocking-chain-v1/run-v2"/name;need(n==load(reference/L"nr-output.raw",W,H,10,8).data,"Dual Reset NR reference mismatch");need(s==load(reference/L"sr-input.raw",W,H,10,8).data,"Dual Reset HDR/SR input reference mismatch");need(answer==load(reference/L"sr-output.raw",W,H,10,8).data,"Dual Reset SR reference mismatch");std::cout<<"RECOVERY_REFERENCE index="<<f<<" NR=1 SRInput=1 SR=1"<<std::endl;}
            need(n!=p&&answer!=s,"Model output has no response");need(hdr.get()==scene.data&&depth.get()==dep.data&&motion.get()==mv.data&&exposure.get()==exp.data,"Chain original input changed");std::cout<<"CHAIN_RESULT index="<<f<<" exactStages="<<(faultMode?-1:5)<<" inputUnchanged=4"<<std::endl;
            if(sharedMode){const auto returned=read11(d11.Get(),context.Get(),output11.Get(),8);save(dir/L"returned11.raw",10,8,returned);need(returned==(published?answer:seed.data),"Returned D11 private output mismatch");const Raw* originals[]={&scene,&dep,&mv,&exp};for(unsigned i=0;i<4;i++)need(read11(d11.Get(),context.Get(),inputs11[i].Get(),originals[i]->bpp)==((!faultMode&&f==2)?std::vector<unsigned char>(originals[i]->data.size()):originals[i]->data),"Original D11 source changed unexpectedly");std::cout<<"BRIDGE_RESULT index="<<f<<" published="<<published<<" returnedExact=1 originalD11Inputs=4"<<std::endl;}
        }
        if(sharedMode){submission->stop();need(submission->drained(),"Idle close failed");}
        if(faultMode){need(dropped==3&&preExpired==1&&stoppedPending==1&&retirementPending==8,"Fault matrix incomplete");std::cout<<"FAULT_MATRIX drops=3 preExpired=1 stoppedPending=1 D11RetirementPending=8 originalFallbacks=4"<<std::endl;}
        need(session.cleanup(),"Chain cleanup failed");
    }
    unsigned errors=0;for(UINT64 i=0;i<info11->GetNumStoredMessages();i++){SIZE_T n=0;ck(info11->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D11_MESSAGE*>(b.data());ck(info11->GetMessage(i,m,&n));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}for(UINT64 i=0;i<info12->GetNumStoredMessages();i++){SIZE_T n=0;ck(info12->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D12_MESSAGE*>(b.data());ck(info12->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}std::cout<<"ChainDebugErrors="<<errors<<std::endl;need(errors==0,"Chain debug errors");ck(d11->GetDeviceRemovedReason());ck(d12->GetDeviceRemovedReason());
    std::ofstream report(out/L"chain-checks.json");report<<"{\"kind\":\"PUBLIC_OPTISCALER_D3D12_NR_HDR_SR\",\"frames\":8,\"exactStages\":"<<(faultMode?20:40)<<",\"inputReadbacksExact\":"<<(faultMode?28:32)<<",\"nrEvaluations\":"<<(faultMode?7:8)<<",\"srEvaluations\":"<<(faultMode?7:8)<<",\"faultTest\":"<<(faultMode?"true":"false")<<",\"sharedD11Bridge\":"<<(sharedMode?"true":"false")<<",\"srResetEveryFrame\":"<<(resetSr?"true":"false")<<",\"singleD3d12Queue\":true,\"gpuBasedValidation\":false,\"bothDebugErrors\":0,\"intermediateCpuImageFeedback\":false,\"gameIntegration\":false,\"performanceAccepted\":false}";report.close();need(bool(report),"Chain report write failed");fs::remove(out/L"INCOMPLETE");std::cout<<"PASS Public chain NR="<<(faultMode?7:8)<<" SR="<<(faultMode?7:8)<<" shared="<<sharedMode<<" faults="<<faultMode<<" Debug0\n";return 0;
}
}
