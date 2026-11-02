#pragma once
#include "guide_normalizer.h"
#include <array>
#include <wrl/client.h>

namespace ac7 {
// Color consumers keep their own output grid; motion guides remain in the native grid.
class TemporalColor {
public:
    explicit TemporalColor(ID3D11Device*);
    Microsoft::WRL::ComPtr<ID3D11CommandList> motionBlur(
        ID3D11Texture2D*color,ID3D11Texture2D*flat,ID3D11Texture2D*tiles,
        ID3D11Texture2D*target,ID3D11Texture2D*resolved,ViewRect low,ViewRect high,
        const std::array<float,2>&jitter,const std::array<float,4>&parameters,unsigned quality);
private:
    void begin(ID3D11Texture2D*,ViewRect);
    Microsoft::WRL::ComPtr<ID3D11CommandList>finish(ID3D11Texture2D*,ID3D11Texture2D*);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>srv(ID3D11Texture2D*);
    Microsoft::WRL::ComPtr<ID3D11Device>device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>deferred_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>motion_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>linear_,point_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState>depth_;
    Microsoft::WRL::ComPtr<ID3D11BlendState>blend_;
};
}
