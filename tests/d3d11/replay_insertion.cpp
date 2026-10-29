// Offline capture replay only. No injection, game hooks, or model calls.
#include "guide_normalizer.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr) { if(FAILED(hr))throw std::runtime_error("Replay insertion HRESULT "+std::to_string(hr)); }
void dump(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const char* name, unsigned bpp) {
    const char* out=std::getenv("AC7_REPLAY_DUMP_DIR");if(!out)throw std::runtime_error("Missing replay dump directory");
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    D3D11_TEXTURE2D_DESC d;texture->GetDesc(&d);auto width=d.Width,height=d.Height;auto format=d.Format;
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=d.MiscFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging));context->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map;check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
    std::ofstream file(std::filesystem::path(out)/name,std::ios::binary);uint32_t header[]{width,height,uint32_t(format),bpp};file.write(reinterpret_cast<char*>(header),16);
    for(unsigned y=0;y<height;y++)file.write(static_cast<char*>(map.pData)+y*map.RowPitch,width*bpp);
    bool good=file.good();context->Unmap(staging.Get(),0);if(!good)throw std::runtime_error("Replay readback write failed");
}
}
void ValidateReplayInsertion(ID3D11DeviceContext* context, ID3D11Texture2D* scene, ID3D11Resource* depth, ID3D11Texture2D* motion) {
    const char* constants=std::getenv("AC7_GUIDE_PARAMETERS");if(!constants)throw std::runtime_error("Missing guide parameters file");
    struct Parameters {ac7::ViewRect rect;ac7::ClipToPrevious matrix;} p;
    std::ifstream input(constants,std::ios::binary);input.read(reinterpret_cast<char*>(&p),sizeof(p));
    if(!input||input.peek()!=EOF)throw std::runtime_error("Invalid replay guide parameters");
    ComPtr<ID3D11Device> device;context->GetDevice(&device);
    ComPtr<ID3D11Texture2D> depthTexture;check(depth->QueryInterface(IID_PPV_ARGS(&depthTexture)));
    ac7::GuideNormalizer normalizer(device.Get(),p.rect.width,p.rect.height);
    normalizer.record(context,depthTexture.Get(),motion,p.rect,p.matrix);
    dump(context,normalizer.motion(),"insertion-motion.raw",8);
    dump(context,normalizer.depth(),"insertion-depth.raw",4);
    dump(context,normalizer.validity(),"insertion-validity.raw",4);
    // Same-size identity handoff: CopyResource preserves the entire allocation, including padding.
    D3D11_TEXTURE2D_DESC desc;scene->GetDesc(&desc);
    if(desc.Format!=DXGI_FORMAT_B8G8R8A8_TYPELESS||desc.SampleDesc.Count!=1||desc.MipLevels!=1||desc.ArraySize!=1)
        throw std::runtime_error("Unexpected scene handoff layout");
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=desc.CPUAccessFlags=desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> snapshot;check(device->CreateTexture2D(&desc,nullptr,&snapshot));
    context->CopyResource(snapshot.Get(),scene);context->CopyResource(scene,snapshot.Get());
    check(device->GetDeviceRemovedReason());
}
