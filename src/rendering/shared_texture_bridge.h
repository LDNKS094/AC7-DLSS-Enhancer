#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
namespace ac7 {
// Caller retires D11 consumers before replacing resources or reusing D12 allocators.
class SharedTextureBridge {
public:
    SharedTextureBridge(ID3D11Device*,ID3D12Device*,ID3D12CommandQueue*,unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight);
    void uploadScene(uint64_t frame,ID3D11Texture2D*color,const std::array<ID3D11Texture2D*,2>&guides);
    void submit(ID3D12GraphicsCommandList*);
    void queueOutputDraw(uint64_t frame,ID3D11CommandList*);
    ID3D11Texture2D*outputTexture()const{return shared11_[3].Get();}
    ID3D12Resource*resource(unsigned index)const{return shared12_.at(index).Get();}
    uint64_t completedValue();
    uint64_t submittedValue()const{return value_;}
    bool busy()const{return phase_!=Phase::idle;}
private:
    enum class Phase {idle,uploaded,submitted};
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4>context11_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence>fence12_;
    Microsoft::WRL::ComPtr<ID3D11Fence>fence11_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,4>shared12_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,4>shared11_;
    unsigned width_,height_;
    uint64_t value_=0,frame_=0;
    Phase phase_=Phase::idle;
    bool hasFrame_=false;
};
}
