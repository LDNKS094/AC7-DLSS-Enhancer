// Independent public-export probe. Never injects into or starts AC7. No NGX loader linked.
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3d12sdklayers.h>
#include <dxgi1_2.h>
#include "shared_texture_bridge.h"
#include <chrono>
#include <thread>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include "../nr/evaluation_parameters.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <utility>
using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned W=2560,H=1440;
LONG CALLBACK observeException(EXCEPTION_POINTERS* p){
    const auto code=p->ExceptionRecord->ExceptionCode;if(code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_ILLEGAL_INSTRUCTION||code==0xc0000409){
        HMODULE m=nullptr;wchar_t name[1024]{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(p->ExceptionRecord->ExceptionAddress),&m);if(m)GetModuleFileNameW(m,name,1024);
        std::fwprintf(stderr,L"NativeException code=0x%08lx module=%ls offset=0x%llx\n",code,name,static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(m)));std::fflush(stderr);
    }return EXCEPTION_CONTINUE_SEARCH;
}
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(h));}
void ngx(NVSDK_NGX_Result r,const char* stage){std::cout<<stage<<" result=0x"<<std::hex<<unsigned(r)<<std::dec<<std::endl;need(NVSDK_NGX_SUCCEED(r),stage);}
template<class T>T symbol(HMODULE m,const char* name){auto p=GetProcAddress(m,name);need(p!=nullptr,name);std::cout<<"Export "<<name<<" present"<<std::endl;return reinterpret_cast<T>(p);}
struct Raw {unsigned w,h,fmt,bpp;std::vector<unsigned char> bytes;};
Raw read(const std::filesystem::path& p,unsigned fmt,unsigned bpp,bool exp=false){
    Raw r{};std::ifstream in(p,std::ios::binary|std::ios::ate);need(bool(in),"Missing fixture");const auto length=in.tellg();in.seekg(0);unsigned header[4]{};in.read(reinterpret_cast<char*>(header),16);r.w=header[0];r.h=header[1];r.fmt=header[2];r.bpp=header[3];need(r.w==(exp?1:W)&&r.h==(exp?1:H)&&r.fmt==fmt&&r.bpp==bpp&&length==std::streamoff(16+size_t(r.w)*r.h*bpp),"Invalid raw layout");r.bytes.resize(size_t(r.w)*r.h*bpp);in.read(reinterpret_cast<char*>(r.bytes.data()),r.bytes.size());need(bool(in),"Raw read failed");return r;
}
float frameTime(const std::filesystem::path& directory){float dt=0;std::ifstream in(directory/L"frame-time.bin",std::ios::binary|std::ios::ate);need(bool(in)&&in.tellg()==4,"Missing actual frame time");in.seekg(0);in.read(reinterpret_cast<char*>(&dt),4);need(bool(in)&&std::isfinite(dt)&&dt>0,"Invalid dt");return dt;}
ComPtr<ID3D11Texture2D> texture(ID3D11Device*d,const Raw&r,bool output=false){D3D11_TEXTURE2D_DESC t{};t.Width=r.w;t.Height=r.h;t.Format=DXGI_FORMAT(r.fmt);t.ArraySize=t.MipLevels=t.SampleDesc.Count=1;t.BindFlags=D3D11_BIND_SHADER_RESOURCE|(output?D3D11_BIND_UNORDERED_ACCESS:0);D3D11_SUBRESOURCE_DATA init{r.bytes.data(),r.w*r.bpp,0};ComPtr<ID3D11Texture2D> result;ck(d->CreateTexture2D(&t,&init,&result));return result;}
std::vector<unsigned char> bytes(ID3D11Device*d,ID3D11DeviceContext*c,ID3D11Texture2D*t,unsigned bpp){D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D>s;ck(d->CreateTexture2D(&desc,nullptr,&s));c->CopyResource(s.Get(),t);D3D11_MAPPED_SUBRESOURCE m{};ck(c->Map(s.Get(),0,D3D11_MAP_READ,0,&m));std::vector<unsigned char> r(size_t(desc.Width)*desc.Height*bpp);for(unsigned y=0;y<desc.Height;y++)std::memcpy(r.data()+size_t(y)*desc.Width*bpp,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(desc.Width)*bpp);c->Unmap(s.Get(),0);return r;}
float half(unsigned v){const auto e=v>>10&31,m=v&1023;need(e!=31,"Untouched NaN seed or nonfinite SR output");return (v&32768?-1.f:1.f)*(e?std::ldexp(1.f+float(m)/1024,int(e)-15):std::ldexp(float(m),-24));}
void save(const std::filesystem::path&p,const Raw&r,const std::vector<unsigned char>& b){need(!std::filesystem::exists(p),"Refusing raw overwrite");std::ofstream out(p,std::ios::binary);unsigned h[]={r.w,r.h,r.fmt,r.bpp};out.write(reinterpret_cast<char*>(h),16);out.write(reinterpret_cast<const char*>(b.data()),b.size());out.close();need(bool(out),"Save failed");}
void barrier(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
int nativeDx12(HMODULE module,ID3D11Device*d11,ID3D11DeviceContext*c,std::array<Raw,4> data,float dt,const std::filesystem::path& sr,const std::filesystem::path& out,const std::filesystem::path& sequenceRoot,unsigned frameCount,bool resetAll,bool nrMode){
    ComPtr<ID3D12Debug>debug;ck(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<IDXGIDevice>dxgi;ck(d11->QueryInterface(IID_PPV_ARGS(&dxgi)));ComPtr<IDXGIAdapter>adapter;ck(dxgi->GetAdapter(&adapter));ComPtr<ID3D12Device>d12;ck(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12)));D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue>q;ck(d12->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)));ac7::SharedTextureBridge bridge(d11,d12.Get(),q.Get(),W,H);
    auto init=symbol<decltype(&NVSDK_NGX_D3D12_Init_with_ProjectID)>(module,"NVSDK_NGX_D3D12_Init_with_ProjectID");auto get=symbol<decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters)>(module,"NVSDK_NGX_D3D12_GetCapabilityParameters");auto create=symbol<decltype(&NVSDK_NGX_D3D12_CreateFeature)>(module,"NVSDK_NGX_D3D12_CreateFeature");auto evaluate=symbol<decltype(&NVSDK_NGX_D3D12_EvaluateFeature)>(module,"NVSDK_NGX_D3D12_EvaluateFeature");auto release=symbol<decltype(&NVSDK_NGX_D3D12_ReleaseFeature)>(module,"NVSDK_NGX_D3D12_ReleaseFeature");auto destroy=symbol<decltype(&NVSDK_NGX_D3D12_DestroyParameters)>(module,"NVSDK_NGX_D3D12_DestroyParameters");using Shutdown=NVSDK_NGX_Result(NVSDK_CONV*)();auto shutdown=symbol<Shutdown>(module,"NVSDK_NGX_D3D12_Shutdown");
    const wchar_t*paths[]={sr.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};ngx(init("05882ed0-d4cd-4063-a726-0b4862bad3dc",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"Independent public DX12 interface",out.c_str(),d12.Get(),&info,NVSDK_NGX_Version_API),"Init12");NVSDK_NGX_Parameter*p=nullptr;ngx(get(&p),"GetCapabilityParameters12");need(p,"Missing native parameters");
    if(nrMode){int available=0;const auto capability=p->Get(NVSDK_NGX_Parameter_DLSSNR_Available,&available);std::cout<<"NRCapability result="<<unsigned(capability)<<" available="<<available<<std::endl;if(!NVSDK_NGX_SUCCEED(capability)||available!=1){ngx(destroy(p),"DestroyParameters12");ngx(shutdown(),"Shutdown12");need(false,"Independent NR unavailable through public OptiScaler entry");}p->Set(NVSDK_NGX_Parameter_DLSSNR_Width,W);p->Set(NVSDK_NGX_Parameter_DLSSNR_Height,H);}
    else {
    p->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);p->Set(NVSDK_NGX_Parameter_Width,W);p->Set(NVSDK_NGX_Parameter_Height,H);p->Set(NVSDK_NGX_Parameter_OutWidth,W);p->Set(NVSDK_NGX_Parameter_OutHeight,H);p->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_DLAA));p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));
    }
    ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;ck(d12->CreateCommandAllocator(qd.Type,IID_PPV_ARGS(&a)));ck(d12->CreateCommandList(0,qd.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));NVSDK_NGX_Handle*feature=nullptr;ngx(create(l.Get(),nrMode?NVSDK_NGX_Feature_DLSSNR:NVSDK_NGX_Feature_SuperSampling,p,&feature),"CreateFeature12");need(feature,"Null native feature");ck(l->Close());
    ComPtr<ID3D12Fence>f;ck(d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));ID3D12CommandList*lists[]={l.Get()};q->ExecuteCommandLists(1,lists);if(FAILED(q->Signal(f.Get(),1)))ExitProcess(10);const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(f->GetCompletedValue()<1){if(std::chrono::steady_clock::now()>=limit)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}need(f->GetCompletedValue()!=UINT64_MAX,"Native device lost");
    std::array<ComPtr<ID3D11Texture2D>,4>inputs;std::array<ID3D11Texture2D*,4>raw{};for(unsigned i=0;i<4;i++){inputs[i]=texture(d11,data[i]);raw[i]=inputs[i].Get();}Raw seed{W,H,10,8,std::vector<unsigned char>(size_t(W)*H*8)};for(size_t i=0;i<seed.bytes.size();i+=2){seed.bytes[i]=0;seed.bytes[i+1]=0x7e;}auto output=texture(d11,seed,true); // Seed shared output too, so a no-op backend cannot pass.
    D3D12_HEAP_PROPERTIES uploadHeap{};uploadHeap.Type=D3D12_HEAP_TYPE_UPLOAD;uploadHeap.CreationNodeMask=uploadHeap.VisibleNodeMask=1;auto desc=bridge.resource(4)->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT64 size=0;d12->GetCopyableFootprints(&desc,0,1,0,&layout,nullptr,nullptr,&size);D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=size;bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource>seedUpload;ck(d12->CreateCommittedResource(&uploadHeap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&seedUpload)));void*mapped=nullptr;D3D12_RANGE empty{};ck(seedUpload->Map(0,&empty,&mapped));for(unsigned y=0;y<H;y++)std::memcpy(static_cast<unsigned char*>(mapped)+layout.Offset+size_t(y)*layout.Footprint.RowPitch,seed.bytes.data()+size_t(y)*W*8,W*8);seedUpload->Unmap(0,nullptr);
    for(unsigned frame=0;frame<frameCount;frame++){
    if(frame){const auto dir=sequenceRoot/(L"frame"+std::to_wstring(frame));data={read(dir/L"color.raw",10,8),read(dir/L"depth.raw",41,4),read(dir/L"motion.raw",34,4),read(dir/L"exposure.raw",41,4,true)};dt=frameTime(dir);for(unsigned i=0;i<4;i++)c->UpdateSubresource(inputs[i].Get(),0,nullptr,data[i].bytes.data(),data[i].w*data[i].bpp,0);}
    const bool reset=frame==0||resetAll;const auto dest=frameCount==1?out:out/(L"frame"+std::to_wstring(frame));if(frameCount>1)std::filesystem::create_directory(dest);
    ck(a->Reset());ck(l->Reset(a.Get(),nullptr));barrier(l.Get(),bridge.resource(4),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION target{},origin{};target.pResource=bridge.resource(4);target.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;origin.pResource=seedUpload.Get();origin.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;origin.PlacedFootprint=layout;l->CopyTextureRegion(&target,0,0,0,&origin,nullptr);barrier(l.Get(),bridge.resource(4),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for(unsigned i=0;i<4;i++)barrier(l.Get(),bridge.resource(i),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const char*keys[]={NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_ExposureTexture};for(unsigned i=0;i<4;i++)p->Set(keys[i],bridge.resource(i));p->Set(NVSDK_NGX_Parameter_Output,bridge.resource(4));p->Set(NVSDK_NGX_Parameter_Reset,int(reset));p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);p->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.f);p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.f);p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,dt);p->Set(NVSDK_NGX_Parameter_Sharpness,0.f);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,W);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,H);
    if(nrMode)configureCapturedEvaluation(p,{bridge.resource(0),bridge.resource(1),bridge.resource(2),bridge.resource(4),bridge.resource(3)},false,reset,false,dt);
    if(frameCount>1)std::cout<<"SEQUENCE_FRAME index="<<frame<<" Reset="<<reset<<" dtMs="<<dt<<" feature="<<feature<<" color="<<bridge.resource(0)<<" output="<<bridge.resource(4)<<std::endl;
    std::cout<<"Evaluate12Attempt actualDtMs="<<dt<<" P=1 uncalibrated jitter=0"<<std::endl;ngx(evaluate(l.Get(),feature,p,nullptr),"EvaluateFeature12");for(unsigned i=0;i<4;i++)barrier(l.Get(),bridge.resource(i),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);barrier(l.Get(),bridge.resource(4),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);ck(l->Close());
    try{bridge.upload(uint64_t(frame)+1,raw);bridge.submit(l.Get());const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);ac7::SharedTextureBridge::Result result;while((result=bridge.poll(uint64_t(frame)+1,output.Get()))==ac7::SharedTextureBridge::Result::pending){if(std::chrono::steady_clock::now()>=end)ExitProcess(10);std::this_thread::sleep_for(std::chrono::milliseconds(1));}need(result==ac7::SharedTextureBridge::Result::published,"Native output discarded");}catch(const std::exception&e){std::cerr<<"STOP native probe submission: "<<e.what()<<std::endl;ExitProcess(10);}
    const auto answer=bytes(d11,c,output.Get(),8);save(dest/(nrMode?L"nr-output.raw":L"sr-output.raw"),seed,answer);float maximum=0;for(size_t i=0;i<answer.size();i+=8)for(unsigned k=0;k<4;k++){uint16_t v;std::memcpy(&v,answer.data()+i+2*k,2);const float value=half(v);if(nrMode&&k<3)need(value>=0&&value<=1,"NR SDR result out of domain");if(k<3&&value>maximum)maximum=value;}need(answer!=data[0].bytes&&(nrMode?(maximum>0&&maximum<=1):maximum>1),"No native model response in expected domain");for(unsigned i=0;i<4;i++){need(bytes(d11,c,inputs[i].Get(),data[i].bpp)==data[i].bytes,"Native input mutated");std::cout<<"InputExact index="<<i<<std::endl;}
    if(frameCount==1)std::cout<<"OutputFiniteRGB=11059200 max="<<maximum<<std::endl;
    if(frameCount>1)std::cout<<"SEQUENCE_RESULT index="<<frame<<" finiteRgb=11059200 max="<<maximum<<" inputReadbacksExact=4"<<std::endl;
    }
    ngx(release(feature),"ReleaseFeature12");ngx(destroy(p),"DestroyParameters12");ngx(shutdown(),"Shutdown12");unsigned errors=0;ComPtr<ID3D11InfoQueue>q11;ComPtr<ID3D12InfoQueue>q12;ck(d11->QueryInterface(IID_PPV_ARGS(&q11)));ck(d12.As(&q12));for(UINT64 i=0;i<q11->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;q11->GetMessage(i,nullptr,&n);std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D11_MESSAGE*>(b.data());q11->GetMessage(i,m,&n);if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}for(UINT64 i=0;i<q12->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;q12->GetMessage(i,nullptr,&n);std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D12_MESSAGE*>(b.data());q12->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}std::cout<<"BothDebugErrors="<<errors<<" FrameCount="<<frameCount<<std::endl;need(errors==0,"Native API debug errors");std::ofstream done(out/(nrMode?L"native-nr-complete.txt":frameCount==1?L"native-complete.txt":L"native-eight-complete.txt"));done<<(nrMode?"Public DX12 independent NR passed; inspect backend log\n":"Public DX12 SR through existing bridge passed; inspect backend log\n");done.close();need(bool(done),"Completion marker failed");std::filesystem::remove(out/L"INCOMPLETE");return 0;
}
}
int wmain(int argc,wchar_t**argv)try{
    need(argc>=5&&argc<=8,"Usage: optiscaler-interface OPTISCALER_DLL SR_RUNTIME FIXTURE_FRAME_OR_SEQUENCE NEW_OUTPUT [--fl11] [--shutdown-base] [--native-dx12|--native-eight|--native-nr] [--reset-control]");
    bool fl11=false,baseShutdown=false,native=false,eight=false,resetAll=false,nrMode=false;for(int i=5;i<argc;i++){const std::wstring flag(argv[i]);if(flag==L"--fl11")fl11=true;else if(flag==L"--shutdown-base")baseShutdown=true;else if(flag==L"--native-dx12")native=true;else if(flag==L"--native-nr"){native=nrMode=true;}else if(flag==L"--native-eight"){native=eight=true;}else if(flag==L"--reset-control")resetAll=true;else need(false,"Unknown diagnostic argument");}
    need(!nrMode||!eight,"Independent NR probe is single-frame");
    need(!resetAll||eight,"Reset control requires fixed eight-frame mode");
    AddVectoredExceptionHandler(1,observeException);
    const auto dll=std::filesystem::absolute(argv[1]),sr=std::filesystem::absolute(argv[2]),input=std::filesystem::absolute(argv[3]),out=std::filesystem::absolute(argv[4]);
    need(!std::filesystem::exists(out),"Refusing existing output");need(std::filesystem::is_regular_file(dll)&&std::filesystem::is_regular_file(sr/L"nvngx_dlss.dll"),"Missing runtime");
    if(eight){need(std::filesystem::is_regular_file(input/L"sequence-contract.json"),"Missing qualified sequence contract");for(unsigned f=0;f<8;f++){const auto dir=input/(L"frame"+std::to_wstring(f));read(dir/L"color.raw",10,8);read(dir/L"depth.raw",41,4);read(dir/L"motion.raw",34,4);read(dir/L"exposure.raw",41,4,true);frameTime(dir);}}
    const auto first=eight?input/L"frame0":input;
    std::array<Raw,4> data={read(first/L"color.raw",10,8),read(first/L"depth.raw",41,4),read(first/L"motion.raw",34,4),read(first/L"exposure.raw",41,4,true)};
    const float dt=frameTime(first);
    std::filesystem::create_directories(out);std::ofstream(out/L"INCOMPLETE")<<"Probe not accepted\n";
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;const D3D_FEATURE_LEVEL requested=fl11?D3D_FEATURE_LEVEL_11_0:D3D_FEATURE_LEVEL_11_1;D3D_FEATURE_LEVEL actual{};ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,&requested,1,D3D11_SDK_VERSION,&d,&actual,&c));std::cout<<"FeatureLevel=0x"<<std::hex<<unsigned(actual)<<std::dec<<std::endl;
    // Full-path dynamic loading tests the actual package entry, including DllMain/global hooks.
    HMODULE module=LoadLibraryExW(dll.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module)throw std::runtime_error("LoadLibrary failed "+std::to_string(GetLastError()));std::cout<<"LoadedOptiScaler"<<std::endl;
    if(native)return nativeDx12(module,d.Get(),c.Get(),std::move(data),dt,sr,out,input,eight?8u:1u,resetAll,nrMode);
    auto init=symbol<decltype(&NVSDK_NGX_D3D11_Init_with_ProjectID)>(module,"NVSDK_NGX_D3D11_Init_with_ProjectID");
    auto get=symbol<decltype(&NVSDK_NGX_D3D11_GetCapabilityParameters)>(module,"NVSDK_NGX_D3D11_GetCapabilityParameters");
    auto create=symbol<decltype(&NVSDK_NGX_D3D11_CreateFeature)>(module,"NVSDK_NGX_D3D11_CreateFeature");
    auto evaluate=symbol<decltype(&NVSDK_NGX_D3D11_EvaluateFeature)>(module,"NVSDK_NGX_D3D11_EvaluateFeature");
    auto release=symbol<decltype(&NVSDK_NGX_D3D11_ReleaseFeature)>(module,"NVSDK_NGX_D3D11_ReleaseFeature");
    auto destroy=symbol<decltype(&NVSDK_NGX_D3D11_DestroyParameters)>(module,"NVSDK_NGX_D3D11_DestroyParameters");
    auto shutdown=symbol<decltype(&NVSDK_NGX_D3D11_Shutdown1)>(module,"NVSDK_NGX_D3D11_Shutdown1");
    using ShutdownBase=NVSDK_NGX_Result(NVSDK_CONV*)(); // Public export signature in the fixed OptiScaler source.
    auto shutdownBase=symbol<ShutdownBase>(module,"NVSDK_NGX_D3D11_Shutdown");
    const wchar_t* paths[]={sr.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};
    ngx(init("05882ed0-d4cd-4063-a726-0b4862bad3dc",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"Independent OptiScaler interface",out.c_str(),d.Get(),&info,NVSDK_NGX_Version_API),"Init");
    NVSDK_NGX_Parameter*p=nullptr;ngx(get(&p),"GetCapabilityParameters");need(p,"Null parameter object");
    int available=0;ngx(p->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available),"AvailableQuery");std::cout<<"AdvertisedAvailable="<<available<<std::endl;
    p->Set(NVSDK_NGX_Parameter_Width,W);p->Set(NVSDK_NGX_Parameter_Height,H);p->Set(NVSDK_NGX_Parameter_OutWidth,W);p->Set(NVSDK_NGX_Parameter_OutHeight,H);p->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_DLAA));
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));
    NVSDK_NGX_Handle* feature=nullptr;ngx(create(c.Get(),NVSDK_NGX_Feature_SuperSampling,p,&feature),"CreateFeature");need(feature,"Null feature");
    std::array<ComPtr<ID3D11Texture2D>,4> inputs;for(unsigned i=0;i<4;i++)inputs[i]=texture(d.Get(),data[i]);
    Raw seed{W,H,10,8,std::vector<unsigned char>(size_t(W)*H*8)};for(size_t i=0;i<seed.bytes.size();i+=2){seed.bytes[i]=0;seed.bytes[i+1]=0x7e;}auto output=texture(d.Get(),seed,true);
    const char*keys[]={NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_ExposureTexture};for(unsigned i=0;i<4;i++)p->Set(keys[i],static_cast<ID3D11Resource*>(inputs[i].Get()));p->Set(NVSDK_NGX_Parameter_Output,static_cast<ID3D11Resource*>(output.Get()));
    p->Set(NVSDK_NGX_Parameter_Reset,1);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);p->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.f);p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.f);p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,dt);p->Set(NVSDK_NGX_Parameter_Sharpness,0.f);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,W);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,H);
    std::cout<<"EvaluateAttempt actualDtMs="<<dt<<" P=1 uncalibrated jitter=0"<<std::endl;ngx(evaluate(c.Get(),feature,p,nullptr),"EvaluateFeature");c->Flush();
    const auto result=bytes(d.Get(),c.Get(),output.Get(),8);save(out/L"sr-output.raw",seed,result);
    for(unsigned i=0;i<4;i++){need(bytes(d.Get(),c.Get(),inputs[i].Get(),data[i].bpp)==data[i].bytes,"Model mutated source input");std::cout<<"InputExact index="<<i<<std::endl;}
    float max=0;size_t finite=0;for(size_t i=0;i<result.size();i+=8)for(unsigned k=0;k<4;k++){uint16_t v;std::memcpy(&v,result.data()+i+2*k,2);const float f=half(v);if(k<3){if(f>max)max=f;++finite;}}
    need(result!=data[0].bytes&&max>1,"No HDR model response");std::cout<<"OutputFiniteRGB="<<finite<<" max="<<max<<" nonidentity=1"<<std::endl;
    ComPtr<ID3D11InfoQueue>q;ck(d.As(&q));unsigned errors=0;for(UINT64 i=0;i<q->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;ck(q->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D11_MESSAGE*>(b.data());ck(q->GetMessage(i,m,&n));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
    std::cout<<"D3D11DebugErrors="<<errors<<std::endl;
    ngx(release(feature),"ReleaseFeature");ngx(destroy(p),"DestroyParameters");std::cout<<"ShutdownEnter variant="<<(baseShutdown?"base":"device")<<std::endl;ngx(baseShutdown?shutdownBase():shutdown(d.Get()),"Shutdown");need(errors==0,"D3D11 debug errors");
    // No FreeLibrary: package hooks/background work are process-global; hot unload is not qualified.
    std::ofstream(out/L"probe-complete.txt")<<"D3D11 public OptiScaler entry/output probe passed; inspect backend log before acceptance\n";std::filesystem::remove(out/L"INCOMPLETE");return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
