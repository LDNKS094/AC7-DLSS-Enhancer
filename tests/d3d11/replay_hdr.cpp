// Offline replay diagnostic: optional precomputed NR readback; no live hooks or interop.
#include "hdr_proxy.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>
namespace {
void check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("HDR replay resource failure");}
std::vector<unsigned char> readModelRaw(const char* path) {
    std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    const size_t bytes=size_t(2560)*1440*8;
    if(!file||file.tellg()!=static_cast<std::streamoff>(bytes+16))throw std::runtime_error("Invalid NR raw length");
    unsigned header[4]{};file.seekg(0);file.read(reinterpret_cast<char*>(header),16);
    if(header[0]!=2560||header[1]!=1440||header[2]!=10||header[3]!=8)throw std::runtime_error("Invalid NR raw header");
    std::vector<unsigned char> result(bytes);file.read(reinterpret_cast<char*>(result.data()),bytes);if(!file)throw std::runtime_error("NR raw read failed");return result;
}
void dumpPrepared(ID3D11DeviceContext* context,ID3D11Texture2D* texture,const char* path) {
    if(std::filesystem::exists(path))throw std::runtime_error("Refusing to overwrite GPU proxy dump");
    Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging));context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
    std::ofstream file(std::filesystem::path(path),std::ios::binary);unsigned header[]={desc.Width,desc.Height,static_cast<unsigned>(desc.Format),8};file.write(reinterpret_cast<char*>(header),16);
    for(unsigned y=0;y<desc.Height;++y)file.write(static_cast<char*>(map.pData)+size_t(y)*map.RowPitch,desc.Width*8);
    context->Unmap(staging.Get(),0);file.close();if(!file)throw std::runtime_error("GPU proxy dump failed");
}
void verifyPrepared(ID3D11DeviceContext* context,ID3D11Texture2D* texture,const std::vector<unsigned char>& expected) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging));context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));bool equal=true;
    for(unsigned y=0;y<1440;++y)equal&=std::memcmp(static_cast<unsigned char*>(map.pData)+size_t(y)*map.RowPitch,expected.data()+size_t(y)*2560*8,2560*8)==0;
    context->Unmap(staging.Get(),0);if(!equal)throw std::runtime_error("Replay GPU proxy differs from actual NR input; refusing writeback");
}
}
void ValidateHdrReplay(ID3D11DeviceContext* context,ID3D11Texture2D* scene) {
    const char* path=std::getenv("AC7_HDR_EXPOSURE");if(!path)throw std::runtime_error("Missing explicit captured exposure");
    std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=24)throw std::runtime_error("Invalid exposure raw length");
    unsigned header[4]{};float gain=0;file.seekg(0);file.read(reinterpret_cast<char*>(header),16);file.read(reinterpret_cast<char*>(&gain),4);
    if(!file||header[0]!=1||header[1]!=1||header[2]!=16||header[3]!=8)throw std::runtime_error("Invalid exposure raw header");
    D3D11_TEXTURE2D_DESC desc{};scene->GetDesc(&desc);
    if(desc.Width!=5120||desc.Height!=1440||desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT)throw std::runtime_error("Unexpected captured HDR scene");
    Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> neural;
    std::vector<unsigned char> expected;
    if(const char* model=std::getenv("AC7_HDR_NEURAL")) {
        const char* proxy=std::getenv("AC7_HDR_EXPECTED_PROXY");if(!proxy)throw std::runtime_error("NR writeback requires exact input-proxy verification");
        const auto bytes=readModelRaw(model);expected=readModelRaw(proxy);
        D3D11_TEXTURE2D_DESC d{};d.Width=2560;d.Height=1440;d.ArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{bytes.data(),2560*8,0};check(device->CreateTexture2D(&d,&init,&neural));
    } else if(std::getenv("AC7_HDR_EXPECTED_PROXY"))throw std::runtime_error("Unexpected proxy verification without NR output");
    ac7::HdrProxy codec(device.Get(),2560,1440);
    codec.record(context,scene,{1280,0,2560,1440},gain,neural.Get());
    if(neural)verifyPrepared(context,codec.prepared(),expected);
    if(const char* dump=std::getenv("AC7_HDR_DUMP_PREPARED"))dumpPrepared(context,codec.prepared(),dump);
    D3D11_BOX region{0,0,0,2560,1440,1};
    context->CopySubresourceRegion(scene,0,1280,0,0,codec.restored(),0,&region);
    check(device->GetDeviceRemovedReason());
}
