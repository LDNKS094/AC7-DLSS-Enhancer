#include "nr_spatial.h"
#include "ac7_nr_PackColor.h"
#include "ac7_nr_PackGuides.h"
#include "ac7_nr_UnpackEdits.h"
#include <stdexcept>
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("NR spatial HRESULT "+std::to_string(unsigned(hr)));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
struct Constants {unsigned width,height,workWidth;float centerHalf,transition,edge;std::array<float,2>jitter;};
static_assert(sizeof(Constants)==32);
}
NrSpatial::NrSpatial(ID3D12Device*d,unsigned w,unsigned h,unsigned work):width_(w),height_(h),workWidth_(work),device_(d){
    if(!d||!w||!h||!work||work>=w)throw std::invalid_argument("Invalid NR spatial extent");
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,3}};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,8};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxAnisotropy=1;
    D3D12_ROOT_SIGNATURE_DESC signature{2,params,1,&sampler};Microsoft::WRL::ComPtr<ID3DBlob>code,error;
    check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error));check(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root_)));
    const D3D12_SHADER_BYTECODE shaders[]={{ac7_nr_PackColor,sizeof(ac7_nr_PackColor)},{ac7_nr_PackGuides,sizeof(ac7_nr_PackGuides)},{ac7_nr_UnpackEdits,sizeof(ac7_nr_UnpackEdits)}};
    for(unsigned i=0;i<3;i++){D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_.Get();pso.CS=shaders[i];check(d->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipelines_[i])));}
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=18;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(d->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&heap_)));stride_=d->GetDescriptorHandleIncrementSize(heap.Type);
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};
    ID3D12Resource** outputs[]={color_.GetAddressOf(),depth_.GetAddressOf(),motion_.GetAddressOf(),edits_.GetAddressOf()};
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;properties.CreationNodeMask=properties.VisibleNodeMask=1;
    for(unsigned i=0;i<4;i++){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=i==3?w:work;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=formats[i];desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;check(d->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(outputs[i])));}
}
void NrSpatial::bind(ID3D12GraphicsCommandList*l,unsigned pass,const NrSpatialLayout&a,const std::array<float,2>&jitter,const std::array<ID3D12Resource*,3>&sources,const std::array<ID3D12Resource*,3>&targets){
    if(!l||a.width!=width_||a.height!=height_||a.workWidth!=workWidth_||!(a.edge>0&&a.edge<1))throw std::invalid_argument("NR spatial layout mismatch");
    auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=6*pass*stride_;
    for(auto*r:sources){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=r?r->GetDesc().Format:DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;device_->CreateShaderResourceView(r,&view,cpu);cpu.ptr+=stride_;}
    for(auto*r:targets){D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=r?r->GetDesc().Format:DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(r,nullptr,&view,cpu);cpu.ptr+=stride_;}
    ID3D12DescriptorHeap*heaps[]={heap_.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(root_.Get());l->SetPipelineState(pipelines_[pass].Get());
    const Constants constants{width_,height_,workWidth_,a.centerHalf,a.transition,a.edge,jitter};l->SetComputeRoot32BitConstants(0,8,&constants,0);
    auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=6*pass*stride_;l->SetComputeRootDescriptorTable(1,gpu);
}
void NrSpatial::pack(ID3D12GraphicsCommandList*l,ID3D12Resource*proxy,ID3D12Resource*depth,ID3D12Resource*motion,const NrSpatialLayout&a,const std::array<float,2>&jitter){
    transition(l,color_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(l,0,a,jitter,{proxy,nullptr,nullptr},{color_.Get(),nullptr,nullptr});l->Dispatch((workWidth_+7)/8,(height_+7)/8,1);
    transition(l,color_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(l,depth_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(l,motion_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(l,1,a,jitter,{nullptr,depth,motion},{nullptr,depth_.Get(),motion_.Get()});l->Dispatch((workWidth_+7)/8,(height_+7)/8,1);
    transition(l,depth_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(l,motion_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
void NrSpatial::unpackEdits(ID3D12GraphicsCommandList*l,ID3D12Resource*answer,const NrSpatialLayout&a,const std::array<float,2>&jitter){
    transition(l,edits_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    bind(l,2,a,jitter,{answer,color_.Get(),nullptr},{edits_.Get(),nullptr,nullptr});l->Dispatch((width_+7)/8,(height_+7)/8,1);
    transition(l,edits_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    for(auto*r:{color_.Get(),depth_.Get(),motion_.Get()})transition(l,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
}
}
