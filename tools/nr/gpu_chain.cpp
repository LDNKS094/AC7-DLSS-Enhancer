// Independent fixed8 GPU chain. Files initialize native fixtures and validate outputs only.
#include "evaluation_parameters.h"
#include "frame_submission.h"
#include "chain_schedule.h"
#include "hdr_proxy.h"
#include <nvsdk_ngx_defs_dlssnr.h>
#include <d3dcompiler.h>
#include <d3d11sdklayers.h>
#include <d3d12sdklayers.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <memory>
using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned W=2560,H=1440;
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("GPU chain HRESULT "+std::to_string(h));}
void ngx(NVSDK_NGX_Result r,const char* stage){std::cout<<stage<<" result=0x"<<std::hex<<unsigned(r)<<std::dec<<std::endl;need(NVSDK_NGX_SUCCEED(r),stage);}
struct Raw {unsigned w,h,format,bpp;std::vector<unsigned char> bytes;};
Raw raw(const std::filesystem::path& p,unsigned w,unsigned h,unsigned format,unsigned bpp){
    std::ifstream in(p,std::ios::binary|std::ios::ate);need(bool(in),"Missing raw fixture/reference");need(in.tellg()==std::streamoff(16+size_t(w)*h*bpp),"Wrong raw length");in.seekg(0);
    unsigned header[4]{};in.read(reinterpret_cast<char*>(header),16);need(header[0]==w&&header[1]==h&&header[2]==format&&header[3]==bpp,"Wrong raw header");
    Raw r{w,h,format,bpp,std::vector<unsigned char>(size_t(w)*h*bpp)};in.read(reinterpret_cast<char*>(r.bytes.data()),r.bytes.size());need(bool(in),"Read raw failed");return r;
}
ComPtr<ID3D11Texture2D> texture(ID3D11Device* d,const Raw& r){D3D11_TEXTURE2D_DESC t{};t.Width=r.w;t.Height=r.h;t.Format=DXGI_FORMAT(r.format);t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_SHADER_RESOURCE|(r.bytes.empty()?D3D11_BIND_UNORDERED_ACCESS:0);
    D3D11_SUBRESOURCE_DATA data{r.bytes.data(),r.w*r.bpp,0};ComPtr<ID3D11Texture2D> result;ck(d->CreateTexture2D(&t,r.bytes.empty()?nullptr:&data,&result));return result;}
std::vector<unsigned char> readback(ID3D11DeviceContext* c,ID3D11Texture2D* t,unsigned bpp){ComPtr<ID3D11Device>d;c->GetDevice(&d);D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=desc.MiscFlags=0;ComPtr<ID3D11Texture2D>s;ck(d->CreateTexture2D(&desc,nullptr,&s));c->CopyResource(s.Get(),t);
    D3D11_MAPPED_SUBRESOURCE m{};ck(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m));std::vector<unsigned char>b(size_t(desc.Width)*desc.Height*bpp);for(unsigned y=0;y<desc.Height;y++)std::memcpy(b.data()+size_t(y)*desc.Width*bpp,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(desc.Width)*bpp);c->Unmap(s.Get(),0);return b;}
