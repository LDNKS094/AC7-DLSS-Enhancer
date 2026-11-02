#include "hdr_color.h"
#include "ac7_hdr_Prepare.h"
#include "ac7_hdr_Restore.h"
#include <stdexcept>
#include <string>
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("HDR color pass HRESULT "+std::to_string(unsigned(hr)));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
struct Constants {ViewRect rect;float exposure;unsigned linearEdits=0;float padding[2]{};};
static_assert(sizeof(Constants)==32);
}
HdrColorPass::HdrColorPass(ID3D12Device*d,unsigned width,unsigned height,const HdrColorPass*shared):width_(width),height_(height),device_(d){
    if(shared){root_=shared->root_;pipelines_=shared->pipelines_;}
    else{
        D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,3}};
        D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,8};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
        D3D12_ROOT_SIGNATURE_DESC signature{2,params};Microsoft::WRL::ComPtr<ID3DBlob>code,error;
        check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error));check(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root_)));
        const D3D12_SHADER_BYTECODE shaders[]={{ac7_hdr_Prepare,sizeof(ac7_hdr_Prepare)},{ac7_hdr_Restore,sizeof(ac7_hdr_Restore)}};
        for(unsigned i=0;i<pipelines_.size();i++){D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_.Get();pso.CS=shaders[i];check(d->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipelines_[i])));}
    }
    D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=width;texture.Height=height;texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;properties.CreationNodeMask=properties.VisibleNodeMask=1;
    ID3D12Resource**outputs[]={prepared_.GetAddressOf(),restored_.GetAddressOf()};
    for(auto output:outputs)check(d->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(output)));
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=8;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(d->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&heap_)));stride_=d->GetDescriptorHandleIncrementSize(heap.Type);
}
void HdrColorPass::descriptors(unsigned pass,ID3D12Resource*source,ID3D12Resource*prepared,ID3D12Resource*neural,ID3D12Resource*output){
    ID3D12Resource*row[]={source,prepared,neural,output};auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=4*pass*stride_;
    for(unsigned slot=0;slot<4;slot++,cpu.ptr+=stride_){
        const unsigned index=4*pass+slot,mask=1u<<index;
        if((bound_&mask)&&bindings_[index].Get()==row[slot])continue;
        bindings_[index]=row[slot];bound_|=mask;
        if(slot==3){D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(row[slot],nullptr,&view,cpu);}
        else{D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;device_->CreateShaderResourceView(row[slot],&view,cpu);}
    }
}
void HdrColorPass::bind(ID3D12GraphicsCommandList*l,ViewRect rect,float exposure,bool linearEdits){
    ID3D12DescriptorHeap*heaps[]={heap_.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(root_.Get());const Constants values{rect,exposure,unsigned(linearEdits)};l->SetComputeRoot32BitConstants(0,8,&values,0);
}
void HdrColorPass::prepare(ID3D12GraphicsCommandList*l,ID3D12Resource*hdr,ViewRect rect,float exposure){
    descriptors(0,hdr,nullptr,nullptr,prepared_.Get());bind(l,rect,exposure);
    transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);l->SetPipelineState(pipelines_[0].Get());l->SetComputeRootDescriptorTable(1,heap_->GetGPUDescriptorHandleForHeapStart());l->Dispatch((width_+7)/8,(height_+7)/8,1);transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
}
void HdrColorPass::restore(ID3D12GraphicsCommandList*l,ID3D12Resource*hdr,ViewRect rect,float exposure,ID3D12Resource*neural,bool linearEdits){
    descriptors(1,hdr,prepared_.Get(),neural,restored_.Get());bind(l,rect,exposure,linearEdits);
    transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(l,restored_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=4*stride_;l->SetPipelineState(pipelines_[1].Get());l->SetComputeRootDescriptorTable(1,gpu);l->Dispatch((width_+7)/8,(height_+7)/8,1);
    transition(l,restored_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
}
}
