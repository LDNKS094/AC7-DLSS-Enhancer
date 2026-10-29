// Standalone hardware test of two narrow color passes; no NGX or game injection.
#include "hdr_proxy.h"
#include <d3d11sdklayers.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;
namespace fs=std::filesystem;
namespace {
void check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("GPU validation HRESULT "+std::to_string(static_cast<unsigned>(hr)));}
std::vector<unsigned char> read(const fs::path& p,DXGI_FORMAT format,unsigned bpp) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);const size_t bytes=size_t(2560)*1440*bpp;
    if(!f||f.tellg()!=static_cast<std::streamoff>(bytes+16))throw std::runtime_error("Invalid test raw length");
    unsigned header[4]{};f.seekg(0);f.read(reinterpret_cast<char*>(header),16);
    if(header[0]!=2560||header[1]!=1440||header[2]!=format||header[3]!=bpp)throw std::runtime_error("Invalid test raw header");
    std::vector<unsigned char> result(bytes);f.read(reinterpret_cast<char*>(result.data()),bytes);if(!f)throw std::runtime_error("Test read failed");return result;
}
std::vector<unsigned char> dump(ID3D11DeviceContext* context,ID3D11Texture2D* texture,unsigned bpp,const fs::path& file) {
    if(fs::exists(file))throw std::runtime_error("Refusing to overwrite GPU evidence");
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);const auto format=d.Format;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=d.MiscFlags=0;
    ComPtr<ID3D11Device> device;context->GetDevice(&device);ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging));context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));std::vector<unsigned char> bytes(size_t(d.Width)*d.Height*bpp);
    for(unsigned y=0;y<d.Height;++y)std::memcpy(bytes.data()+size_t(y)*d.Width*bpp,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(d.Width)*bpp);context->Unmap(staging.Get(),0);
    std::ofstream f(file,std::ios::binary);unsigned header[]={d.Width,d.Height,static_cast<unsigned>(format),bpp};f.write(reinterpret_cast<char*>(header),16);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();if(!f)throw std::runtime_error("Dump write failed");return bytes;
}
}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=4)throw std::runtime_error("Usage: validate-hdr-gpu PREPARED_HDR_DIR NR_EVALUATION_DIR NEW_OUTPUT_DIR");
    fs::path source=argv[1],model=argv[2],out=argv[3];if(fs::exists(out))throw std::runtime_error("New output directory required");
    const auto original=read(source/L"original-hdr.raw",DXGI_FORMAT_R11G11B10_FLOAT,4),proxy=read(source/L"color.raw",DXGI_FORMAT_R16G16B16A16_FLOAT,8),neural=read(model/L"nr-output.raw",DXGI_FORMAT_R16G16B16A16_FLOAT,8),cpu=read(model/L"hdr-restored.raw",DXGI_FORMAT_R16G16B16A16_FLOAT,8);
    std::ifstream ef(source/L"exposure.raw",std::ios::binary|std::ios::ate);if(!ef||ef.tellg()!=24)throw std::runtime_error("Invalid exposure file");ef.seekg(0);unsigned eh[4]{};ef.read(reinterpret_cast<char*>(eh),16);if(eh[0]!=1||eh[1]!=1||eh[2]!=16||eh[3]!=8)throw std::runtime_error("Invalid exposure layout");float exposure=0;ef.read(reinterpret_cast<char*>(&exposure),4);if(!ef)throw std::runtime_error("Exposure read failed");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
    ComPtr<ID3D11InfoQueue> info;check(device.As(&info));
    auto texture=[&](DXGI_FORMAT format,unsigned bpp,const std::vector<unsigned char>& bytes) {
        D3D11_TEXTURE2D_DESC d{};d.Width=2560;d.Height=1440;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{bytes.data(),2560*bpp,0};ComPtr<ID3D11Texture2D> result;check(device->CreateTexture2D(&d,&init,&result));return result;
    };
    const auto hdr=texture(DXGI_FORMAT_R11G11B10_FLOAT,4,original),nr=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,8,neural);
    ac7::HdrProxy codec(device.Get(),2560,1440);fs::create_directories(out);
    D3D11_VIEWPORT viewport{17,23,100,200,0,1};context->RSSetViewports(1,&viewport);
    codec.record(context.Get(),hdr.Get(),{0,0,2560,1440},exposure);
    const auto gpuProxy=dump(context.Get(),codec.prepared(),8,out/L"prepared.raw");const auto neutral=dump(context.Get(),codec.restored(),4,out/L"neutral.raw");
    if(neutral!=original)throw std::runtime_error("GPU neutral HDR roundtrip is not bit-exact");
    UINT count=1;D3D11_VIEWPORT restored{};context->RSGetViewports(&count,&restored);if(count!=1||std::memcmp(&restored,&viewport,sizeof(viewport)))throw std::runtime_error("Viewport state restoration failed");
    // Refusals happen before modifying device context state.
    unsigned refused=0;for(unsigned c=0;c<3;++c)try {codec.record(context.Get(),hdr.Get(),c==0?ac7::ViewRect{1,0,2560,1440}:ac7::ViewRect{0,0,2560,1440},c==1?NAN:exposure,c==2?hdr.Get():nullptr);}catch(const std::invalid_argument&){++refused;}
    if(refused!=3)throw std::runtime_error("Invalid HDR inputs were accepted");
    codec.record(context.Get(),hdr.Get(),{0,0,2560,1440},exposure,nr.Get());const auto result=dump(context.Get(),codec.restored(),4,out/L"restored.raw");
    double maxProxy=0,maxHdr=0;size_t proxyDifferent=0,packedDifferent=0;
    for(size_t i=0;i<size_t(2560)*1440;++i) {
        HALF a[4],b[4];std::memcpy(a,gpuProxy.data()+i*8,8);std::memcpy(b,proxy.data()+i*8,8);
        for(unsigned c=0;c<3;++c) {const float x=XMConvertHalfToFloat(a[c]),y=XMConvertHalfToFloat(b[c]);if(!std::isfinite(x))throw std::runtime_error("Nonfinite GPU proxy");maxProxy=std::max(maxProxy,double(std::abs(x-y)));proxyDifferent+=a[c]!=b[c];}
        unsigned word;std::memcpy(&word,result.data()+i*4,4);XMFLOAT3PK p(word);XMFLOAT3 decoded;XMStoreFloat3(&decoded,XMLoadFloat3PK(&p));const float actual[]={decoded.x,decoded.y,decoded.z};std::memcpy(b,cpu.data()+i*8,8);XMFLOAT3PK expected;XMStoreFloat3PK(&expected,XMVectorSet(XMConvertHalfToFloat(b[0]),XMConvertHalfToFloat(b[1]),XMConvertHalfToFloat(b[2]),0));packedDifferent+=word!=expected.v;
        for(unsigned c=0;c<3;++c) {const float target=XMConvertHalfToFloat(b[c]);if(!std::isfinite(actual[c]))throw std::runtime_error("Nonfinite restored GPU HDR");maxHdr=std::max(maxHdr,double(std::abs(actual[c]-target)));if(std::abs(actual[c]-target)>std::max(.002f,target*.035f))throw std::runtime_error("GPU HDR exceeds CPU/packed-format tolerance");}
    }
    auto poisoned=neural;poisoned[0]=0;poisoned[1]=0x7e;
    context->UpdateSubresource(nr.Get(),0,nullptr,poisoned.data(),2560*8,0);
    codec.record(context.Get(),hdr.Get(),{0,0,2560,1440},exposure,nr.Get());
    const auto guarded=dump(context.Get(),codec.restored(),4,out/L"invalid-neural.raw");
    if(std::memcmp(guarded.data(),original.data(),4))throw std::runtime_error("Invalid model pixel did not preserve original HDR");
    unsigned errors=0;for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {SIZE_T bytes=0;check(info->GetMessage(i,nullptr,&bytes));std::vector<unsigned char> storage(bytes);auto* m=reinterpret_cast<D3D11_MESSAGE*>(storage.data());check(info->GetMessage(i,m,&bytes));if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}
    check(device->GetDeviceRemovedReason());if(errors||maxProxy>0.001)throw std::runtime_error("GPU validation errors");
    std::ofstream report(out/L"checks.json");report<<"{\"neutralBitExact\":true,\"viewportRestored\":true,\"invalidModelPixelPreserved\":true,\"invalidInputsRefused\":"<<refused<<",\"debugErrors\":"<<errors<<",\"proxyDifferentChannels\":"<<proxyDifferent<<",\"maxProxyDifference\":"<<maxProxy<<",\"packedHdrDifferentFromCpu\":"<<packedDifferent<<",\"maxHdrDifference\":"<<maxHdr<<"}\n";report.close();if(!report)throw std::runtime_error("Report write failed");
    std::cout<<"PASS GPU neutral bit-exact, state/refusals/debug, proxy max difference="<<maxProxy<<", HDR max difference="<<maxHdr<<", packed differences="<<packedDifferent<<'\n';return 0;
} catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
