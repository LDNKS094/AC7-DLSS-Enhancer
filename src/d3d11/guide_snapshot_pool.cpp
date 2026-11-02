#include "guide_snapshot_pool.h"
#include <dxgi1_2.h>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace ac7 {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char*stage){if(FAILED(hr))throw std::runtime_error(std::string(stage)+" HRESULT "+std::to_string(unsigned(hr)));}
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
}
void GuideSnapshot::consumedBy(ID3D12Fence*fence,UINT64 value){
    if(!fence||!value)throw std::invalid_argument("Missing guide snapshot reader fence/value");
    std::lock_guard<std::mutex>guard(lock_);
    for(auto&reader:readers_)if(reader.fence.Get()==fence){reader.value=std::max(reader.value,value);return;}
    readers_.push_back({fence,value});
}
bool GuideSnapshot::retired(){
    std::lock_guard<std::mutex>guard(lock_);
    for(const auto&reader:readers_){
        const auto complete=reader.fence->GetCompletedValue();
        if(complete==UINT64_MAX)throw std::runtime_error("Guide snapshot reader device removed");
        if(complete<reader.value)return false;
    }
    return true;
}
GuideSnapshotPool::GuideSnapshotPool(ID3D11Device*d11,ID3D12Device*d12):device11_(d11),device12_(d12){
    if(!d11||!d12)throw std::invalid_argument("Missing guide snapshot device");
    ComPtr<IDXGIDevice>dxgi;check(d11->QueryInterface(IID_PPV_ARGS(&dxgi)),"Get guide snapshot DXGI device");
    ComPtr<IDXGIAdapter>adapter;check(dxgi->GetAdapter(&adapter),"Get guide snapshot adapter");
    DXGI_ADAPTER_DESC desc{};check(adapter->GetDesc(&desc),"Get guide snapshot adapter LUID");const auto luid=d12->GetAdapterLuid();
    if(luid.LowPart!=desc.AdapterLuid.LowPart||luid.HighPart!=desc.AdapterLuid.HighPart)throw std::runtime_error("Guide snapshot adapter mismatch");
}
std::shared_ptr<GuideSnapshot>GuideSnapshotPool::copy(ID3D11DeviceContext*context,ID3D11Texture2D*depth,ID3D11Texture2D*motion){
    if(!context||!depth||!motion||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)throw std::invalid_argument("Missing immediate guide snapshot inputs");
    ComPtr<ID3D11Device>owner;context->GetDevice(&owner);if(owner!=device11_)throw std::runtime_error("Guide snapshot context device mismatch");
    std::array<ID3D11Texture2D*,2>inputs={depth,motion};std::array<D3D11_TEXTURE2D_DESC,2>desc{};
    for(unsigned i=0;i<inputs.size();i++){
        inputs[i]->GetDevice(&owner);if(owner!=device11_)throw std::runtime_error("Guide snapshot input device mismatch");
        inputs[i]->GetDesc(&desc[i]);const auto&d=desc[i];
        const bool format=i?d.Format==DXGI_FORMAT_R16G16_TYPELESS||d.Format==DXGI_FORMAT_R16G16_FLOAT:d.Format==DXGI_FORMAT_R32_FLOAT;
        if(!format||!d.Width||!d.Height||d.Width!=desc[0].Width||d.Height!=desc[0].Height||d.SampleDesc.Count!=1||d.SampleDesc.Quality||d.MipLevels!=1||d.ArraySize!=1)
            throw std::runtime_error("Invalid uncompressed normalized guide snapshot layout");
    }
    const auto width=desc[0].Width,height=desc[0].Height;
    // Discard obsolete free layouts, while borrowed or GPU-busy snapshots remain alive.
    snapshots_.erase(std::remove_if(snapshots_.begin(),snapshots_.end(),[&](const auto&s){return s.use_count()==1&&(s->width_!=width||s->height_!=height)&&s->retired();}),snapshots_.end());
    std::shared_ptr<GuideSnapshot>snapshot;
    for(auto&s:snapshots_)if(s->width_==width&&s->height_==height&&s.use_count()==1&&s->retired()){snapshot=s;break;}
    if(!snapshot){
        snapshot=std::make_shared<GuideSnapshot>();snapshot->width_=width;snapshot->height_=height;
        constexpr DXGI_FORMAT formats[]={DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
        for(unsigned i=0;i<inputs.size();i++){
            auto layout=desc[i];layout.Format=formats[i];layout.Usage=D3D11_USAGE_DEFAULT;layout.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            layout.CPUAccessFlags=0;layout.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            check(device11_->CreateTexture2D(&layout,nullptr,&snapshot->shared11_[i]),"Create shared guide snapshot texture");
            ComPtr<IDXGIResource1>resource;check(snapshot->shared11_[i].As(&resource),"Get guide snapshot shared resource");Handle shared;
            check(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared.value),"Export guide snapshot texture");
            check(device12_->OpenSharedHandle(shared.value,IID_PPV_ARGS(&snapshot->shared12_[i])),"Open guide snapshot texture");
        }
        snapshots_.push_back(snapshot);
    }
    {std::lock_guard<std::mutex>guard(snapshot->lock_);snapshot->readers_.clear();}
    for(unsigned i=0;i<inputs.size();i++)context->CopyResource(snapshot->shared11_[i].Get(),inputs[i]);
    // The eventual present producer Signal/Flush covers these preceding D11 copies.
    return snapshot;
}
}