void save(const std::filesystem::path& p,ID3D11Texture2D* t,unsigned bpp,const std::vector<unsigned char>& b){need(!std::filesystem::exists(p),"Refusing stage overwrite");D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);unsigned h[]={d.Width,d.Height,unsigned(d.Format),bpp};std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<char*>(h),16);out.write(reinterpret_cast<const char*>(b.data()),b.size());out.close();need(bool(out),"Stage write failed");}
void compare(ID3D11DeviceContext* c,ID3D11Texture2D* t,unsigned bpp,const std::filesystem::path& reference,const std::filesystem::path& saved){D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);auto b=readback(c,t,bpp);save(saved,t,bpp,b);auto expected=raw(reference,d.Width,d.Height,unsigned(d.Format),bpp);need(b==expected.bytes,("Stage not exact: "+reference.string()).c_str());std::cout<<"StageExact "<<saved.filename().string()<<std::endl;}
void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
void waitQueue(ID3D12Device*d,ID3D12CommandQueue*q){ComPtr<ID3D12Fence> f;HANDLE e=nullptr;if(FAILED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)))||!(e=CreateEventW(nullptr,FALSE,FALSE,nullptr))){std::cerr<<"STOP cannot track pending standalone GPU work\n";ExitProcess(10);}const auto signal=q->Signal(f.Get(),1),completion=f->SetEventOnCompletion(1,e);const auto s=SUCCEEDED(signal)&&SUCCEEDED(completion)?WaitForSingleObject(e,10000):WAIT_FAILED;CloseHandle(e);if(s!=WAIT_OBJECT_0||f->GetCompletedValue()==UINT64_MAX){std::cerr<<"STOP pending standalone GPU work; ExitProcess avoids pending-resource destruction\n";ExitProcess(10);}}
struct Session {ID3D12Device* device;bool initialized=false;~Session(){if(initialized)NVSDK_NGX_D3D12_Shutdown1(device);}};
struct Model {
    ID3D12Device* d;ID3D12CommandQueue*q;bool sr,reference;NVSDK_NGX_Parameter*p=nullptr;NVSDK_NGX_Handle*feature=nullptr;
    ac7::SharedTextureBridge bridge;ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D11Texture2D> output;
    ac7::FrameSubmission submission;
    Model(ID3D11Device*d11,ID3D12Device*d12,ID3D12CommandQueue*queue,bool isSr,bool ref=false):d(d12),q(queue),sr(isSr),reference(ref),bridge(d11,d12,queue,W,H),submission(bridge){try{
        ngx(NVSDK_NGX_D3D12_GetCapabilityParameters(&p),"GetCapabilityParameters");int available=0;ngx(p->Get(sr?NVSDK_NGX_Parameter_SuperSampling_Available:NVSDK_NGX_Parameter_DLSSNR_Available,&available),"FeatureAvailable");need(available==1,"Requested model unavailable");
        if(sr){p->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);p->Set(NVSDK_NGX_Parameter_Width,W);p->Set(NVSDK_NGX_Parameter_Height,H);p->Set(NVSDK_NGX_Parameter_OutWidth,W);p->Set(NVSDK_NGX_Parameter_OutHeight,H);p->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_DLAA));p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,0);}
        else {p->Set(NVSDK_NGX_Parameter_DLSSNR_Width,W);p->Set(NVSDK_NGX_Parameter_DLSSNR_Height,H);}
        ck(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));ck(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
        ngx(NVSDK_NGX_D3D12_CreateFeature(list.Get(),sr?NVSDK_NGX_Feature_SuperSampling:NVSDK_NGX_Feature_DLSSNR,p,&feature),reference?(sr?"CreateReferenceSR":"CreateReferenceNR"):(sr?"CreateSR":"CreateNR"));need(feature!=nullptr,"Null model feature");ck(list->Close());ID3D12CommandList* lists[]={list.Get()};q->ExecuteCommandLists(1,lists);waitQueue(d,q);
        output=texture(d11,{W,H,10,8,{}});
        }catch(...){if(feature)NVSDK_NGX_D3D12_ReleaseFeature(feature);if(p)NVSDK_NGX_D3D12_DestroyParameters(p);throw;}
    }
    ~Model(){if(bridge.busy()){std::cerr<<"STOP attempted destruction of pending standalone model\n";ExitProcess(10);}if(feature)std::cout<<(reference?(sr?"ReleaseReferenceSR":"ReleaseReferenceNR"):(sr?"ReleaseSR":"ReleaseNR"))<<" result="<<unsigned(NVSDK_NGX_D3D12_ReleaseFeature(feature))<<std::endl;if(p)std::cout<<"DestroyParameters result="<<unsigned(NVSDK_NGX_D3D12_DestroyParameters(p))<<std::endl;}
    void record(uint64_t frame,float dt,bool reset){
        need(!bridge.busy(),"Model resources still in flight");
        ck(alloc->Reset());ck(list->Reset(alloc.Get(),nullptr));for(unsigned i=0;i<5;i++)barrier(list.Get(),bridge.resource(i),D3D12_RESOURCE_STATE_COMMON,i==4?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        std::array<ID3D12Resource*,5> resources={bridge.resource(0),bridge.resource(1),bridge.resource(2),bridge.resource(4),bridge.resource(3)};
        configureCapturedEvaluation(p,resources,sr,reset,false,dt);
        std::cout<<(reference?(sr?"REFERENCE_SR_FRAME":"REFERENCE_NR_FRAME"):(sr?"SR_FRAME":"NR_FRAME"))<<" index="<<frame<<" Reset="<<reset<<" sharedFeature="<<feature<<" colorResource="<<resources[0]<<" outputResource="<<resources[3]<<std::endl;
        ngx(NVSDK_NGX_D3D12_EvaluateFeature(list.Get(),feature,p,nullptr),reference?(sr?"EvaluateReferenceSR":"EvaluateReferenceNR"):(sr?"EvaluateSR":"EvaluateNR"));
        for(unsigned i=0;i<5;i++)barrier(list.Get(),bridge.resource(i),i==4?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);ck(list->Close());
    }
    void begin(const std::array<ID3D11Texture2D*,4>& inputs,uint64_t frame,float dt,bool reset,std::chrono::milliseconds waitBudget=std::chrono::seconds(10)){
        need(waitBudget.count()>=0,"Negative wait budget");record(frame,dt,reset);
        try {need(submission.begin(frame,inputs,list.Get(),std::chrono::steady_clock::now()+waitBudget),"Model deadline elapsed before upload");}
        catch(const std::exception&e){std::cerr<<"STOP standalone submission failure: "<<e.what()<<std::endl;ExitProcess(10);}
    }
    bool beginUntil(const std::array<ID3D11Texture2D*,4>& inputs,uint64_t frame,float dt,bool reset,ac7::ChainSchedule::Clock::time_point deadline){
        need(!bridge.busy(),"Model resources still in flight");
        if(ac7::ChainSchedule::Clock::now()>=deadline)return false;
        record(frame,dt,reset);
        return submission.begin(frame,inputs,list.Get(),deadline);
    }
    ac7::SharedTextureBridge::Result poll(uint64_t frame){
        return submission.poll(frame,output.Get());
    }
    ac7::SharedTextureBridge::Result retire(uint64_t frame){
        try {const auto began=std::chrono::steady_clock::now();ac7::SharedTextureBridge::Result r;while((r=poll(frame))==ac7::SharedTextureBridge::Result::pending){if(std::chrono::steady_clock::now()-began>std::chrono::seconds(10))throw std::runtime_error("Model retirement watchdog");std::this_thread::sleep_for(std::chrono::milliseconds(1));}return r;}
        catch(const std::exception&e){std::cerr<<"STOP pending standalone chain: "<<e.what()<<std::endl;ExitProcess(10);}
    }
    ID3D11Texture2D* finish(uint64_t frame,bool publish=true){
        need(retire(frame)==(publish?ac7::SharedTextureBridge::Result::published:ac7::SharedTextureBridge::Result::discarded),"Unexpected model publication");return output.Get();
    }
    ID3D11Texture2D* run(const std::array<ID3D11Texture2D*,4>& inputs,uint64_t frame,float dt,bool reset){begin(inputs,frame,dt,reset);return finish(frame);}
};
// Local shader conversion only; no tone mapping, motion limiting or parameter jitter.
struct Convert {
    ComPtr<ID3D11ComputeShader> motionShader,colorShader;ComPtr<ID3D11Texture2D> motion,motionBits,color;
    Convert(ID3D11Device*d){const char* code=R"(
Texture2D<float4> source:register(t0);RWTexture2D<float4> color:register(u0);RWTexture2D<uint2> motion:register(u1);
[numthreads(8,8,1)]void Color(uint3 p:SV_DispatchThreadID){if(p.x<2560&&p.y<1440)color[p.xy]=float4(source.Load(int3(p.xy,0)).rgb,1);}
// Typed RG16F UAV stores may truncate. Round exact float-NDC * integer scale to half-even before store.
uint HalfMotion(float v,uint odd,int scaleExponent,bool negate){
 uint bits=asuint(v),sign=((bits>>16)&32768)^(negate?32768:0),e=(bits>>23)&255,m=bits&0x7fffff;
 if(e!=0)m|=0x800000;else e=1;if(m==0)return sign;
 uint product=m*odd;int top=firstbithigh(product),he=top+int(e)-135+scaleExponent;
 int shift=he>0?top-10:126-int(e)-scaleExponent;if(shift>31)return sign;
 uint q=product>>shift,remainder=product&((1u<<shift)-1),mid=1u<<(shift-1);if(remainder>mid||(remainder==mid&&(q&1)))q++;
 if(he<=0)return sign|q;if(q>=2048){q>>=1;he++;}return sign|(he>=31?31744:((uint(he)<<10)|(q&1023)));
}
[numthreads(8,8,1)]void Motion(uint3 p:SV_DispatchThreadID){if(p.x<2560&&p.y<1440){float2 v=source.Load(int3(p.xy,0)).xy;motion[p.xy]=uint2(HalfMotion(v.x,5,8,true),HalfMotion(v.y,45,4,false));}}
)";for(auto entry:{"Color","Motion"}){ComPtr<ID3DBlob>b,e;auto hr=D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS,0,&b,&e);if(FAILED(hr))throw std::runtime_error(e?std::string(static_cast<char*>(e->GetBufferPointer()),e->GetBufferSize()):"Conversion compile failed");ck(d->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,entry[0]=='C'?colorShader.GetAddressOf():motionShader.GetAddressOf()));}motion=texture(d,{W,H,34,4,{}});motionBits=texture(d,{W,H,33,4,{}});color=texture(d,{W,H,10,8,{}});}
    ID3D11Texture2D* run(ID3D11DeviceContext*c,ID3D11Texture2D*input,bool mv){ComPtr<ID3D11Device>d;c->GetDevice(&d);ComPtr<ID3D11ShaderResourceView>s;ComPtr<ID3D11UnorderedAccessView>u;ck(d->CreateShaderResourceView(input,nullptr,&s));D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R16G16_UINT;desc.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;ck(d->CreateUnorderedAccessView(mv?motionBits.Get():color.Get(),mv?&desc:nullptr,&u));ID3D11ShaderResourceView* srv=s.Get();ID3D11UnorderedAccessView* views[]={nullptr,nullptr};views[mv?1:0]=u.Get();c->CSSetShaderResources(0,1,&srv);c->CSSetUnorderedAccessViews(0,2,views,nullptr);c->CSSetShader(mv?motionShader.Get():colorShader.Get(),nullptr,0);c->Dispatch(W/8,H/8,1);srv=nullptr;views[0]=views[1]=nullptr;c->CSSetShaderResources(0,1,&srv);c->CSSetUnorderedAccessViews(0,2,views,nullptr);if(mv)c->CopyResource(motion.Get(),motionBits.Get());return mv?motion.Get():color.Get();}
};
// Independent fixed-layout driver prototype; the caller must stop and drain before destruction.
// Shared pass objects are exclusively leased while a frame is active; stop/drain before owner teardown.
class NonblockingChain {
public:
    using Clock=ac7::ChainSchedule::Clock;using Stage=ac7::ChainSchedule::Stage;
    enum class Result { idle,pending,published,discarded,unretirable };
    struct Input {std::array<ID3D11Texture2D*,4> textures;ac7::ClipToPrevious transform;float gain,dt;};
    NonblockingChain(Model& nr,Model& sr,ID3D11Device* device,ID3D11DeviceContext* context,ac7::HdrProxy& codec,ac7::GuideNormalizer& guides,Convert& conversion)
        :nr_(nr),sr_(sr),device_(device),context_(context),codec_(codec),guides_(guides),conversion_(conversion){
        need(context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE&&nr.d==sr.d&&nr.q==sr.q&&!nr.sr&&sr.sr,"Invalid driver context/models");
        ComPtr<ID3D11Device> owner;context->GetDevice(&owner);need(owner.Get()==device,"Driver context device mismatch");
        ComPtr<ID3D11Device5> d5;ck(device->QueryInterface(IID_PPV_ARGS(&d5)));ck(context->QueryInterface(IID_PPV_ARGS(&context4_)));
        ck(d5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&retirement_)));
        scene_=texture(device,{5120,H,26,4,{}});exposure_=texture(device,{1,1,41,4,{}});
    }
    NonblockingChain(const NonblockingChain&)=delete;NonblockingChain& operator=(const NonblockingChain&)=delete;
    bool start(uint64_t token,const Input& input,Clock::time_point deadline){
        if(stopping_)return false;
        if(schedule_.busy()){need(!schedule_.begin(token,deadline),"Busy schedule accepted frame");try{invalidate();}catch(const std::exception&e){reportFailure(e.what());}return false;}
        for(unsigned i=0;i<4;i++)validate(input.textures[i],i);
        need(std::isfinite(input.gain)&&input.gain>=1.f/65504&&input.gain<=65504&&std::isfinite(input.dt)&&input.dt>0,"Invalid driver exposure/time");
        for(const auto& row:input.transform)for(float value:row)need(std::isfinite(value),"Nonfinite driver transform");
        if(!schedule_.begin(token,deadline)){outputValid_=false;return false;}
        outputValid_=false;gain_=input.gain;dt_=input.dt;
        for(unsigned i=0;i<4;i++)sources_[i]=input.textures[i];
        // Later HDR restoration must use this frame's scene, not a reused engine target.
        try {
            context_->CopyResource(scene_.Get(),input.textures[0]);context_->CopyResource(exposure_.Get(),input.textures[3]);
            guides_.record(context_.Get(),input.textures[1],input.textures[2],{1280,0,W,H},input.transform);
            motion_=conversion_.run(context_.Get(),guides_.motion(),true);codec_.record(context_.Get(),scene_.Get(),{1280,0,W,H},gain_);seal();return true;
        }catch(const std::exception&e){reportFailure(e.what());return false;}
    }
    Result advance(){
        if(unretirable_)return Result::unretirable;
        try{return advanceStep();}
        catch(const std::exception&e){reportFailure(e.what());return unretirable_?Result::unretirable:(schedule_.busy()?Result::pending:Result::discarded);}
    }
    // Caller-reported processing failure: latch the first cause, hide output, stop new work.
    // Healthy submitted work may still retire; unknown/failed submissions remain quarantined.
    void reportFailure(const std::string& cause){
        if(error_.empty())error_=cause.empty()?"Unspecified chain processing failure":cause;
        stopping_=true;outputValid_=false;
        try {
            if((nr_.bridge.busy()&&!nr_.bridge.canPoll())||(sr_.bridge.busy()&&!sr_.bridge.canPoll())){unretirable_=true;return;}
            if(nr_.bridge.busy())nr_.bridge.abandon();
            if(sr_.bridge.busy())sr_.bridge.abandon();
            // beginUntil may have submitted before the policy stage was advanced.
            if(schedule_.stage()==Stage::prepare&&nr_.bridge.busy())schedule_.nrSubmitted();
            if(schedule_.stage()==Stage::restore&&sr_.bridge.busy())schedule_.srSubmitted();
            if(!nr_.bridge.busy()&&!sr_.bridge.busy()){
                if(schedule_.stage()==Stage::nr)schedule_.nrRetired(false);
                else if(schedule_.stage()==Stage::sr)schedule_.srRetired(false);
                else schedule_.drop();
            }
            seal();
        }catch(...){unretirable_=true;}
    }
    const std::string& error()const{return error_;}
    bool requiresDeviceTeardown()const{return unretirable_;}
