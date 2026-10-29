// Readback checkpoints for the 2026-10-27 07:41:15 capture only.
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include "Resources.h"
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Replay readback HRESULT " + std::to_string(hr)); }
static void Dump(const char* name, ID3D11Resource* resource) {
    const char* directory = std::getenv("AC7_REPLAY_DUMP_DIR");
    if (!directory || !resource) throw std::runtime_error("Missing readback directory or resource");
    ComPtr<ID3D11Texture2D> texture;
    Check(resource->QueryInterface(IID_PPV_ARGS(&texture)));
    D3D11_TEXTURE2D_DESC desc; texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1) throw std::runtime_error("Readback requires single sample");
    uint32_t bpp = (desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS || desc.Format == DXGI_FORMAT_R32G32_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
    if (desc.Format != DXGI_FORMAT_R32G8X24_TYPELESS && desc.Format != DXGI_FORMAT_R32G32_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format != DXGI_FORMAT_R16G16_UNORM && desc.Format != DXGI_FORMAT_R11G11B10_FLOAT && desc.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS && desc.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS && desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) throw std::runtime_error("Unsupported readback format");
    ComPtr<ID3D11Device> device; texture->GetDevice(&device);
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    auto stagingDesc = desc; stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0; stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging; Check(device->CreateTexture2D(&stagingDesc, nullptr, &staging));
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped; Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    std::ofstream file(std::string(directory) + "/" + name + ".raw", std::ios::binary);
    uint32_t header[] = { desc.Width, desc.Height, static_cast<uint32_t>(desc.Format), bpp };
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (UINT y = 0; y < desc.Height; y++) file.write(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * bpp);
    const bool ok = file.good(); context->Unmap(staging.Get(), 0);
    if (!ok) throw std::runtime_error("Readback file write failed");
}
void CaptureCheckpoint(int draw) {
    if (draw == 1401) {
        Dump("1401-scene-hdr", pID3D11Texture2D_uid_1652);
        Dump("1401-depth", pID3D11Texture2D_uid_1548);
        Dump("1401-velocity", pID3D11Texture2D_uid_1698);
    }
    if (draw == 1427) Dump("1427-post-tonemap", pID3D11Texture2D_uid_1640);
    if (draw == 1428) Dump("1428-post-spatial-aa", pID3D11Texture2D_uid_1646);
    if (draw == 1437 || draw == 1443) {
        ComPtr<ID3D11Texture2D> backbuffer;
        Check(pIDXGISwapChain_uid_252->GetBuffer(0, IID_PPV_ARGS(&backbuffer)));
        Dump(draw == 1437 ? "1437-composite" : "1443-final", backbuffer.Get());
    }
}
