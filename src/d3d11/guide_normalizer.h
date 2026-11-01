#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

namespace ac7 {
struct ViewRect { uint32_t x, y, width, height; };
// Row-vector transform observed in the game's temporal consumer, not world/view matrices.
using ClipToPrevious = std::array<std::array<float, 4>, 4>;
class GuideNormalizer {
public:
    GuideNormalizer(ID3D11Device* device, uint32_t width, uint32_t height, bool modelMotion=false);
    // Immediate context, serialized calls only. Reads allocation coordinates; outputs a cropped active view.
    // modelMotion stores backward pixels using explicit float-to-half conversion in RG16 typeless storage.
    // Default motion is current-minus-previous NDC in the observed game convention.
    // Caller establishes temporal/jitter semantics and keeps outputs alive through use.
    void record(ID3D11DeviceContext* context, ID3D11Texture2D* depth,
                ID3D11Texture2D* packedMotion, ViewRect rect, const ClipToPrevious& transform,const std::array<float,2>& projectionJitter={});
    ID3D11Texture2D* depth() const { return depth_.Get(); }
    ID3D11Texture2D* motion() const { return motion_.Get(); }
    // 1=camera fallback, 2=object velocity, 3=invalid (cleared depth/motion, not valid history evidence).
    ID3D11Texture2D* validity() const { return validity_.Get(); }
private:
    uint32_t width_, height_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_, motion_, validity_;
    std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 3> views_;
};
}
