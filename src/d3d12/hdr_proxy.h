#pragma once
#include <d3d12.h>
#include "../d3d11/guide_normalizer.h"
namespace ac7 {
// Fixed-size local color passes, not a model or game host.
class HdrProxy12 {
public:
    HdrProxy12(ID3D12Device* device,uint32_t width,uint32_t height,DXGI_FORMAT hdrFormat=DXGI_FORMAT_R11G11B10_FLOAT);
    HdrProxy12(const HdrProxy12&)=delete;
    HdrProxy12& operator=(const HdrProxy12&)=delete;
    // Exclusive DIRECT list. HDR/neural enter and remain NON_PIXEL_SHADER_RESOURCE.
    // Outputs enter/leave COMMON. Caller returns them to COMMON after use.
    // Caller must retire the previous frame before reuse; retain inputs/object until completion.
    // prepare/restore use disjoint descriptor slots and may surround NR on one list without a CPU wait.
    // They must use the same retained HDR contents, rect and exposure; only one preparation is outstanding.
    // Null neural reads the actual FP16 proxy as an identity diagnostic.
    void prepare(ID3D12GraphicsCommandList* list,ID3D12Resource* hdr,ViewRect rect,float exposure);
    void restore(ID3D12GraphicsCommandList* list,ID3D12Resource* hdr,ViewRect rect,float exposure,ID3D12Resource* neural=nullptr);
    void record(ID3D12GraphicsCommandList* list,ID3D12Resource* hdr,ViewRect rect,float exposure,ID3D12Resource* neural=nullptr);
    ID3D12Resource* prepared() const {return prepared_.Get();}
    ID3D12Resource* restored() const {return restored_.Get();}
    ID3D12Resource* srColor() const {return color_.Get();}
private:
    void validate(ID3D12GraphicsCommandList* list,ID3D12Resource* hdr,ViewRect rect,float exposure,ID3D12Resource* neural);
    void bind(ID3D12GraphicsCommandList* list,ViewRect rect,float exposure);
    void descriptors(unsigned pass,ID3D12Resource* source,ID3D12Resource* prepared,ID3D12Resource* neural,ID3D12Resource* output);
    uint32_t width_,height_,stride_;
    DXGI_FORMAT hdrFormat_;
    ID3D12Resource* source_=nullptr;
    ViewRect rect_{};
    float exposure_=0;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,3> pipelines_;
    Microsoft::WRL::ComPtr<ID3D12Resource> prepared_,restored_,color_;
};
}
