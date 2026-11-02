#pragma once
#include "../settings/nr_settings.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
namespace ac7 {
struct NrSpatialLayout {unsigned width=0,height=0,workWidth=0;float centerHalf=0,transition=0,edge=1;};
// Integral of a smoothstep density ramp, matching the FoveatedDLSS5 density model.
inline NrSpatialLayout nrSpatialLayout(unsigned w,unsigned h,const NrSettings&s){
    NrSpatialLayout a{w,h,w,float(w)*.5f,0,1};
    if(!s.enabled||!s.peripheralCompression||w<16||s.edgeScale>=1)return a;
    const double half=w*.5,center=std::min(half,h*double(s.centerAspect)*.5),r=half-center;
    const double transition=w*double(s.transitionWidth),t=transition>0?std::min(r/transition,1.0):0;
    const double loss=transition>0?transition*(t*t*t-.5*t*t*t*t)+std::max(r-transition,0.0):r;
    if(loss<=0)return a;
    // Equal parity makes the untouched centre an integer-texel translation.
    const unsigned removed=2*unsigned(std::round((1-s.edgeScale)*loss));
    if(!removed||removed>=w||w-removed<16)return a;
    const double edge=1-(removed*.5)/loss;if(!(edge>0&&edge<1))return a;
    a.workWidth=w-removed;a.centerHalf=float(center);a.transition=float(transition);a.edge=float(edge);
    return a;
}
// One allocator slot owns this object; retire its fence before recording again.
class NrSpatial {
public:
    NrSpatial(ID3D12Device*,unsigned width,unsigned height,unsigned workWidth);
    void pack(ID3D12GraphicsCommandList*,ID3D12Resource*proxy,ID3D12Resource*depth,ID3D12Resource*motion,const NrSpatialLayout&,const std::array<float,2>&jitter);
    void unpackEdits(ID3D12GraphicsCommandList*,ID3D12Resource*answer,const NrSpatialLayout&,const std::array<float,2>&jitter);
    ID3D12Resource*color()const{return color_.Get();}
    ID3D12Resource*depth()const{return depth_.Get();}
    ID3D12Resource*motion()const{return motion_.Get();}
    ID3D12Resource*edits()const{return edits_.Get();}
private:
    void bind(ID3D12GraphicsCommandList*,unsigned pass,const NrSpatialLayout&,const std::array<float,2>&,const std::array<ID3D12Resource*,3>&sources,const std::array<ID3D12Resource*,3>&targets);
    unsigned width_,height_,workWidth_,stride_;
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>root_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>heap_;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,3>pipelines_;
    Microsoft::WRL::ComPtr<ID3D12Resource>color_,depth_,motion_,edits_;
};
}
