#include "hdr_proxy.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("HDR12 HRESULT "+std::to_string(hr));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&v);}
struct Constants {ViewRect rect;float exposure;float padding[3]{};};
static_assert(sizeof(Constants)==32,"HLSL/root-constant layout");
}
HdrProxy12::HdrProxy12(ID3D12Device*d,uint32_t width,uint32_t height):width_(width),height_(height),stride_(0),device_(d){
    if(!d||!width||!height||width>8192||height>8192)throw std::invalid_argument("Invalid HDR12 device/extent");
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,3};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,8};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[1].DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC signature{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob>code,error;
    check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error));check(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root_)));
    const char* convert="Texture2D<float4> source:register(t0);RWTexture2D<float4> color:register(u0);cbuffer C:register(b0){uint2 base;uint2 size;float exposure;float3 pad;};[numthreads(8,8,1)]void Color(uint3 p:SV_DispatchThreadID){if(all(p.xy<size))color[p.xy]=float4(source.Load(int3(p.xy,0)).rgb,1);}";
    for(unsigned i=0;i<3;i++){
        code.Reset();error.Reset();const UINT flags=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS;
        const auto hr=i==2?D3DCompile(convert,std::strlen(convert),nullptr,nullptr,nullptr,"Color","cs_5_0",flags,0,&code,&error):D3DCompileFromFile(AC7_HDR_SHADER_PATH,nullptr,nullptr,i?"Restore":"Prepare","cs_5_0",flags,0,&code,&error);
        if(FAILED(hr))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"HDR12 compile failed");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_.Get();pso.CS={code->GetBufferPointer(),code->GetBufferSize()};check(d->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipelines_[i])));
        const auto format=i==1?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};check(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)));
        if(!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)||!(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD))throw std::runtime_error("HDR12 typed store/load unavailable");
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=format;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;properties.CreationNodeMask=properties.VisibleNodeMask=1;
        auto& output=i==0?prepared_:i==1?restored_:color_;check(d->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(output.GetAddressOf())));
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=12;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(d->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&heap_)));stride_=d->GetDescriptorHandleIncrementSize(heap.Type);
}
void HdrProxy12::validate(ID3D12GraphicsCommandList*l,ID3D12Resource*hdr,ViewRect rect,float exposure,ID3D12Resource*neural){
    if(!l||!hdr||l->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||rect.width!=width_||rect.height!=height_||!std::isfinite(exposure)||exposure<1.f/65504||exposure>65504)throw std::invalid_argument("Invalid HDR12 record arguments");
    ComPtr<ID3D12Device> actual;check(l->GetDevice(IID_PPV_ARGS(&actual)));if(actual.Get()!=device_.Get())throw std::invalid_argument("HDR12 command-list device mismatch");
    for(unsigned i=0;i<2;i++){
        auto* resource=i?neural:hdr;if(!resource)continue;
        actual.Reset();check(resource->GetDevice(IID_PPV_ARGS(&actual)));if(actual.Get()!=device_.Get())throw std::invalid_argument("HDR12 texture device mismatch");
        const auto desc=resource->GetDesc();
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Format!=(i?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R11G11B10_FLOAT)||desc.MipLevels!=1||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1||desc.SampleDesc.Quality||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))throw std::invalid_argument("Unsupported HDR12 texture layout");
        if(i?(desc.Width!=width_||desc.Height!=height_):(desc.Width<width_||desc.Height<height_||rect.x>desc.Width-width_||rect.y>desc.Height-height_))throw std::invalid_argument("Invalid HDR12 region");
        if(resource==prepared_.Get()||resource==restored_.Get()||resource==color_.Get())throw std::invalid_argument("HDR12 input/output alias");
    }
}
void HdrProxy12::descriptors(unsigned pass,ID3D12Resource*source,ID3D12Resource*prepared,ID3D12Resource*neural,ID3D12Resource*output){
    ID3D12Resource* row[]={source,prepared,neural,output};auto cpu=heap_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=4*pass*stride_;
    for(unsigned slot=0;slot<4;slot++,cpu.ptr+=stride_){
        if(slot==3){D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=row[slot]->GetDesc().Format;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(row[slot],nullptr,&view,cpu);}
        else {D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=row[slot]?row[slot]->GetDesc().Format:DXGI_FORMAT_R16G16B16A16_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;device_->CreateShaderResourceView(row[slot],&view,cpu);}
    }
}
void HdrProxy12::bind(ID3D12GraphicsCommandList*l,ViewRect rect,float exposure){
    ID3D12DescriptorHeap* heaps[]={heap_.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(root_.Get());Constants constants{rect,exposure};l->SetComputeRoot32BitConstants(0,8,&constants,0);
}
void HdrProxy12::prepare(ID3D12GraphicsCommandList*l,ID3D12Resource*hdr,ViewRect rect,float exposure){
    validate(l,hdr,rect,exposure,nullptr);if(source_)throw std::invalid_argument("HDR12 preparation already outstanding");descriptors(0,hdr,nullptr,nullptr,prepared_.Get());bind(l,rect,exposure);
    transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);l->SetPipelineState(pipelines_[0].Get());l->SetComputeRootDescriptorTable(1,heap_->GetGPUDescriptorHandleForHeapStart());l->Dispatch((width_+7)/8,(height_+7)/8,1);transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);source_=hdr;rect_=rect;exposure_=exposure;
}
void HdrProxy12::restore(ID3D12GraphicsCommandList*l,ID3D12Resource*hdr,ViewRect rect,float exposure,ID3D12Resource*neural){
    validate(l,hdr,rect,exposure,neural);if(source_!=hdr||rect.x!=rect_.x||rect.y!=rect_.y||rect.width!=rect_.width||rect.height!=rect_.height||exposure!=exposure_)throw std::invalid_argument("HDR12 preparation mismatch");descriptors(1,hdr,prepared_.Get(),neural?neural:prepared_.Get(),restored_.Get());descriptors(2,restored_.Get(),nullptr,nullptr,color_.Get());bind(l,rect,exposure);
    transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto gpu=heap_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=4*stride_;ID3D12Resource* outputs[]={nullptr,restored_.Get(),color_.Get()};
    for(unsigned i=1;i<3;i++,gpu.ptr+=4*stride_){
        transition(l,outputs[i],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);l->SetPipelineState(pipelines_[i].Get());l->SetComputeRootDescriptorTable(1,gpu);l->Dispatch((width_+7)/8,(height_+7)/8,1);
        transition(l,outputs[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,i==2?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    transition(l,prepared_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);transition(l,restored_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);source_=nullptr;
}
void HdrProxy12::record(ID3D12GraphicsCommandList*l,ID3D12Resource*hdr,ViewRect rect,float exposure,ID3D12Resource*neural){
    validate(l,hdr,rect,exposure,neural);prepare(l,hdr,rect,exposure);restore(l,hdr,rect,exposure,neural);
}
}