private:
    Result advanceStep(){
        const auto stage=schedule_.stage();
        if(stage==Stage::idle)return Result::idle;
        const auto token=schedule_.frame();
        if(stage==Stage::prepare){
            if(stopping_||!schedule_.ready(stage)){schedule_.drop();seal();return Result::discarded;}
            if(!nr_.beginUntil({codec_.prepared(),guides_.depth(),motion_,exposure_.Get()},token,dt_,schedule_.reset(),schedule_.deadline())){schedule_.drop();seal();return Result::discarded;}
            schedule_.nrSubmitted();seal();return Result::pending;
        }
        if(stage==Stage::nr){
            if(stopping_||!schedule_.ready(stage))nr_.bridge.abandon();
            const auto result=nr_.poll(token);if(result==ac7::SharedTextureBridge::Result::pending)return Result::pending;
            const bool usable=schedule_.nrRetired(result==ac7::SharedTextureBridge::Result::published&&!stopping_);seal();return usable?Result::pending:Result::discarded;
        }
        if(stage==Stage::restore){
            if(stopping_||!schedule_.ready(stage)){schedule_.drop();seal();return Result::discarded;}
            codec_.record(context_.Get(),scene_.Get(),{1280,0,W,H},gain_,nr_.output.Get());auto color=conversion_.run(context_.Get(),codec_.restored(),false);
            if(!sr_.beginUntil({color,guides_.depth(),motion_,exposure_.Get()},token,dt_,schedule_.reset(),schedule_.deadline())){schedule_.drop();seal();return Result::discarded;}
            schedule_.srSubmitted();seal();return Result::pending;
        }
        if(stopping_||!schedule_.ready(stage))sr_.bridge.abandon();
        const auto result=sr_.poll(token);if(result==ac7::SharedTextureBridge::Result::pending)return Result::pending;
        outputValid_=schedule_.srRetired(result==ac7::SharedTextureBridge::Result::published&&!stopping_);seal();return outputValid_?Result::published:Result::discarded;
    }
public:
    void stop(){if(!stopping_){stopping_=true;outputValid_=false;try{invalidate();}catch(const std::exception&e){reportFailure(e.what());}}}
    bool drained(){
        if(unretirable_||!stopping_||schedule_.busy()||nr_.bridge.busy()||sr_.bridge.busy())return false;
        try {
            ck(device_->GetDeviceRemovedReason());ck(nr_.d->GetDeviceRemovedReason());
            const auto completed=retirement_->GetCompletedValue();need(completed!=UINT64_MAX,"Driver retirement fence reports removal");return completed>=retirementValue_;
        }catch(const std::exception&e){reportFailure(e.what());unretirable_=true;return false;}
    }
    Stage stage()const{return schedule_.stage();}
    bool reset()const{return schedule_.reset();}
    ID3D11Texture2D* output()const{return outputValid_&&!stopping_?sr_.output.Get():nullptr;}
