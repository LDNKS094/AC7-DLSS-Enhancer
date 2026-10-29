// Independent GPU probe: color-only by default; explicit DLL paths enable the model-chain mode.
#include "../../src/d3d12/hdr_proxy.h"
#include "../../src/d3d11/hdr_proxy.h"
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
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace {
constexpr unsigned W=2560,H=1440;
void need(bool b,const char*s){if(!b)throw std::runtime_error(s);}
void ck(HRESULT h){if(FAILED(h))throw std::runtime_error("Color probe HRESULT "+std::to_string(h));}
struct Raw {unsigned w,h,fmt,bpp;std::vector<unsigned char> data;};
Raw load(const fs::path&p,unsigned w,unsigned h,unsigned fmt,unsigned bpp){std::ifstream f(p,std::ios::binary|std::ios::ate);need(bool(f)&&f.tellg()==std::streamoff(16+size_t(w)*h*bpp),"Invalid raw length");f.seekg(0);unsigned v[4]{};f.read(reinterpret_cast<char*>(v),16);need(v[0]==w&&v[1]==h&&v[2]==fmt&&v[3]==bpp,"Invalid raw header");Raw r{w,h,fmt,bpp,std::vector<unsigned char>(size_t(w)*h*bpp)};f.read(reinterpret_cast<char*>(r.data.data()),r.data.size());need(bool(f),"Raw read failed");return r;}
float gain(const fs::path&p){const auto r=load(p,1,1,16,8);float v;std::memcpy(&v,r.data.data(),4);need(std::isfinite(v)&&v>=1.f/65504&&v<=65504,"Invalid actual exposure");return v;}
std::vector<unsigned char> crop(const Raw&r){need(r.w==5120&&r.h==H&&r.fmt==26&&r.bpp==4,"Unexpected native crop");std::vector<unsigned char> b(size_t(W)*H*4);for(unsigned y=0;y<H;y++)std::memcpy(b.data()+size_t(y)*W*4,r.data.data()+(size_t(y)*5120+1280)*4,W*4);return b;}
void save(const fs::path&p,unsigned fmt,unsigned bpp,const std::vector<unsigned char>&b){need(!fs::exists(p),"Refusing overwrite");std::ofstream f(p,std::ios::binary);unsigned v[]={W,H,fmt,bpp};f.write(reinterpret_cast<char*>(v),16);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();need(bool(f),"Write failed");}
void barrier(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
ComPtr<ID3D12Resource> buffer(ID3D12Device*d,UINT64 size,D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=size;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource>r;ck(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;}
struct Surface {
    ComPtr<ID3D12Resource> image,upload,readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT64 total=0;unsigned w,h,bpp;bool input;
    Surface(ID3D12Device*d,ID3D12Resource*r,bool source):image(r),input(source){const auto desc=r->GetDesc();w=unsigned(desc.Width);h=desc.Height;bpp=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;need(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||desc.Format==DXGI_FORMAT_R11G11B10_FLOAT||desc.Format==DXGI_FORMAT_R32_FLOAT||desc.Format==DXGI_FORMAT_R16G16_FLOAT,"Unsupported probe transfer format");d->GetCopyableFootprints(&desc,0,1,0,&layout,nullptr,nullptr,&total);readback=buffer(d,total,D3D12_HEAP_TYPE_READBACK);if(input)upload=buffer(d,total,D3D12_HEAP_TYPE_UPLOAD);}
    void put(ID3D12GraphicsCommandList*l,const Raw&r){need(r.w==w&&r.h==h&&r.bpp==bpp&&r.fmt==image->GetDesc().Format,"Upload mismatch");if(!upload){ComPtr<ID3D12Device>d;ck(image->GetDevice(IID_PPV_ARGS(&d)));upload=buffer(d.Get(),total,D3D12_HEAP_TYPE_UPLOAD);}void*p=nullptr;D3D12_RANGE empty{};ck(upload->Map(0,&empty,&p));for(unsigned y=0;y<h;y++)std::memcpy(static_cast<unsigned char*>(p)+layout.Offset+size_t(y)*layout.Footprint.RowPitch,r.data.data()+size_t(y)*w*bpp,size_t(w)*bpp);upload->Unmap(0,nullptr);const auto state=input?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON;barrier(l,image.Get(),state,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=image.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=layout;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(l,image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);}
    void take(ID3D12GraphicsCommandList*l){const auto state=input?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_COMMON;barrier(l,image.Get(),state,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layout;src.pResource=image.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(l,image.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,state);}
    std::vector<unsigned char> get(){void*p=nullptr;D3D12_RANGE range{0,SIZE_T(total)};ck(readback->Map(0,&range,&p));std::vector<unsigned char> b(size_t(w)*h*bpp);for(unsigned y=0;y<h;y++)std::memcpy(b.data()+size_t(y)*w*bpp,static_cast<unsigned char*>(p)+layout.Offset+size_t(y)*layout.Footprint.RowPitch,size_t(w)*bpp);D3D12_RANGE empty{};readback->Unmap(0,&empty);return b;}
};
ComPtr<ID3D12Resource> input12(ID3D12Device*d,unsigned w,unsigned fmt,unsigned height=H,bool writable=false){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=height;desc.Format=DXGI_FORMAT(fmt);desc.Flags=writable?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;ComPtr<ID3D12Resource>r;ck(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,writable?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&r)));return r;}
ComPtr<ID3D11Texture2D> input11(ID3D11Device*d,const Raw&r){D3D11_TEXTURE2D_DESC desc{};desc.Width=r.w;desc.Height=r.h;desc.Format=DXGI_FORMAT(r.fmt);desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{r.data.data(),r.w*r.bpp,0};ComPtr<ID3D11Texture2D>t;ck(d->CreateTexture2D(&desc,&data,&t));return t;}
std::vector<unsigned char> read11(ID3D11Device*d,ID3D11DeviceContext*c,ID3D11Texture2D*t,unsigned bpp){D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D>r;ck(d->CreateTexture2D(&desc,nullptr,&r));c->CopyResource(r.Get(),t);D3D11_MAPPED_SUBRESOURCE m{};ck(c->Map(r.Get(),0,D3D11_MAP_READ,0,&m));std::vector<unsigned char>b(size_t(desc.Width)*desc.Height*bpp);for(unsigned y=0;y<desc.Height;y++)std::memcpy(b.data()+size_t(y)*desc.Width*bpp,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(desc.Width)*bpp);c->Unmap(r.Get(),0);return b;}
void finish(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12GraphicsCommandList*l){ck(l->Close());ComPtr<ID3D12Fence>f;ck(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);need(event!=nullptr,"Fence event failed");ID3D12CommandList*lists[]={l};q->ExecuteCommandLists(1,lists);if(FAILED(q->Signal(f.Get(),1))||FAILED(f->SetEventOnCompletion(1,event))||WaitForSingleObject(event,10000)!=WAIT_OBJECT_0||f->GetCompletedValue()==UINT64_MAX){std::cerr<<"STOP standalone pending work\n";ExitProcess(10);}CloseHandle(event);}
}
#include "model_chain.inl"
#include "measure_chain.inl"
int wmain(int argc,wchar_t**argv)try{
    if(argc==7&&std::wstring(argv[6])==L"--measure-shared")return measureChain(argv);
    if(argc==6||argc==7){const std::wstring option=argc==7?argv[6]:L"";need(option.empty()||option==L"--sr-reset-control"||option==L"--shared-chain"||option==L"--shared-faults","Unknown chain option");return modelChain(argv,option==L"--sr-reset-control",option==L"--shared-chain"||option==L"--shared-faults",option==L"--shared-faults");}
    need(argc==4,"Usage: color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT");const fs::path source=argv[1],base=argv[2],out=argv[3];need(!fs::exists(out),"Refusing existing output");
    for(unsigned i=0;i<8;i++){const auto n=L"frame"+std::to_wstring(i);load(source/(n+L"-scene.raw"),5120,H,26,4);gain(source/(n+L"-exposure.raw"));load(base/L"nr-output"/n/L"nr-output.raw",W,H,10,8);load(base/L"nr-input"/n/L"color.raw",W,H,10,8);load(base/L"gpu-restore"/n/L"scene-restored.raw",5120,H,26,4);load(base/L"cpu-reference"/n/L"color.raw",W,H,10,8);load(base/L"sr-input"/n/L"color.raw",W,H,10,8);}
    fs::create_directory(out);std::ofstream(out/L"INCOMPLETE")<<"Color validation not complete\n";
    ComPtr<ID3D11Device>d11;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_0;ck(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,&fl,1,D3D11_SDK_VERSION,&d11,nullptr,&c));
    ComPtr<ID3D12Debug>debug;ck(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<ID3D12Debug1>gpuDebug;ck(debug.As(&gpuDebug));gpuDebug->SetEnableGPUBasedValidation(TRUE);
    ComPtr<IDXGIDevice>dxgi;ComPtr<IDXGIAdapter>adapter;ck(d11.As(&dxgi));ck(dxgi->GetAdapter(&adapter));ComPtr<ID3D12Device>d12;ck(D3D12CreateDevice(adapter.Get(),fl,IID_PPV_ARGS(&d12)));ComPtr<ID3D11InfoQueue>info11;ComPtr<ID3D12InfoQueue>info12;ck(d11.As(&info11));ck(d12.As(&info12));
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue>q;ck(d12->CreateCommandQueue(&desc,IID_PPV_ARGS(&q)));ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;ck(d12->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&a)));ck(d12->CreateCommandList(0,desc.Type,a.Get(),nullptr,IID_PPV_ARGS(&l)));ck(l->Close());unsigned refusals=0,pairRefusals=0;
    {
    ac7::HdrProxy ref(d11.Get(),W,H);ac7::HdrProxy12 codec(d12.Get(),W,H);auto hdr12=input12(d12.Get(),5120,26),nr12=input12(d12.Get(),W,10);Surface hdr(d12.Get(),hdr12.Get(),true),nr(d12.Get(),nr12.Get(),true),proxy(d12.Get(),codec.prepared(),false),restored(d12.Get(),codec.restored(),false),color(d12.Get(),codec.srColor(),false);
    std::cout<<"PersistentColor12 proxy="<<codec.prepared()<<" restored="<<codec.restored()<<" srColor="<<codec.srColor()<<std::endl;
    for(unsigned f=0;f<8;f++){
        const auto name=L"frame"+std::to_wstring(f);const auto dir=out/name;fs::create_directory(dir);const auto scene=load(source/(name+L"-scene.raw"),5120,H,26,4),model=load(base/L"nr-output"/name/L"nr-output.raw",W,H,10,8);const float exposure=gain(source/(name+L"-exposure.raw"));auto nativeCrop=crop(scene);auto scene11=input11(d11.Get(),scene),nr11=input11(d11.Get(),model);
        for(unsigned mode=0;mode<2;mode++){
            ref.record(c.Get(),scene11.Get(),{1280,0,W,H},exposure,mode?nr11.Get():nullptr);const auto proxy11=read11(d11.Get(),c.Get(),ref.prepared(),8),restore11=read11(d11.Get(),c.Get(),ref.restored(),4);
            need(proxy11==load(base/L"nr-input"/name/L"color.raw",W,H,10,8).data,"D11 proxy baseline mismatch");need(restore11==(mode?crop(load(base/L"gpu-restore"/name/L"scene-restored.raw",5120,H,26,4)):nativeCrop),"D11 restore baseline mismatch");
            ck(a->Reset());ck(l->Reset(a.Get(),nullptr));hdr.put(l.Get(),scene);nr.put(l.Get(),model);
            if(!mode)codec.record(l.Get(),hdr.image.Get(),{1280,0,W,H},exposure);
            else {
                if(f==0)try{codec.restore(l.Get(),hdr.image.Get(),{1280,0,W,H},exposure,nr.image.Get());}catch(const std::invalid_argument&){pairRefusals++;}
                codec.prepare(l.Get(),hdr.image.Get(),{1280,0,W,H},exposure);
                if(f==0){try{codec.restore(l.Get(),hdr.image.Get(),{1280,0,W,H},exposure*2,nr.image.Get());}catch(const std::invalid_argument&){pairRefusals++;}try{codec.prepare(l.Get(),hdr.image.Get(),{1280,0,W,H},exposure);}catch(const std::invalid_argument&){pairRefusals++;}}
                codec.restore(l.Get(),hdr.image.Get(),{1280,0,W,H},exposure,nr.image.Get());
            }
            for(auto*s:{&proxy,&restored,&color,&hdr,&nr})s->take(l.Get());finish(d12.Get(),q.Get(),l.Get());
            const auto p=proxy.get(),r=restored.get(),s=color.get();need(p==proxy11&&r==restore11,"D12 color differs from D11");need(s==load(base/(mode?L"sr-input":L"cpu-reference")/name/L"color.raw",W,H,10,8).data,"D12 linear FP16 mismatch");need(hdr.get()==scene.data&&nr.get()==model.data,"D12 input mutated");
            if(!mode)save(dir/L"proxy.raw",10,8,p);save(dir/(mode?L"restored.raw":L"neutral.raw"),26,4,r);save(dir/(mode?L"sr-input.raw":L"neutral-sr.raw"),10,8,s);
            std::cout<<"ColorExact frame="<<f<<" mode="<<(mode?"actual-nr-fixture":"neutral")<<" gain="<<exposure<<" stages=3 inputUnchanged=2"<<std::endl;
        }
    }
    need(pairRefusals==3,"Invalid preparation pairing accepted");
    for(unsigned test=0;test<6;test++)try{codec.record(test==0?nullptr:l.Get(),hdr.image.Get(),test==2?ac7::ViewRect{2561,0,W,H}:ac7::ViewRect{1280,0,W,H},test==1?NAN:test==5?0.f:1.f,test==3?hdr.image.Get():test==4?codec.prepared():nullptr);}catch(const std::invalid_argument&){refusals++;}need(refusals==6,"Invalid record accepted");
    auto scene=load(source/L"frame0-scene.raw",5120,H,26,4),model=load(base/L"nr-output/frame0/nr-output.raw",W,H,10,8);const uint16_t poison[]={0x7e00,0xbc00,0x4000};for(unsigned i=0;i<3;i++)std::memcpy(model.data.data()+i*8,&poison[i],2);ck(a->Reset());ck(l->Reset(a.Get(),nullptr));hdr.put(l.Get(),scene);nr.put(l.Get(),model);codec.record(l.Get(),hdr.image.Get(),{1280,0,W,H},gain(source/L"frame0-exposure.raw"),nr.image.Get());restored.take(l.Get());finish(d12.Get(),q.Get(),l.Get());const auto guarded=restored.get(),original=crop(scene);for(unsigned i=0;i<3;i++)need(std::memcmp(guarded.data()+i*4,original.data()+i*4,4)==0,"Invalid model pixel changed original");save(out/L"invalid-model.raw",26,4,guarded);
    c->ClearState();c->Flush();
    }
    unsigned errors=0;for(UINT64 i=0;i<info11->GetNumStoredMessages();i++){SIZE_T n=0;ck(info11->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D11_MESSAGE*>(b.data());ck(info11->GetMessage(i,m,&n));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}for(UINT64 i=0;i<info12->GetNumStoredMessages();i++){SIZE_T n=0;ck(info12->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto*m=reinterpret_cast<D3D12_MESSAGE*>(b.data());ck(info12->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}need(errors==0,"Color API debug errors");ck(d11->GetDeviceRemovedReason());ck(d12->GetDeviceRemovedReason());
    std::ofstream report(out/L"checks.json");report<<"{\"kind\":\"D3D12_COLOR_EQUIVALENCE\",\"frames\":8,\"exactStages\":48,\"inputReadbacksExact\":32,\"refusals\":"<<refusals<<",\"pairRefusals\":"<<pairRefusals<<",\"invalidModelPixelsPreserved\":3,\"bothDebugErrors\":0,\"gpuBasedValidation\":true,\"nrEvaluations\":0,\"srEvaluations\":0,\"gameIntegration\":false,\"performanceAccepted\":false}";report.close();need(bool(report),"Report write failed");fs::remove(out/L"INCOMPLETE");std::cout<<"PASS Color48/Inputs32/Refusals6/PairRefusals3/InvalidPixels3/Debug0 NR0 SR0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
