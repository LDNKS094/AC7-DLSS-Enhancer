// Offline packed SR handoff after NR restoration; no shader or color-scale changes.
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("SR replay resource failure");}
std::vector<unsigned char> read(const char* path,unsigned width){
    std::ifstream f(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    const size_t size=size_t(width)*1440*4;
    if(!f||f.tellg()!=static_cast<std::streamoff>(size+16))throw std::runtime_error("Invalid SR handoff raw length");
    std::array<unsigned,4> header{};f.seekg(0);f.read(reinterpret_cast<char*>(header.data()),16);
    if(header!=std::array<unsigned,4>{width,1440,26,4})throw std::runtime_error("Invalid SR handoff raw header");
    std::vector<unsigned char> bytes(size);f.read(reinterpret_cast<char*>(bytes.data()),size);if(!f)throw std::runtime_error("SR handoff read failed");return bytes;
}
}
void ValidateFxaaReplay(ID3D11DeviceContext* context,ID3D11Texture2D* input,ID3D11Texture2D* output){
    const char* bypass=std::getenv("AC7_SR_BYPASS_FXAA");if(!bypass)return;
    if(std::strcmp(bypass,"1")!=0)throw std::runtime_error("FXAA bypass requires explicit value1");
    D3D11_TEXTURE2D_DESC a{},b{};input->GetDesc(&a);output->GetDesc(&b);
    if(input==output||a.Width!=5120||a.Height!=1440||a.Format!=DXGI_FORMAT_B8G8R8A8_TYPELESS||a.MipLevels!=1||a.ArraySize!=1||a.SampleDesc.Count!=1||a.SampleDesc.Quality||b.Width!=a.Width||b.Height!=a.Height||b.Format!=a.Format||b.MipLevels!=1||b.ArraySize!=1||b.SampleDesc.Count!=1||b.SampleDesc.Quality)throw std::runtime_error("Unexpected FXAA input/output layouts");
    Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
    std::array<std::vector<unsigned char>,2> bytes;
    ID3D11Texture2D* textures[]={input,output};
    a.Usage=D3D11_USAGE_STAGING;a.BindFlags=a.MiscFlags=0;a.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    for(unsigned i=0;i<2;++i){
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&a,nullptr,&staging));context->CopyResource(staging.Get(),textures[i]);
        bytes[i].resize(size_t(5120)*1440*4);D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
        for(unsigned y=0;y<1440;++y)std::memcpy(bytes[i].data()+size_t(y)*5120*4,static_cast<unsigned char*>(map.pData)+size_t(y)*map.RowPitch,5120*4);
        context->Unmap(staging.Get(),0);
    }
    // Tonemap alpha carries luminance; preserve the downstream FXAA alpha contract and padding.
    for(unsigned y=0;y<1440;++y)for(unsigned x=1280;x<3840;++x){const size_t at=(size_t(y)*5120+x)*4;std::memcpy(bytes[1].data()+at,bytes[0].data()+at,3);}
    context->UpdateSubresource(output,0,nullptr,bytes[1].data(),5120*4,0);check(device->GetDeviceRemovedReason());
    std::cout<<"FXAA effect bypass: exact active RGB; original alpha/padding preserved; draw cost and CPU staging still present"<<std::endl;
}
void ValidateSrReplay(ID3D11DeviceContext* context,ID3D11Texture2D* scene){
    const char* expectedFile=std::getenv("AC7_SR_EXPECTED_SCENE"),*resultFile=std::getenv("AC7_SR_PACKED");
    if(!expectedFile||!resultFile)throw std::runtime_error("SR handoff requires explicit expected scene and packed output");
    const auto expected=read(expectedFile,5120),result=read(resultFile,2560);
    D3D11_TEXTURE2D_DESC desc{};scene->GetDesc(&desc);
    if(desc.Width!=5120||desc.Height!=1440||desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT||desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1)throw std::runtime_error("Unexpected SR scene layout");
    Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&desc,nullptr,&staging));context->CopyResource(staging.Get(),scene);
    D3D11_MAPPED_SUBRESOURCE map{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));bool equal=true;
    for(unsigned y=0;y<1440;++y)equal&=std::memcmp(static_cast<unsigned char*>(map.pData)+size_t(y)*map.RowPitch,expected.data()+size_t(y)*5120*4,5120*4)==0;
    context->Unmap(staging.Get(),0);if(!equal)throw std::runtime_error("SR expected full scene mismatch; refusing writeback");
    desc.Width=2560;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
    D3D11_SUBRESOURCE_DATA init{result.data(),2560*4,0};Microsoft::WRL::ComPtr<ID3D11Texture2D> upload;
    check(device->CreateTexture2D(&desc,&init,&upload));D3D11_BOX box{0,0,0,2560,1440,1};context->CopySubresourceRegion(scene,0,1280,0,0,upload.Get(),0,&box);
    check(device->GetDeviceRemovedReason());std::cout<<"SR handoff: full source bit-exact; packed active rect copied; P1 assumption; offline only"<<std::endl;
}