private:
    void validate(ID3D11Texture2D* image,unsigned i)const{
        need(image!=nullptr,"Missing driver texture");ComPtr<ID3D11Device> owner;image->GetDevice(&owner);need(owner.Get()==device_.Get(),"Driver texture device mismatch");
        D3D11_TEXTURE2D_DESC d{};image->GetDesc(&d);const DXGI_FORMAT formats[]={DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_R16G16_UNORM,DXGI_FORMAT_R32_FLOAT};
        need(d.Width==(i==3?1:5120)&&d.Height==(i==3?1:H)&&d.Format==formats[i]&&d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1,"Driver texture layout mismatch");
    }
    void invalidate(){
        const auto stage=schedule_.stage();
        if(stage==Stage::nr)nr_.bridge.abandon();else if(stage==Stage::sr)sr_.bridge.abandon();else schedule_.drop();seal();
    }
    void seal(){need(retirementValue_<UINT64_MAX-1,"Driver retirement counter exhausted");ck(context4_->Signal(retirement_.Get(),++retirementValue_));context_->Flush();}
    Model& nr_;Model& sr_;ComPtr<ID3D11Device> device_;ComPtr<ID3D11DeviceContext> context_;ComPtr<ID3D11DeviceContext4> context4_;
    ac7::HdrProxy& codec_;ac7::GuideNormalizer& guides_;Convert& conversion_;ac7::ChainSchedule schedule_;
    ComPtr<ID3D11Texture2D> scene_,exposure_;std::array<ComPtr<ID3D11Texture2D>,4> sources_;ID3D11Texture2D* motion_=nullptr;
    ComPtr<ID3D11Fence> retirement_;uint64_t retirementValue_=0;float gain_=1,dt_=0;bool stopping_=false,outputValid_=false,unretirable_=false;std::string error_;
};
// Own every per-session dependency in destruction order; NGX/device lifetime remains outside.
// The standalone caller must retain this owner while tryClose reports pending/unretirable.
class FixedRuntime {
    struct Resources {
        ComPtr<ID3D11Device> d11;ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> queue;
        ac7::HdrProxy codec;ac7::GuideNormalizer guides;Convert conversion;
        Model nr,sr;NonblockingChain driver;
        Resources(ID3D11Device*a,ID3D11DeviceContext*c,ID3D12Device*b,ID3D12CommandQueue*q)
            :d11(a),context(c),d12(b),queue(q),codec(a,W,H),guides(a,W,H),conversion(a),nr(a,b,q,false),sr(a,b,q,true),driver(nr,sr,a,c,codec,guides,conversion){}
    };
public:
    FixedRuntime(ID3D11Device*a,ID3D11DeviceContext*c,ID3D12Device*b,ID3D12CommandQueue*q):resources_(std::make_unique<Resources>(a,c,b,q)){}
    FixedRuntime(const FixedRuntime&)=delete;FixedRuntime& operator=(const FixedRuntime&)=delete;
    ~FixedRuntime(){if(resources_){std::cerr<<"STOP standalone owner destroyed before explicit close\n";ExitProcess(10);}}
    NonblockingChain& driver(){need(bool(resources_),"Runtime already closed");return resources_->driver;}
    bool tryClose(){
        if(!resources_)return true;
        auto& d=resources_->driver;d.stop();
        if(d.requiresDeviceTeardown())return false;
        if(d.stage()!=NonblockingChain::Stage::idle)d.advance();
        if(!d.drained())return false;
        resources_.reset();return true;
    }
    Model& nr(){need(bool(resources_),"Runtime already closed");return resources_->nr;}
    Model& sr(){need(bool(resources_),"Runtime already closed");return resources_->sr;}
    ID3D11Texture2D* proxy(){need(bool(resources_),"Runtime already closed");return resources_->codec.prepared();}
    ID3D11Texture2D* depth(){need(bool(resources_),"Runtime already closed");return resources_->guides.depth();}
    ID3D11Texture2D* motion(){need(bool(resources_),"Runtime already closed");return resources_->conversion.motion.Get();}
    ID3D11Texture2D* srInput(){need(bool(resources_),"Runtime already closed");return resources_->conversion.color.Get();}
    bool closed()const{return !resources_;}
private:
    std::unique_ptr<Resources> resources_;
};
void debugCheck(ID3D11Device*d11,ID3D12Device*d12){ComPtr<ID3D11InfoQueue>a;ComPtr<ID3D12InfoQueue>b;ck(d11->QueryInterface(IID_PPV_ARGS(&a)));ck(d12->QueryInterface(IID_PPV_ARGS(&b)));unsigned errors=0;for(UINT64 i=0;i<a->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;ck(a->GetMessage(i,nullptr,&n));std::vector<unsigned char>v(n);auto*m=reinterpret_cast<D3D11_MESSAGE*>(v.data());ck(a->GetMessage(i,m,&n));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}for(UINT64 i=0;i<b->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;ck(b->GetMessage(i,nullptr,&n));std::vector<unsigned char>v(n);auto*m=reinterpret_cast<D3D12_MESSAGE*>(v.data());ck(b->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}need(errors==0,"D3D debug errors");ck(d11->GetDeviceRemovedReason());ck(d12->GetDeviceRemovedReason());std::cout<<"BothApiDebugErrors=0"<<std::endl;}
struct Gate {ComPtr<ID3D12Fence> value;~Gate(){if(value)value->Signal(1);}};
void expectBusy(Model& model,unsigned frame,float dt){
    bool refused=false;try{model.begin({model.submission.inputs()[0].Get(),model.submission.inputs()[1].Get(),model.submission.inputs()[2].Get(),model.submission.inputs()[3].Get()},frame,dt,true);}catch(const std::exception&e){refused=std::string(e.what())=="Model resources still in flight";}need(refused,"Busy model allowed allocator/resource reuse");
}
void preserveOriginal(ID3D11DeviceContext*c,ID3D11Texture2D*scene,const std::filesystem::path&source,const std::filesystem::path&dest,unsigned frame,const wchar_t* name=L"fallback-scene.raw"){compare(c,scene,4,source/(L"frame"+std::to_wstring(frame)+L"-scene.raw"),dest/name);}
void waitExpired(Model& model,unsigned frame){
    const auto began=std::chrono::steady_clock::now();while(!model.submission.expired()){need(model.poll(frame)==ac7::SharedTextureBridge::Result::pending,"Gate did not keep model pending");if(std::chrono::steady_clock::now()-began>std::chrono::seconds(1)){std::cerr<<"STOP deterministic deadline test failed\n";ExitProcess(10);}if(!model.submission.expired())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    need(model.bridge.busy(),"Expired model retired before gate release");for(const auto&input:model.submission.inputs())need(bool(input),"Expired model released input before retirement");
    std::cout<<"WAIT_EXPIRED role="<<(model.sr?"SR":"NR")<<" frame="<<frame<<" budgetMs=2 pending=1 heldInputs=4"<<std::endl;
}
struct Frame {std::vector<unsigned char>view;float gain,dt;unsigned counter;};
void selfTestSchedule(){
    using S=ac7::ChainSchedule;using Stage=S::Stage;S s;const auto t=S::Clock::time_point{}+std::chrono::seconds(10),d=t+std::chrono::seconds(1);
    auto refuses=[&](auto action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}need(rejected,"Schedule accepted invalid phase/token");};
    need(s.begin(0,d,t)&&s.reset(),"First frame must reset");s.nrSubmitted();refuses([&]{s.drop();});
    need(s.nrRetired(true,t)&&s.deadline()==d,"NR changed frame deadline");s.srSubmitted();need(s.srRetired(true,t),"Healthy chain rejected");
    need(s.begin(1,d,t)&&!s.reset(),"Adjacent frame lost history");s.nrSubmitted();need(!s.begin(2,d,t),"Busy frame accepted");
    need(s.frame()==1&&s.deadline()==d,"Busy input replaced active identity");need(!s.nrRetired(true,t),"Newer frame failed to invalidate old chain");
    need(s.begin(3,d,t)&&s.reset(),"Busy gap failed to reset both models");s.nrSubmitted();need(s.nrRetired(true,t),"NR recovery failed");
    need(!s.ready(Stage::restore,d),"Equality at deadline accepted");s.drop();
    need(!s.begin(4,t,t)&&!s.busy(),"Pre-expired frame entered chain");
    need(s.begin(5,d,t)&&s.reset(),"Pre-expired gap lost reset");s.nrSubmitted();need(s.nrRetired(true,t),"NR stage failed");s.srSubmitted();need(!s.srRetired(true,d),"Completed SR published after deadline");
    need(s.begin(6,d,t)&&s.reset(),"SR failure failed to reset");s.drop();
    need(s.begin(7,d,t),"Drop failed to release policy");s.nrSubmitted();need(!s.nrRetired(false,t),"Discarded NR advanced to restore");
    need(s.begin(8,d,t)&&s.reset(),"NR failure failed to reset");s.nrSubmitted();need(s.nrRetired(true,t),"NR stage failed");s.srSubmitted();need(s.srRetired(true,t),"Recovery failed");
    need(s.begin(10,d,t)&&s.reset(),"Skipped token retained history");s.drop();refuses([&]{s.begin(10,d,t);});refuses([&]{s.srSubmitted();});
    std::cout<<"PASS chain policy: shared absolute deadline; boundary expiry; busy/newer invalidation; no pending drop; gap/NR/SR failures reset; monotonic tokens; no GPU/NGX"<<std::endl;
}
void scheduledChain(Model& nr,Model& sr,ID3D11Device* d11,ID3D11DeviceContext* context,
    ac7::HdrProxy& codec,ac7::GuideNormalizer& normalizer,Convert& conversion,
    const std::array<Frame,8>& frames,const std::filesystem::path& source,const std::filesystem::path& baseline,const std::filesystem::path& output){
    using S=ac7::ChainSchedule;using Stage=S::Stage;S schedule;Gate gate;bool pending=false;
    ComPtr<ID3D11Texture2D> heldScene;std::vector<unsigned char> before;std::unique_ptr<Model> refNR,refSR;
    unsigned accepted=0,dropped=0,referencePairs=0;
    std::cout<<"SCHEDULE_CLOCK numerator="<<S::Clock::period::num<<" denominator="<<S::Clock::period::den<<" origin=native-gpu-input-ready"<<std::endl;
    for(unsigned f=0;f<8;f++){
        const auto prefix=L"frame"+std::to_wstring(f);const auto dest=output/prefix;std::filesystem::create_directory(dest);
        auto scene=texture(d11,raw(source/(prefix+L"-scene.raw"),5120,H,26,4));
        if(pending){
            need(f==7&&!schedule.begin(f,S::Clock::now()+std::chrono::seconds(5)),"Busy chain input accepted");
            need(schedule.frame()==6,"Busy frame changed active token");preserveOriginal(context,scene.Get(),source,dest,f);++dropped;
            std::cout<<"SCHEDULE_FRAME index="<<f<<" status=busy-original"<<std::endl;
            ck(gate.value->Signal(1));need(sr.retire(6)==ac7::SharedTextureBridge::Result::discarded,"Late SR published");need(!schedule.srRetired(false),"Late chain published");
            const auto after=readback(context,sr.output.Get(),8);need(after==before,"Late SR overwrote destination");
            const auto retired=output/L"frame6";save(retired/L"destination-after.raw",sr.output.Get(),8,after);preserveOriginal(context,heldScene.Get(),source,retired,6,L"retired-scene.raw");heldScene.Reset();pending=false;
            std::cout<<"SCHEDULE_RETIRED source=6 sameToken=1 destinationExact=1"<<std::endl;continue;
        }
        auto depth=texture(d11,raw(source/(prefix+L"-depth.raw"),5120,H,19,8)),velocity=texture(d11,raw(source/(prefix+L"-velocity.raw"),5120,H,35,4));
        Raw exp{1,1,41,4,std::vector<unsigned char>(4)};std::memcpy(exp.bytes.data(),&frames[f].gain,4);auto exposure=texture(d11,exp);
        if(f==6){before=readback(context,sr.output.Get(),8);save(dest/L"destination-before.raw",sr.output.Get(),8,before);}
        // Native GPU fixtures are ready; no diagnostic Map/file work until this frame ends.
        const auto start=S::Clock::now(),deadline=start+(f==1?std::chrono::seconds(0):(f==4||f==6?std::chrono::seconds(1):std::chrono::seconds(5)));
        const auto ticks=deadline.time_since_epoch().count();
        std::cout<<"SCHEDULE_BUDGET index="<<f<<" start="<<start.time_since_epoch().count()<<" deadline="<<ticks<<std::endl;
        if(!schedule.begin(f,deadline)){
            need(f==1,"Unexpected input expiry");preserveOriginal(context,scene.Get(),source,dest,f);++dropped;
            std::cout<<"SCHEDULE_FRAME index="<<f<<" status=expired-before-input"<<std::endl;continue;
        }
        const bool reset=schedule.reset();ac7::ClipToPrevious transform;std::memcpy(transform.data(),frames[f].view.data()+110*16,64);
        normalizer.record(context,depth.Get(),velocity.Get(),{1280,0,W,H},transform);auto motion=conversion.run(context,normalizer.motion(),true);codec.record(context,scene.Get(),{1280,0,W,H},frames[f].gain);
        const std::array<ID3D11Texture2D*,4> nrInputs={codec.prepared(),normalizer.depth(),motion,exposure.Get()};
        need(schedule.ready(Stage::prepare),"Unexpected preparation expiry");
        std::cout<<"SCHEDULE_STAGE index="<<f<<" role=NR deadline="<<ticks<<" Reset="<<reset<<std::endl;
        need(nr.beginUntil(nrInputs,f,frames[f].dt,reset,schedule.deadline()),"Unexpected NR recording expiry");schedule.nrSubmitted();
        need(schedule.nrRetired(nr.retire(f)==ac7::SharedTextureBridge::Result::published),"Unexpected NR expiry");
        codec.record(context,scene.Get(),{1280,0,W,H},frames[f].gain,nr.output.Get());auto srColor=conversion.run(context,codec.restored(),false);
        if(f==4)while(S::Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!schedule.ready(Stage::restore)){
            need(f==4,"Unexpected HDR restoration expiry");schedule.drop();preserveOriginal(context,scene.Get(),source,dest,f);save(dest/L"nr-output.raw",nr.output.Get(),8,readback(context,nr.output.Get(),8));++dropped;
            std::cout<<"SCHEDULE_FRAME index="<<f<<" status=expired-before-SR"<<std::endl;continue;
        }
        if(f==6){ck(nr.d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(gate.value.ReleaseAndGetAddressOf())));ck(nr.q->Wait(gate.value.Get(),1));}
        std::cout<<"SCHEDULE_STAGE index="<<f<<" role=SR deadline="<<ticks<<" Reset="<<reset<<std::endl;
        need(sr.beginUntil({srColor,normalizer.depth(),motion,exposure.Get()},f,frames[f].dt,reset,schedule.deadline()),"Unexpected SR recording expiry");schedule.srSubmitted();
        if(f==6){
            while(!sr.submission.expired()){need(sr.poll(f)==ac7::SharedTextureBridge::Result::pending,"Gated SR completed early");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            need(!schedule.ready(Stage::sr)&&sr.bridge.busy(),"Expired SR lost pending state");heldScene=scene;pending=true;
            preserveOriginal(context,scene.Get(),source,dest,f);++dropped;std::cout<<"SCHEDULE_FRAME index="<<f<<" status=expired-SR-pending"<<std::endl;continue;
        }
        need(schedule.srRetired(sr.retire(f)==ac7::SharedTextureBridge::Result::published),"Unexpected SR expiry");
        const auto end=S::Clock::now();std::cout<<"SCHEDULE_FRAME index="<<f<<" status=published Reset="<<reset<<" end="<<end.time_since_epoch().count()<<" deadline="<<ticks<<std::endl;++accepted;
        // All reference readback, files and extra model controls are outside the active frame.
        if(f==0){
            compare(context,codec.prepared(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"proxy.raw");compare(context,normalizer.depth(),4,baseline/L"nr-input"/prefix/L"depth.raw",dest/L"depth.raw");compare(context,motion,4,baseline/L"nr-input"/prefix/L"motion.raw",dest/L"motion.raw");
            compare(context,nr.output.Get(),8,baseline/L"nr-output"/prefix/L"nr-output.raw",dest/L"nr-output.raw");compare(context,codec.prepared(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"restore-proxy.raw");compare(context,srColor,8,baseline/L"sr-input"/prefix/L"color.raw",dest/L"sr-input.raw");compare(context,sr.output.Get(),8,baseline/L"history"/prefix/L"sr-output.raw",dest/L"sr-output.raw");
        }else{
            save(dest/L"nr-output.raw",nr.output.Get(),8,readback(context,nr.output.Get(),8));save(dest/L"sr-input.raw",srColor,8,readback(context,srColor,8));save(dest/L"sr-output.raw",sr.output.Get(),8,readback(context,sr.output.Get(),8));
            if(reset){refSR.reset();refNR.reset();refNR=std::make_unique<Model>(d11,nr.d,nr.q,false,true);refSR=std::make_unique<Model>(d11,nr.d,nr.q,true,true);}
            need(refNR&&refSR,"Missing scheduled reference features");auto freshNR=refNR->run(nrInputs,f,frames[f].dt,reset);
            need(readback(context,freshNR,8)==readback(context,nr.output.Get(),8),"Scheduled NR history mismatch");save(dest/L"reference-nr.raw",freshNR,8,readback(context,freshNR,8));
            codec.record(context,scene.Get(),{1280,0,W,H},frames[f].gain,freshNR);auto freshColor=conversion.run(context,codec.restored(),false);auto freshSR=refSR->run({freshColor,normalizer.depth(),motion,exposure.Get()},f,frames[f].dt,reset);
            need(readback(context,freshSR,8)==readback(context,sr.output.Get(),8),"Scheduled SR history mismatch");save(dest/L"reference-sr.raw",freshSR,8,readback(context,freshSR,8));++referencePairs;
            std::cout<<"SCHEDULE_REFERENCE index="<<f<<" NRExact=1 SRExact=1 Reset="<<reset<<std::endl;
        }
        debugCheck(d11,nr.d);
    }
    need(!pending&&!schedule.busy()&&accepted==4&&dropped==4&&referencePairs==3,"Scheduled coverage incomplete");debugCheck(d11,nr.d);
}
void driverTest(Model& nr,Model& sr,ID3D11Device* d11,ID3D11DeviceContext* context,
    ac7::HdrProxy& codec,ac7::GuideNormalizer& normalizer,Convert& conversion,
    const std::array<Frame,8>& frames,const std::filesystem::path& source,const std::filesystem::path& baseline,const std::filesystem::path& output,bool errorMode=false){
    using D=NonblockingChain;using Stage=D::Stage;constexpr uint64_t Base=uint64_t(1)<<32;
    // Standalone failure protection only; never destroy private snapshots while GPU work is pending.
    struct Owner {std::unique_ptr<D> p;~Owner(){if(p){try{if(p->drained())return;}catch(...){}std::cerr<<"STOP standalone driver destroyed before retirement\n";ExitProcess(10);}}} owner;
    auto create=[&]{owner.p=std::make_unique<D>(nr,sr,d11,context,codec,normalizer,conversion);};create();
    auto injectError=[&](unsigned f,const char* role){
        const std::string first=std::string("Injected processing failure at ")+role;
        owner.p->reportFailure(first);owner.p->reportFailure("Secondary error must not replace first cause");
        need(owner.p->error()==first&&!owner.p->requiresDeviceTeardown()&&!owner.p->output(),"Recoverable failure lost cause or quarantined healthy work");
        need(!owner.p->start(Base+f+100,{},D::Clock::now()+std::chrono::seconds(5)),"Failed driver accepted new frame");
        std::cout<<"DRIVER_ERROR index="<<f<<" role="<<role<<" firstCausePreserved=1 newFramesRefused=1 outputHidden=1 teardownRequired=0"<<std::endl;
    };
    unsigned calls=0,published=0,references=0,overwrites=0;Gate gate;std::vector<unsigned char> before;std::unique_ptr<Model> refNR,refSR;
    auto tick=[&]{++calls;return owner.p->advance();};
    auto drain=[&]{owner.p->stop();const auto limit=D::Clock::now()+std::chrono::seconds(10);while(!owner.p->drained()){tick();if(D::Clock::now()>=limit){std::cerr<<"STOP standalone driver drain watchdog\n";ExitProcess(10);}std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    auto reach=[&](Stage stage){const auto limit=D::Clock::now()+std::chrono::seconds(10);while(owner.p->stage()!=stage){need(tick()==D::Result::pending,"Unexpected driver terminal stage");if(D::Clock::now()>=limit)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    try {
    for(unsigned f=0;f<8;f++){
        const auto prefix=L"frame"+std::to_wstring(f);const auto dest=output/prefix;std::filesystem::create_directory(dest);
        auto scene=texture(d11,raw(source/(prefix+L"-scene.raw"),5120,H,26,4));const uint64_t token=Base+f;
        if(f==2){
            need(!owner.p->start(token,{},D::Clock::now()+std::chrono::seconds(5)),"Stopped driver accepted new frame");preserveOriginal(context,scene.Get(),source,dest,f);
            ck(gate.value->Signal(1));drain();const auto after=readback(context,nr.output.Get(),8);need(after==before,"Stopped NR changed destination");save(output/L"frame1"/L"destination-after.raw",nr.output.Get(),8,after);
            std::cout<<"DRIVER_RETIRED index=1 role=NR d3d11Complete=1 modelsIdle=1 destinationExact=1"<<std::endl;owner.p.reset();create();
            std::cout<<"DRIVER_FRAME index=2 token="<<token<<" status=stopped-original"<<std::endl;continue;
        }
        auto depth=texture(d11,raw(source/(prefix+L"-depth.raw"),5120,H,19,8)),velocity=texture(d11,raw(source/(prefix+L"-velocity.raw"),5120,H,35,4));Raw exp{1,1,41,4,std::vector<unsigned char>(4)};std::memcpy(exp.bytes.data(),&frames[f].gain,4);auto exposure=texture(d11,exp);
        ac7::ClipToPrevious transform;std::memcpy(transform.data(),frames[f].view.data()+110*16,64);D::Input input{{scene.Get(),depth.Get(),velocity.Get(),exposure.Get()},transform,frames[f].gain,frames[f].dt};
        if(f==5){
            auto wrong=texture(d11,{1,1,26,4,std::vector<unsigned char>(4)});auto bad=input;bad.textures[0]=wrong.Get();bool refused=false;
            try{owner.p->start(token,bad,D::Clock::now()+std::chrono::seconds(5));}catch(const std::exception&e){refused=std::string(e.what())=="Driver texture layout mismatch";}
            need(refused&&owner.p->stage()==Stage::idle,"Unsupported resize was not rejected before work");if(errorMode)injectError(f,"idle");preserveOriginal(context,scene.Get(),source,dest,f);drain();need(owner.p->drained(),"Resize stop did not drain");owner.p.reset();create();
            std::cout<<"DRIVER_FRAME index=5 token="<<token<<" status=resize-refused-original"<<std::endl;continue;
        }
        if(f==1||f==7){auto image=f==1?nr.output.Get():sr.output.Get();before=readback(context,image,8);save(dest/L"destination-before.raw",image,8,before);}
        need(owner.p->start(token,input,D::Clock::now()+std::chrono::seconds(5)),"Driver rejected valid input");const bool reset=owner.p->reset();
        if(f==1||f==7){
            if(f==7)reach(Stage::restore);
            ck(nr.d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(gate.value.ReleaseAndGetAddressOf())));ck(nr.q->Wait(gate.value.Get(),1));
            need(tick()==D::Result::pending&&owner.p->stage()==(f==1?Stage::nr:Stage::sr),"Gate failed to keep requested stage in flight");
            if(errorMode)injectError(f,f==1?"NR":"SR");
            owner.p->stop();owner.p->stop();need(!owner.p->drained()&&!owner.p->output(),"Stopped pending driver advertised safe output/destruction");
            for(unsigned i=0;i<16;i++)need(tick()==D::Result::pending&&!owner.p->drained(),"Pending poll waited for or retired blocked GPU work");
            preserveOriginal(context,scene.Get(),source,dest,f);std::cout<<"DRIVER_STOP index="<<f<<" role="<<(f==1?"NR":"SR")<<" pendingCalls=16 drained=0 outputHidden=1"<<std::endl;
            if(f==7){
                need(!owner.p->start(token+1,{},D::Clock::now()+std::chrono::seconds(5)),"Stopped SR driver accepted input");ck(gate.value->Signal(1));drain();const auto after=readback(context,sr.output.Get(),8);need(after==before,"Stopped SR changed destination");save(dest/L"destination-after.raw",sr.output.Get(),8,after);
                std::cout<<"DRIVER_RETIRED index=7 role=SR d3d11Complete=1 modelsIdle=1 destinationExact=1"<<std::endl;
            }
            std::cout<<"DRIVER_FRAME index="<<f<<" token="<<token<<" status="<<(f==1?"NR-stop-pending":"SR-stop-retired")<<std::endl;continue;
        }
        const unsigned bpp[]={4,8,4,4};
        for(unsigned i=0;i<4;i++){D3D11_TEXTURE2D_DESC d{};input.textures[i]->GetDesc(&d);std::vector<unsigned char> bytes(size_t(d.Width)*d.Height*bpp[i],0x5a);context->UpdateSubresource(input.textures[i],0,nullptr,bytes.data(),d.Width*bpp[i],0);}
        const auto limit=D::Clock::now()+std::chrono::seconds(10);while(true){const auto r=tick();if(r==D::Result::published)break;need(r==D::Result::pending,"Driver unexpectedly discarded valid frame");if(D::Clock::now()>=limit)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        need(owner.p->output()==sr.output.Get(),"Published output unavailable");++published;
        std::cout<<"DRIVER_FRAME index="<<f<<" token="<<token<<" status=published Reset="<<reset<<std::endl;
        size_t overwrittenBytes=0,ignoredPaddingDifferences=0;for(unsigned i=0;i<4;i++){
            const auto changed=readback(context,input.textures[i],bpp[i]);
            for(size_t byte=0;byte<changed.size();byte++){
                // Native depth X24 padding is undefined; only depth32/stencil8 are meaningful.
                if(i==1&&byte%8>=5){if(changed[byte]!=0x5a)++ignoredPaddingDifferences;continue;}
                need(changed[byte]==0x5a,"Meaningful source overwrite control failed");++overwrittenBytes;
            }
        }++overwrites;
        std::cout<<"DRIVER_OVERWRITE index="<<f<<" inputs=4 verifiedBytes="<<overwrittenBytes<<" depthPaddingIgnored=1 depthPaddingDifferences="<<ignoredPaddingDifferences<<std::endl;
        if(f==0){
            compare(context,codec.prepared(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"proxy.raw");compare(context,normalizer.depth(),4,baseline/L"nr-input"/prefix/L"depth.raw",dest/L"depth.raw");compare(context,conversion.motion.Get(),4,baseline/L"nr-input"/prefix/L"motion.raw",dest/L"motion.raw");compare(context,nr.output.Get(),8,baseline/L"nr-output"/prefix/L"nr-output.raw",dest/L"nr-output.raw");compare(context,codec.prepared(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"restore-proxy.raw");compare(context,conversion.color.Get(),8,baseline/L"sr-input"/prefix/L"color.raw",dest/L"sr-input.raw");compare(context,sr.output.Get(),8,baseline/L"history"/prefix/L"sr-output.raw",dest/L"sr-output.raw");
        }else{
            save(dest/L"nr-output.raw",nr.output.Get(),8,readback(context,nr.output.Get(),8));save(dest/L"sr-input.raw",conversion.color.Get(),8,readback(context,conversion.color.Get(),8));save(dest/L"sr-output.raw",sr.output.Get(),8,readback(context,sr.output.Get(),8));
            if(reset){refSR.reset();refNR.reset();refNR=std::make_unique<Model>(d11,nr.d,nr.q,false,true);refSR=std::make_unique<Model>(d11,nr.d,nr.q,true,true);}
            // Independently rebuild reference inputs from preserved originals, never the overwritten sources.
            auto originalScene=texture(d11,raw(source/(prefix+L"-scene.raw"),5120,H,26,4)),originalDepth=texture(d11,raw(source/(prefix+L"-depth.raw"),5120,H,19,8)),originalVelocity=texture(d11,raw(source/(prefix+L"-velocity.raw"),5120,H,35,4)),originalExposure=texture(d11,exp);
            normalizer.record(context,originalDepth.Get(),originalVelocity.Get(),{1280,0,W,H},transform);auto motion=conversion.run(context,normalizer.motion(),true);codec.record(context,originalScene.Get(),{1280,0,W,H},frames[f].gain);
            auto freshNR=refNR->run({codec.prepared(),normalizer.depth(),motion,originalExposure.Get()},token,frames[f].dt,reset);need(readback(context,freshNR,8)==readback(context,nr.output.Get(),8),"Driver NR differs from pristine reference");save(dest/L"reference-nr.raw",freshNR,8,readback(context,freshNR,8));
            codec.record(context,originalScene.Get(),{1280,0,W,H},frames[f].gain,freshNR);auto color=conversion.run(context,codec.restored(),false);auto freshSR=refSR->run({color,normalizer.depth(),motion,originalExposure.Get()},token,frames[f].dt,reset);need(readback(context,freshSR,8)==readback(context,sr.output.Get(),8),"Driver SR differs from pristine reference");save(dest/L"reference-sr.raw",freshSR,8,readback(context,freshSR,8));++references;
            std::cout<<"DRIVER_REFERENCE index="<<f<<" NRExact=1 SRExact=1 Reset="<<reset<<std::endl;
        }
        debugCheck(d11,nr.d);
    }
    need(published==4&&references==3&&overwrites==4&&owner.p->drained(),"Driver coverage incomplete");owner.p.reset();debugCheck(d11,nr.d);
    std::cout<<"DRIVER_COMPLETE callbackCalls="<<calls<<" explicitWaitsInDriver=0 sessions=3 unsupportedResizeRefused=1 tokenBase="<<Base<<std::endl;
    }catch(const std::exception&e){std::cerr<<"FAIL standalone driver: "<<e.what()<<std::endl;throw;}
}
void ownerTest(ID3D11Device* d11,ID3D11DeviceContext* context,ID3D12Device* d12,ID3D12CommandQueue* queue,
    const std::array<Frame,8>& frames,const std::filesystem::path& source,const std::filesystem::path& baseline,
    const std::filesystem::path& output,const std::filesystem::path& nrPath,const std::filesystem::path& srPath){
    using D=NonblockingChain;using Stage=D::Stage;constexpr uint64_t Base=(uint64_t(1)<<32)+100;
    // A context mismatch fails after model construction; completed initialization unwinds safely.
    ComPtr<ID3D11Device> warp;ComPtr<ID3D11DeviceContext> warpContext;ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&warp,nullptr,&warpContext));
    bool refused=false;try{FixedRuntime invalid(d11,warpContext.Get(),d12,queue);while(!invalid.tryClose())std::this_thread::yield();}catch(const std::exception&e){refused=std::string(e.what())=="Driver context device mismatch";}
    need(refused,"Partial runtime initialization did not reject mismatched context");std::cout<<"OWNER_PARTIAL_INIT contextMismatchRefused=1 evaluated=0"<<std::endl;
    std::unique_ptr<FixedRuntime> runtime;Gate gate;unsigned generation=0,published=0,comparisons=0;ComPtr<ID3D11Texture2D> retainedDestination;std::vector<unsigned char> before;
    const auto driverBaseline=baseline.parent_path()/L"nonblocking-chain-v1"/L"run-v2";
    auto create=[&]{runtime=std::make_unique<FixedRuntime>(d11,context,d12,queue);++generation;
        for(const auto& pair:{std::pair{L"nvngx_dlssnr.dll",nrPath},std::pair{L"nvngx_dlss.dll",srPath}}){HMODULE module=GetModuleHandleW(pair.first);need(module!=nullptr,"Owner runtime missing module");wchar_t p[32768]{};need(GetModuleFileNameW(module,p,32768)>0&&std::filesystem::equivalent(p,pair.second/pair.first),"Owner loaded wrong feature runtime");}
        std::cout<<"OWNER_CREATED generation="<<generation<<std::endl;};
    auto close=[&]{const auto limit=D::Clock::now()+std::chrono::seconds(10);while(!runtime->tryClose()){if(D::Clock::now()>=limit){std::cerr<<"STOP owner close watchdog\n";ExitProcess(10);}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        need(runtime->closed(),"Runtime retained resources after successful close");need(runtime->tryClose(),"Close was not idempotent");bool rejected=false;try{runtime->driver();}catch(const std::exception&){rejected=true;}need(rejected,"Closed runtime exposed driver");std::cout<<"OWNER_CLOSED generation="<<generation<<" ownedReferencesReleased=1 repeatedClose=1 accessRefused=1"<<std::endl;runtime.reset();};
    create();
    for(unsigned f=0;f<8;f++){
        const auto prefix=L"frame"+std::to_wstring(f);const auto dest=output/prefix;std::filesystem::create_directory(dest);const uint64_t token=Base+f;
        auto scene=texture(d11,raw(source/(prefix+L"-scene.raw"),5120,H,26,4));
        if(f==2){need(!runtime->driver().start(token,{},D::Clock::now()+std::chrono::seconds(5)),"Closing owner accepted input");preserveOriginal(context,scene.Get(),source,dest,f);ck(gate.value->Signal(1));close();const auto after=readback(context,retainedDestination.Get(),8);need(after==before,"Closed NR output changed");save(output/L"frame1"/L"destination-after.raw",retainedDestination.Get(),8,after);retainedDestination.Reset();create();std::cout<<"OWNER_FRAME index=2 status=closing-original"<<std::endl;continue;}
        auto depth=texture(d11,raw(source/(prefix+L"-depth.raw"),5120,H,19,8)),velocity=texture(d11,raw(source/(prefix+L"-velocity.raw"),5120,H,35,4));Raw exp{1,1,41,4,std::vector<unsigned char>(4)};std::memcpy(exp.bytes.data(),&frames[f].gain,4);auto exposure=texture(d11,exp);ac7::ClipToPrevious transform;std::memcpy(transform.data(),frames[f].view.data()+110*16,64);
        D::Input input{{scene.Get(),depth.Get(),velocity.Get(),exposure.Get()},transform,frames[f].gain,frames[f].dt};
        if(f==5){auto wrong=texture(d11,{1,1,26,4,std::vector<unsigned char>(4)});auto bad=input;bad.textures[0]=wrong.Get();bool rejected=false;try{runtime->driver().start(token,bad,D::Clock::now()+std::chrono::seconds(5));}catch(const std::exception&e){rejected=std::string(e.what())=="Driver texture layout mismatch";}need(rejected,"Owner accepted unsupported dimensions");preserveOriginal(context,scene.Get(),source,dest,f);close();create();std::cout<<"OWNER_FRAME index=5 status=layout-refused-original"<<std::endl;continue;}
        if(f==1||f==7){retainedDestination=f==1?runtime->nr().output:runtime->sr().output;
            const auto sentinel=raw(baseline/L"nr-output"/L"frame0"/L"nr-output.raw",W,H,10,8);context->UpdateSubresource(retainedDestination.Get(),0,nullptr,sentinel.bytes.data(),W*8,0);before=readback(context,retainedDestination.Get(),8);save(dest/L"destination-before.raw",retainedDestination.Get(),8,before);}
        need(runtime->driver().start(token,input,D::Clock::now()+std::chrono::seconds(5)),"Owner refused frame");const bool reset=runtime->driver().reset();
        if(f==1||f==7){
            if(f==7){const auto limit=D::Clock::now()+std::chrono::seconds(10);while(runtime->driver().stage()!=Stage::restore){need(runtime->driver().advance()==D::Result::pending,"Owner ended before SR");if(D::Clock::now()>=limit)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
            ck(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(gate.value.ReleaseAndGetAddressOf())));ck(queue->Wait(gate.value.Get(),1));need(runtime->driver().advance()==D::Result::pending,"Owner failed to submit gated work");
            for(unsigned i=0;i<16;i++)need(!runtime->tryClose()&&!runtime->closed(),"Pending close destroyed owned resources");
            need(!runtime->driver().output(),"Closing runtime exposed output");preserveOriginal(context,scene.Get(),source,dest,f);std::cout<<"OWNER_PENDING index="<<f<<" closeCalls=16 resourcesRetained=1 outputHidden=1"<<std::endl;
            if(f==7){ck(gate.value->Signal(1));close();const auto after=readback(context,retainedDestination.Get(),8);need(after==before,"Closed SR output changed");save(dest/L"destination-after.raw",retainedDestination.Get(),8,after);retainedDestination.Reset();}
            std::cout<<"OWNER_FRAME index="<<f<<" status="<<(f==1?"NR-close-pending":"SR-close-retired")<<std::endl;continue;
        }
        const auto limit=D::Clock::now()+std::chrono::seconds(10);while(true){const auto result=runtime->driver().advance();if(result==D::Result::published)break;need(result==D::Result::pending,"Owner frame discarded");if(D::Clock::now()>=limit)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}++published;
        if(f==0){compare(context,runtime->proxy(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"proxy.raw");compare(context,runtime->depth(),4,baseline/L"nr-input"/prefix/L"depth.raw",dest/L"depth.raw");compare(context,runtime->motion(),4,baseline/L"nr-input"/prefix/L"motion.raw",dest/L"motion.raw");compare(context,runtime->nr().output.Get(),8,baseline/L"nr-output"/prefix/L"nr-output.raw",dest/L"nr-output.raw");compare(context,runtime->proxy(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"restore-proxy.raw");compare(context,runtime->srInput(),8,baseline/L"sr-input"/prefix/L"color.raw",dest/L"sr-input.raw");compare(context,runtime->sr().output.Get(),8,baseline/L"history"/prefix/L"sr-output.raw",dest/L"sr-output.raw");}
        else{compare(context,runtime->nr().output.Get(),8,driverBaseline/prefix/L"nr-output.raw",dest/L"nr-output.raw");compare(context,runtime->sr().output.Get(),8,driverBaseline/prefix/L"sr-output.raw",dest/L"sr-output.raw");comparisons+=2;}
        std::cout<<"OWNER_FRAME index="<<f<<" status=published Reset="<<reset<<" generation="<<generation<<" token="<<token<<std::endl;debugCheck(d11,d12);
        if(f==0){close();create();}
    }
    need(!runtime&&generation==4&&published==4&&comparisons==6,"Owner coverage incomplete");debugCheck(d11,d12);std::cout<<"OWNER_COMPLETE lifetimes=4 referencesExact=6 pendingCloseCalls=32 partialInitRefusal=1"<<std::endl;
}
}
int wmain(int argc,wchar_t**argv)try{
    if(argc==2&&std::wstring(argv[1])==L"--self-test-motion"){
        // Expected binary16 values independently generated by Python struct.pack('<e', float32 * scale).
        const unsigned cases[][3]={{0x00000000u,0x8000u,0x0000u},{0x80000000u,0x0000u,0x8000u},{0x3f800000u,0xe500u,0x61a0u},{0xbf800000u,0x6500u,0xe1a0u},{0x35800000u,0x9500u,0x11a0u},{0x33800000u,0x8500u,0x02d0u},{0x30000000u,0x800au,0x0006u},{0x28800000u,0x8000u,0x0000u},{0x38000000u,0xa900u,0x25a0u},{0x37ffffffu,0xa900u,0x25a0u},{0x324ccccdu,0x8100u,0x0090u},{0xb24ccccdu,0x0100u,0x8090u},{0x39000001u,0xb100u,0x2da0u},{0x39abcdefu,0xb6b6u,0x338du},{0xb7f12345u,0x28b6u,0xa54cu},{0x3c012345u,0xc90bu,0x45adu},{0x38004000u,0xa902u,0x25a3u},{0x3800c000u,0xa908u,0x25a8u},{0x38020000u,0xa914u,0x25b6u},{0x38060000u,0xa93cu,0x25e4u}};
        ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c));Convert conversion(d.Get());Raw values{W,H,16,8,std::vector<unsigned char>(size_t(W)*H*8)};
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){std::memcpy(values.bytes.data()+i*8,&cases[i][0],4);std::memcpy(values.bytes.data()+i*8+4,&cases[i][0],4);}auto input=texture(d.Get(),values);auto result=readback(c.Get(),conversion.run(c.Get(),input.Get(),true),4);
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++)for(unsigned axis=0;axis<2;axis++){uint16_t actual;std::memcpy(&actual,result.data()+i*4+axis*2,2);need(actual==cases[i][axis+1],"GPU half motion edge self-test mismatch");}
        ComPtr<ID3D11InfoQueue>q;ck(d.As(&q));for(UINT64 i=0;i<q->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;ck(q->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D11_MESSAGE*>(b.data());ck(q->GetMessage(i,m,&n));need(m->Severity>D3D11_MESSAGE_SEVERITY_ERROR,"GPU conversion self-test debug error");}
        std::cout<<"PASS GPU motion40 components: half-even/signed-zero/subnormal/tiny/ordinary; debug0; no NGX calls"<<std::endl;return 0;
    }
    if(argc==2&&std::wstring(argv[1])==L"--self-test-schedule"){selfTestSchedule();return 0;}
    const bool deadlineTest=argc==7&&std::wstring(argv[6])==L"--deadline-test";
    const bool scheduledTest=argc==7&&std::wstring(argv[6])==L"--scheduled-test";
    const bool errorMode=argc==7&&std::wstring(argv[6])==L"--driver-error-test";
    const bool driverMode=argc==7&&(std::wstring(argv[6])==L"--driver-test"||errorMode);
    const bool ownedMode=argc==7&&std::wstring(argv[6])==L"--owned-session-test";
    need(argc==6||deadlineTest||scheduledTest||driverMode||ownedMode,"Usage: gpu-chain NR_RUNTIME SR_RUNTIME AUDITED_LIVE8 FILE_BASELINE NEW_OUTPUT [--deadline-test|--scheduled-test|--driver-test|--driver-error-test|--owned-session-test]");
    const std::filesystem::path nrPath=std::filesystem::absolute(argv[1]),srPath=std::filesystem::absolute(argv[2]),source(argv[3]),baseline(argv[4]),output(argv[5]);
    need(!std::filesystem::exists(output),"Refusing prior output");need(std::filesystem::is_regular_file(source/L"audit.json")&&std::filesystem::is_regular_file(source/L"capture.json")&&!std::filesystem::exists(source/L"INCOMPLETE"),"Missing completed audited capture");need(std::filesystem::is_regular_file(nrPath/L"nvngx_dlssnr.dll")&&std::filesystem::is_regular_file(srPath/L"nvngx_dlss.dll"),"Missing official runtime");
    std::array<Frame,8> frames;
    for(unsigned f=0;f<8;f++){auto prefix=L"frame"+std::to_wstring(f);auto& frame=frames[f];std::ifstream in(source/(prefix+L"-view.bin"),std::ios::binary|std::ios::ate);need(bool(in)&&in.tellg()==4096,"Missing View");frame.view.resize(4096);in.seekg(0);in.read(reinterpret_cast<char*>(frame.view.data()),4096);need(bool(in),"View read failed");auto fl=[&](unsigned row,unsigned c){float v;std::memcpy(&v,frame.view.data()+row*16+c*4,4);need(std::isfinite(v),"Nonfinite View value");return v;};for(unsigned c=0;c<4;c++)need(fl(114,c)==0,"Actual jitter nonzero unsupported");need(fl(126,0)==1280&&fl(126,1)==0&&fl(127,0)==W&&fl(127,1)==H&&fl(128,0)==5120&&fl(128,1)==1440,"Unsupported native rect");std::memcpy(&frame.counter,frame.view.data()+137*16+4,4);frame.dt=(fl(136,2)-fl(133,2))*1000;need(frame.dt>0,"Simulation not advancing");if(f){need(frame.counter==frames[f-1].counter+1,"Counter gap");for(unsigned c=2;c<4;c++){float before;std::memcpy(&before,frames[f-1].view.data()+136*16+c*4,4);need(fl(133,c)==before&&fl(136,c)>before,"Time chain mismatch");}}
        const auto exp=raw(source/(prefix+L"-exposure.raw"),1,1,16,8);std::memcpy(&frame.gain,exp.bytes.data(),4);need(std::isfinite(frame.gain)&&frame.gain>=1.f/65504&&frame.gain<=65504,"Invalid exposure");
        const auto scene=raw(source/(prefix+L"-scene.raw"),5120,H,26,4),depth=raw(source/(prefix+L"-depth.raw"),5120,H,19,8);
        for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){const auto index=size_t(y)*5120+x+1280;unsigned word;float z;std::memcpy(&word,scene.bytes.data()+index*4,4);std::memcpy(&z,depth.bytes.data()+index*8,4);need(((word>>6)&31)!=31&&((word>>17)&31)!=31&&((word>>27)&31)!=31,"Nonfinite native HDR before model initialization");need(std::isfinite(z)&&z>=0&&z<=1,"Invalid native depth before model initialization");}
        for(unsigned row=110;row<114;row++)for(unsigned col=0;col<4;col++)fl(row,col);
        raw(source/(prefix+L"-velocity.raw"),5120,H,35,4);
        for(const auto& p:{baseline/L"nr-input"/prefix/L"color.raw",baseline/L"nr-input"/prefix/L"depth.raw",baseline/L"nr-input"/prefix/L"motion.raw",baseline/L"nr-output"/prefix/L"nr-output.raw",baseline/L"sr-input"/prefix/L"color.raw",baseline/L"history"/prefix/L"sr-output.raw"})need(std::filesystem::is_regular_file(p),"Missing late baseline");
    }
    if(ownedMode)for(unsigned f:{3u,4u,6u})for(const auto* name:{L"nr-output.raw",L"sr-output.raw"})need(std::filesystem::is_regular_file(baseline.parent_path()/L"nonblocking-chain-v1"/L"run-v2"/(L"frame"+std::to_wstring(f))/name),"Missing qualified owner reset baseline");
    std::filesystem::create_directories(output);std::ofstream(output/L"INCOMPLETE")<<"GPU chain not fully validated\n";
    ComPtr<ID3D12Debug> debug;ck(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<ID3D11Device>d11;ComPtr<ID3D11DeviceContext>context;ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&d11,nullptr,&context));ComPtr<IDXGIDevice>dxgi;ck(d11.As(&dxgi));ComPtr<IDXGIAdapter>adapter;ck(dxgi->GetAdapter(&adapter));ComPtr<ID3D12Device>d12;ck(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12)));D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue>queue;ck(d12->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    Session session{d12.Get()};const wchar_t* paths[]={nrPath.c_str(),srPath.c_str()};NVSDK_NGX_FeatureCommonInfo common{};common.PathListInfo={paths,2};ngx(NVSDK_NGX_D3D12_Init_with_ProjectID("05882ed0-d4cd-4063-a726-0b4862bad3dc",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"AC7 independent GPU chain",output.c_str(),d12.Get(),&common),"Init");session.initialized=true;
    if(ownedMode)ownerTest(d11.Get(),context.Get(),d12.Get(),queue.Get(),frames,source,baseline,output,nrPath,srPath);
    else {Model nr(d11.Get(),d12.Get(),queue.Get(),false),sr(d11.Get(),d12.Get(),queue.Get(),true);for(const auto& pair:{std::pair{L"nvngx_dlssnr.dll",nrPath},std::pair{L"nvngx_dlss.dll",srPath}}){HMODULE module=GetModuleHandleW(pair.first);need(module!=nullptr,"Feature runtime not loaded");wchar_t p[32768]{};need(GetModuleFileNameW(module,p,32768)>0,"Runtime path query failed");need(std::filesystem::equivalent(p,pair.second/pair.first),"Wrong model module loaded");}
    ac7::HdrProxy codec(d11.Get(),W,H);ac7::GuideNormalizer normalizer(d11.Get(),W,H);Convert conversion(d11.Get());
    if(driverMode)driverTest(nr,sr,d11.Get(),context.Get(),codec,normalizer,conversion,frames,source,baseline,output,errorMode);
    else if(scheduledTest)scheduledChain(nr,sr,d11.Get(),context.Get(),codec,normalizer,conversion,frames,source,baseline,output);
    else {
    bool resetNext=true;Model* pending=nullptr;unsigned pendingFrame=0;ComPtr<ID3D11Texture2D> pendingScene;
    Gate gate;std::vector<unsigned char> destinationBefore;std::unique_ptr<Model> referenceNR,referenceSR;
    unsigned accepted=0,expiredFrames=0,busyFrames=0,references=0;
    for(unsigned f=0;f<8;f++){
        auto prefix=L"frame"+std::to_wstring(f);auto dest=output/prefix;std::filesystem::create_directory(dest);
        auto scene=texture(d11.Get(),raw(source/(prefix+L"-scene.raw"),5120,H,26,4));
        if(pending){
            need(deadlineTest&&(f==2||f==6),"Unexpected pending test schedule");
            // Return this frame's original image before reclaiming the old transaction.
            preserveOriginal(context.Get(),scene.Get(),source,dest,f);expectBusy(*pending,f,frames[f].dt);++busyFrames;resetNext=true;
            std::cout<<"DEADLINE_FRAME index="<<f<<" status=busy-original"<<std::endl;
            ck(gate.value->Signal(1));pending->finish(pendingFrame,false);
            const auto destinationAfter=readback(context.Get(),pending->output.Get(),8);
            need(destinationAfter==destinationBefore,"Abandoned result overwrote same-token destination");
            for(const auto&input:pending->submission.inputs())need(!input,"Retired model retained fixture input");
            need(!pending->bridge.busy(),"Abandoned model did not retire");
            auto retired=output/(L"frame"+std::to_wstring(pendingFrame));preserveOriginal(context.Get(),pendingScene.Get(),source,retired,pendingFrame,L"retired-scene.raw");
            save(retired/L"destination-after.raw",pending->output.Get(),8,destinationAfter);
            std::cout<<"MODEL_RETIRED role="<<(pending->sr?"SR":"NR")<<" sourceFrame="<<pendingFrame<<" currentFrame="<<f<<" discarded=1 sameToken=1 destinationExact=1 heldReleased=1"<<std::endl;
            pending=nullptr;pendingScene.Reset();continue;
        }
        auto depth=texture(d11.Get(),raw(source/(prefix+L"-depth.raw"),5120,H,19,8));auto velocity=texture(d11.Get(),raw(source/(prefix+L"-velocity.raw"),5120,H,35,4));
        ac7::ClipToPrevious transform;std::memcpy(transform.data(),frames[f].view.data()+110*16,64);normalizer.record(context.Get(),depth.Get(),velocity.Get(),{1280,0,W,H},transform);
        const auto validity=readback(context.Get(),normalizer.validity(),4);for(size_t i=0;i<validity.size();i+=4){unsigned v;std::memcpy(&v,validity.data()+i,4);need(v==1||v==2,"Invalid guide pixels before model");}
        auto motion=conversion.run(context.Get(),normalizer.motion(),true);codec.record(context.Get(),scene.Get(),{1280,0,W,H},frames[f].gain);
        Raw exp{1,1,41,4,std::vector<unsigned char>(4)};std::memcpy(exp.bytes.data(),&frames[f].gain,4);auto exposure=texture(d11.Get(),exp);
        const std::array<ID3D11Texture2D*,4> nrInputs={codec.prepared(),normalizer.depth(),motion,exposure.Get()};
        compare(context.Get(),codec.prepared(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"proxy.raw");compare(context.Get(),normalizer.depth(),4,baseline/L"nr-input"/prefix/L"depth.raw",dest/L"depth.raw");compare(context.Get(),motion,4,baseline/L"nr-input"/prefix/L"motion.raw",dest/L"motion.raw");
        if(deadlineTest&&f==1){
            destinationBefore=readback(context.Get(),nr.output.Get(),8);save(dest/L"destination-before.raw",nr.output.Get(),8,destinationBefore);
            pendingScene=scene;ck(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(gate.value.ReleaseAndGetAddressOf())));ck(queue->Wait(gate.value.Get(),1));
            nr.begin(nrInputs,f,frames[f].dt,resetNext,std::chrono::milliseconds(2));waitExpired(nr,f);expectBusy(nr,f+1,frames[f].dt);preserveOriginal(context.Get(),scene.Get(),source,dest,f);
            pending=&nr;pendingFrame=f;resetNext=true;++expiredFrames;
            std::cout<<"DEADLINE_FRAME index="<<f<<" status=expired-NR-original"<<std::endl;continue;
        }
        auto neural=nr.run(nrInputs,f,frames[f].dt,resetNext);
        if(!deadlineTest||f==0)compare(context.Get(),neural,8,baseline/L"nr-output"/prefix/L"nr-output.raw",dest/L"nr-output.raw");
        else save(dest/L"nr-output.raw",neural,8,readback(context.Get(),neural,8));
        codec.record(context.Get(),scene.Get(),{1280,0,W,H},frames[f].gain,neural);compare(context.Get(),codec.prepared(),8,baseline/L"nr-input"/prefix/L"color.raw",dest/L"restore-proxy.raw");auto srColor=conversion.run(context.Get(),codec.restored(),false);
        if(!deadlineTest||f==0)compare(context.Get(),srColor,8,baseline/L"sr-input"/prefix/L"color.raw",dest/L"sr-input.raw");
        else save(dest/L"sr-input.raw",srColor,8,readback(context.Get(),srColor,8));
        if(deadlineTest&&f==5){
            destinationBefore=readback(context.Get(),sr.output.Get(),8);save(dest/L"destination-before.raw",sr.output.Get(),8,destinationBefore);
            pendingScene=scene;ck(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(gate.value.ReleaseAndGetAddressOf())));ck(queue->Wait(gate.value.Get(),1));
            sr.begin({srColor,normalizer.depth(),motion,exposure.Get()},f,frames[f].dt,resetNext,std::chrono::milliseconds(2));waitExpired(sr,f);expectBusy(sr,f+1,frames[f].dt);preserveOriginal(context.Get(),scene.Get(),source,dest,f);
            pending=&sr;pendingFrame=f;resetNext=true;++expiredFrames;
            std::cout<<"DEADLINE_FRAME index="<<f<<" status=expired-SR-original"<<std::endl;continue;
        }
        auto final=sr.run({srColor,normalizer.depth(),motion,exposure.Get()},f,frames[f].dt,resetNext);
        if(!deadlineTest||f==0)compare(context.Get(),final,8,baseline/L"history"/prefix/L"sr-output.raw",dest/L"sr-output.raw");
        else save(dest/L"sr-output.raw",final,8,readback(context.Get(),final,8));
        if(deadlineTest&&f>=3){
            if(resetNext){referenceSR.reset();referenceNR.reset();referenceNR=std::make_unique<Model>(d11.Get(),d12.Get(),queue.Get(),false,true);referenceSR=std::make_unique<Model>(d11.Get(),d12.Get(),queue.Get(),true,true);}
            need(referenceNR&&referenceSR,"Missing recovery reference models");
            auto freshNR=referenceNR->run(nrInputs,f,frames[f].dt,resetNext);
            need(readback(context.Get(),freshNR,8)==readback(context.Get(),neural,8),"NR recovered history differs from clean reference");save(dest/L"reference-nr.raw",freshNR,8,readback(context.Get(),freshNR,8));
            codec.record(context.Get(),scene.Get(),{1280,0,W,H},frames[f].gain,freshNR);auto freshColor=conversion.run(context.Get(),codec.restored(),false);
            auto freshSR=referenceSR->run({freshColor,normalizer.depth(),motion,exposure.Get()},f,frames[f].dt,resetNext);
            need(readback(context.Get(),freshSR,8)==readback(context.Get(),final,8),"SR recovered history differs from clean reference");save(dest/L"reference-sr.raw",freshSR,8,readback(context.Get(),freshSR,8));++references;
            std::cout<<"RECOVERY_REFERENCE index="<<f<<" NRExact=1 SRExact=1 Reset="<<resetNext<<std::endl;
        }
        ++accepted;debugCheck(d11.Get(),d12.Get());
        if(deadlineTest)std::cout<<"DEADLINE_FRAME index="<<f<<" status=published Reset="<<resetNext<<std::endl;
        else std::cout<<"GPU_CHAIN_FRAME index="<<f<<" counter="<<frames[f].counter<<" exact; no intermediate images feed CPU/files"<<std::endl;
        resetNext=false;
    }
    need(!pending,"Pending chain at end");
    if(deadlineTest)need(accepted==4&&expiredFrames==2&&busyFrames==2&&references==3,"Deadline scenario coverage incomplete");
    }
    }ngx(NVSDK_NGX_D3D12_Shutdown1(d12.Get()),"Shutdown");session.initialized=false;
    std::ofstream done(output/(ownedMode?L"owned-complete.txt":(driverMode?L"driver-complete.txt":(scheduledTest?L"scheduled-complete.txt":(deadlineTest?L"deadline-complete.txt":L"complete.txt")))));done<<(ownedMode?"Owned fixed runtime: four lifetimes, bounded close, partial-init refusal, original-baseline recovery exact\n":(driverMode?"8 inputs; incremental driver; NR/SR stop/drain and source snapshots; session reset references exact\n":(scheduledTest?"8 inputs; shared absolute frame deadline; accepted4/dropped4; coupled history recovery exact\n":(deadlineTest?"8 inputs; accepted4/expired2/busy2; NR/SR recovery references3 exact; no late publication\n":"8 frames; NR8/SR8; all seven stages exact; both API debug0; no intermediate CPU image feed\n"))));done.close();need(bool(done),"Completion marker failed");std::filesystem::remove(output/L"INCOMPLETE");return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
