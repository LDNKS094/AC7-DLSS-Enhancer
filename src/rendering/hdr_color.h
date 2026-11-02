#pragma once
#include <d3d12.h>
#include "guide_normalizer.h"
namespace ac7 {
// Private FP16 color passes; each allocator slot owns its textures and descriptors.
class HdrColorPass {
public:
    HdrColorPass(ID3D12Device*,unsigned width,unsigned height,const HdrColorPass*shared=nullptr);
    void prepare(ID3D12GraphicsCommandList*,ID3D12Resource*hdr,ViewRect,float exposure);
    void restore(ID3D12GraphicsCommandList*,ID3D12Resource*hdr,ViewRect,float exposure,ID3D12Resource*neural,bool linearEdits=false);
    ID3D12Resource*prepared()const{return prepared_.Get();}
    ID3D12Resource*srColor()const{return restored_.Get();}
private:
    void bind(ID3D12GraphicsCommandList*,ViewRect,float exposure,bool linearEdits=false);
    void descriptors(unsigned pass,ID3D12Resource*source,ID3D12Resource*prepared,ID3D12Resource*neural,ID3D12Resource*output);
    unsigned width_,height_,stride_=0,bound_=0;
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>root_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,2>pipelines_;
    Microsoft::WRL::ComPtr<ID3D12Resource>prepared_,restored_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,8>bindings_{};
};
}
