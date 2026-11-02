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
    GuideNormalizer(ID3D11Device* device, uint32_t width, uint32_t height);
    // Immediate context, serialized calls only. Reads allocation coordinates; outputs a cropped active view.
    // Stores backward pixels as explicit half values in RG16 typeless storage.
    // Caller establishes temporal/jitter semantics and keeps outputs alive through use.
    void record(ID3D11DeviceContext* context, ID3D11Texture2D* depth,
                ID3D11Texture2D* packedMotion, ViewRect rect, const ClipToPrevious& transform,const std::array<float,2>& projectionJitter={});
    ID3D11Texture2D* depth() const { return depth_.Get(); }
    ID3D11Texture2D* motion() const { return motion_.Get(); }
private:
    uint32_t width_, height_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_, motion_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> inputs_;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> inputViews_;
    std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, 2> views_;
};
}
