// Offline GPU conversion of two preserved live frames; no hooks, NGX or game writes.
#include "hdr_proxy.h"
#include <d3d11sdklayers.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <string>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT h) { if (FAILED(h)) throw std::runtime_error("GPU pair HRESULT " + std::to_string(h)); }
std::vector<unsigned char> read(const std::filesystem::path& p, unsigned w, unsigned h, unsigned fmt, unsigned bpp) {
    std::ifstream f(p, std::ios::binary|std::ios::ate);
    if (!f || f.tellg()!=std::streamoff(16+size_t(w)*h*bpp)) throw std::runtime_error("Unexpected raw length: "+p.string());
    f.seekg(0); unsigned header[4]{};f.read(reinterpret_cast<char*>(header),16);
    if(header[0]!=w||header[1]!=h||header[2]!=fmt||header[3]!=bpp) throw std::runtime_error("Unexpected raw layout");
    std::vector<unsigned char> b(size_t(w)*h*bpp);f.read(reinterpret_cast<char*>(b.data()),b.size());if(!f)throw std::runtime_error("Read failed");return b;
}
ComPtr<ID3D11Texture2D> texture(ID3D11Device* d,const std::vector<unsigned char>& b,unsigned w,unsigned h,DXGI_FORMAT fmt,unsigned bpp) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=fmt;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{b.data(),w*bpp,0};ComPtr<ID3D11Texture2D> t;check(d->CreateTexture2D(&desc,&init,&t));return t;
}
std::vector<unsigned char> dump(ID3D11DeviceContext* c,ID3D11Texture2D* t,unsigned bpp,const std::filesystem::path& p) {
    if(std::filesystem::exists(p))throw std::runtime_error("Refusing existing output");
    ComPtr<ID3D11Device> device;c->GetDevice(&device);D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);
    desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging));c->CopyResource(staging.Get(),t);
    D3D11_MAPPED_SUBRESOURCE m{};check(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<unsigned char> b(size_t(desc.Width)*desc.Height*bpp);
    for(unsigned y=0;y<desc.Height;y++)std::memcpy(b.data()+size_t(y)*desc.Width*bpp,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(desc.Width)*bpp);
    c->Unmap(staging.Get(),0);std::ofstream f(p,std::ios::binary);unsigned header[]={desc.Width,desc.Height,unsigned(desc.Format),bpp};f.write(reinterpret_cast<char*>(header),16);f.write(reinterpret_cast<char*>(b.data()),b.size());f.close();if(!f)throw std::runtime_error("Dump write failed");return b;
}
}
int wmain(int argc,wchar_t** argv) try {
    const bool eight=argc>1 && std::wstring(argv[argc-1])==L"--eight";
    const int baseArgc=argc-(eight?1:0);
    const unsigned frameCount=eight?8:2;
    const bool restore=baseArgc==6&&std::wstring(argv[1])==L"restore";
    if(!restore&&!(baseArgc==4&&std::wstring(argv[1])==L"prepare"))throw std::runtime_error("Usage: gpu-live-pair prepare LIVE_PAIR NEW_OUTPUT [--eight] | restore LIVE_PAIR NEW_OUTPUT NR_PAIR_OUTPUT EXPECTED_GPU_PROXY_PAIR [--eight]");
    const std::filesystem::path input(argv[2]),output(argv[3]);
    if(std::filesystem::exists(output))throw std::runtime_error("Refusing prior output directory");
    if(!std::filesystem::exists(input/L"capture.json")||std::filesystem::exists(input/L"INCOMPLETE"))throw std::runtime_error("Incomplete source capture");
    std::filesystem::create_directories(output);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx));
    ac7::HdrProxy codec(device.Get(),2560,1440);ac7::GuideNormalizer normalizer(device.Get(),2560,1440);
    for(unsigned frame=0;frame<frameCount;frame++) {
        const auto prefix=L"frame"+std::to_wstring(frame);const auto dest=output/prefix;std::filesystem::create_directory(dest);
        const auto original=read(input/(prefix+L"-scene.raw"),5120,1440,26,4);
        auto scene=texture(device.Get(),original,5120,1440,DXGI_FORMAT_R11G11B10_FLOAT,4);
        const auto exp=read(input/(prefix+L"-exposure.raw"),1,1,16,8);float gain;std::memcpy(&gain,exp.data(),4);
        std::ifstream vf(input/(prefix+L"-view.bin"),std::ios::binary|std::ios::ate);if(!vf||vf.tellg()!=4096)throw std::runtime_error("Unexpected View");
        std::vector<unsigned char> view(4096);vf.seekg(0);vf.read(reinterpret_cast<char*>(view.data()),4096);if(!vf)throw std::runtime_error("View read failed");
        float rect[4]{},alloc[2]{};std::memcpy(rect,view.data()+126*16,8);std::memcpy(rect+2,view.data()+127*16,8);std::memcpy(alloc,view.data()+128*16,8);
        if(rect[0]!=1280||rect[1]!=0||rect[2]!=2560||rect[3]!=1440||alloc[0]!=5120||alloc[1]!=1440)throw std::runtime_error("View rect/size mismatch");
        for(unsigned i=0;i<4;i++){float v;std::memcpy(&v,view.data()+114*16+i*4,4);if(v!=0)throw std::runtime_error("Only actual zero jitter supported");}
        ComPtr<ID3D11Texture2D> neural;
        std::vector<unsigned char> expected;
        if(restore) {
            expected=read(std::filesystem::path(argv[5])/prefix/L"proxy.raw",2560,1440,10,8);
            neural=texture(device.Get(),read(std::filesystem::path(argv[4])/prefix/L"nr-output.raw",2560,1440,10,8),2560,1440,DXGI_FORMAT_R16G16B16A16_FLOAT,8);
        }
        codec.record(ctx.Get(),scene.Get(),{1280,0,2560,1440},gain,neural.Get());
        const auto proxy=dump(ctx.Get(),codec.prepared(),8,dest/L"proxy.raw");
        if(restore && proxy!=expected)throw std::runtime_error("GPU proxy differs from actual NR source; refusing restored scene publication");
        D3D11_BOX box{0,0,0,2560,1440,1};ctx->CopySubresourceRegion(scene.Get(),0,1280,0,0,codec.restored(),0,&box);
        const auto result=dump(ctx.Get(),scene.Get(),4,dest/L"scene-restored.raw");
        if(!restore && result!=original)throw std::runtime_error("Neutral HDR roundtrip not exact");
        for(unsigned y=0;y<1440;y++)for(unsigned x:{0u,3840u}) {
            const size_t at=(size_t(y)*5120+x)*4;if(std::memcmp(result.data()+at,original.data()+at,1280*4))throw std::runtime_error("Outside View rect changed");
        }
        if(!restore) {
            auto depth=texture(device.Get(),read(input/(prefix+L"-depth.raw"),5120,1440,19,8),5120,1440,DXGI_FORMAT_R32G8X24_TYPELESS,8);
            auto motion=texture(device.Get(),read(input/(prefix+L"-velocity.raw"),5120,1440,35,4),5120,1440,DXGI_FORMAT_R16G16_UNORM,4);
            ac7::ClipToPrevious transform;std::memcpy(transform.data(),view.data()+110*16,64);
            normalizer.record(ctx.Get(),depth.Get(),motion.Get(),{1280,0,2560,1440},transform);
            dump(ctx.Get(),normalizer.depth(),4,dest/L"depth.raw");dump(ctx.Get(),normalizer.motion(),8,dest/L"motion-ndc.raw");
            const auto mask=dump(ctx.Get(),normalizer.validity(),4,dest/L"validity.raw");for(size_t i=0;i<mask.size();i+=4){unsigned v;std::memcpy(&v,mask.data()+i,4);if(v!=1&&v!=2)throw std::runtime_error("Invalid normalized guide pixels");}
        }
        std::cout<<"Frame="<<frame<<" exposure="<<gain<<" proxy prepared; "<<(restore?"paired NR restored; outside rect exact":"neutral full scene exact; GPU guides valid")<<std::endl;
    }
    ComPtr<ID3D11InfoQueue> debug;check(device.As(&debug));unsigned errors=0;
    for(UINT64 i=0;i<debug->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;check(debug->GetMessage(i,nullptr,&n));std::vector<unsigned char>b(n);auto* m=reinterpret_cast<D3D11_MESSAGE*>(b.data());check(debug->GetMessage(i,m,&n));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){errors++;std::cerr<<m->pDescription<<std::endl;}}
    if(errors)throw std::runtime_error("D3D11 debug errors");check(device->GetDeviceRemovedReason());std::ofstream(output/L"complete.txt")<<frameCount<<" frames; D3D11 debug errors=0; "<<(restore?"paired active restore":"neutral exact and guides valid")<<'\n';
    std::cout<<"PASS "<<frameCount<<" frames; D3D11DebugErrors=0"<<std::endl;return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
