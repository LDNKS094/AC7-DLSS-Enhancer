#pragma once
#include "guide_normalizer.h"
#include "shared_texture_bridge.h"
#include "../nvidia/ngx_models.h"
#include "../game/view_identity.h"
namespace ac7 {
// Shared SR transport: reserve before View admission, normalize real guides, return through a private D11 draw.
class SceneProcessor {
public:
    explicit SceneProcessor(GpuContext&);
    bool reserve(unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight,bool srEnabled=true,const NrSettings& settings={});
    bool prepare(ID3D11DeviceContext*,const ViewIdentity&,ID3D11Texture2D*depth,ID3D11Texture2D*motion);
    std::array<ID3D11Texture2D*,2>preparedGuides()const;
    bool draw(ID3D11DeviceContext*,ID3D11Texture2D*color,ID3D11RenderTargetView*,D3D11_VIEWPORT,uint64_t token,uint64_t sequence,const NrSettings&,float sharpness=0);
    void invalidate();
    bool drained();
private:
    void inputLayout(unsigned width,unsigned height);
    void layout(unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight,bool srEnabled,unsigned nrWidth);
    void drawCopy(ID3D11RenderTargetView*,D3D11_VIEWPORT,ID3D11ShaderResourceView*,Microsoft::WRL::ComPtr<ID3D11CommandList>&);
    struct Slot {Microsoft::WRL::ComPtr<ID3D12CommandAllocator>allocator;Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>list;uint64_t fence=0;};
    GpuContext&gpu_;
    Microsoft::WRL::ComPtr<ID3D11Device>device11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>context_,deferred_;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader>convert_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>vertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>pixel_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState>depthState_;
    Microsoft::WRL::ComPtr<ID3D11BlendState>blend_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>color_;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>colorUav_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>colorSrv_,outputSrv_,sourceSrv_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>source_;
    std::unique_ptr<SharedTextureBridge>bridge_;
    std::unique_ptr<GuideNormalizer>normalizer_;
    std::unique_ptr<NgxModels>models_;
    std::array<Slot,3>slots_{};
    ViewIdentity view_{};
    unsigned width_=0,height_=0,outWidth_=0,outHeight_=0,nrWidth_=0,next_=0;
    bool prepared_=false,srEnabled_=true;
};
}
