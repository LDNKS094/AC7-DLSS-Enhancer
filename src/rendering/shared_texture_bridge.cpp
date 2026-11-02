#include "shared_texture_bridge.h"
#include <dxgi1_2.h>
#include <limits>
#include <stdexcept>
#include <string>
namespace ac7 {
namespace {
using Microsoft::WRL::ComPtr;
constexpr DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT};
void check(HRESULT hr,const char*stage){if(FAILED(hr))throw std::runtime_error(std::string(stage)+" HRESULT "+std::to_string(unsigned(hr)));}
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
}
SharedTextureBridge::SharedTextureBridge(ID3D11Device*d11,ID3D12Device*d12,ID3D12CommandQueue*queue,unsigned width,unsigned height,unsigned ow,unsigned oh):queue_(queue),width_(width),height_(height){
    if(!d11||!d12||!queue||!width||!height||!ow||!oh||width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||ow>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION||oh>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)throw std::invalid_argument("Invalid shared scene transport configuration");
    ComPtr<IDXGIDevice>dxgi;check(d11->QueryInterface(IID_PPV_ARGS(&dxgi)),"Get bridge DXGI device");
    ComPtr<IDXGIAdapter>adapter;check(dxgi->GetAdapter(&adapter),"Get bridge adapter");
    DXGI_ADAPTER_DESC desc{};check(adapter->GetDesc(&desc),"Get bridge adapter LUID");const auto luid=d12->GetAdapterLuid();
    if(desc.AdapterLuid.LowPart!=luid.LowPart||desc.AdapterLuid.HighPart!=luid.HighPart)throw std::runtime_error("Bridge adapter mismatch");
    ComPtr<ID3D12Device>owner;check(queue->GetDevice(IID_PPV_ARGS(&owner)),"Get bridge queue device");
    if(owner.Get()!=d12||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::runtime_error("Bridge requires its device's DIRECT queue");
    ComPtr<ID3D11Device5>device5;check(d11->QueryInterface(IID_PPV_ARGS(&device5)),"Get shared-fence device");
    ComPtr<ID3D11DeviceContext>immediate;d11->GetImmediateContext(&immediate);check(immediate.As(&context11_),"Get shared-fence context");
    for(unsigned i=0;i<shared11_.size();i++){
        D3D11_TEXTURE2D_DESC texture{};texture.Width=i==3?ow:width;texture.Height=i==3?oh:height;
        texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;texture.Format=formats[i];texture.Usage=D3D11_USAGE_DEFAULT;
        texture.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;texture.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(d11->CreateTexture2D(&texture,nullptr,&shared11_[i]),"Create shared scene texture");
        ComPtr<IDXGIResource1>resource;check(shared11_[i].As(&resource),"Get shared scene resource");Handle handle;
        check(resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value),"Export shared scene texture");
        check(d12->OpenSharedHandle(handle.value,IID_PPV_ARGS(&shared12_[i])),"Open shared scene texture");
    }
    check(d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence12_)),"Create bridge fence");Handle handle;
    check(d12->CreateSharedHandle(fence12_.Get(),nullptr,GENERIC_ALL,nullptr,&handle.value),"Export bridge fence");
    check(device5->OpenSharedFence(handle.value,IID_PPV_ARGS(&fence11_)),"Open bridge fence");
}
void SharedTextureBridge::uploadScene(uint64_t frame,ID3D11Texture2D*color,const std::array<ID3D11Texture2D*,2>&guides){
    if(busy()||value_>std::numeric_limits<uint64_t>::max()-3||(hasFrame_&&frame<=frame_))throw std::runtime_error("Invalid scene upload phase or token");
    const std::array<ID3D11Texture2D*,3>inputs={color,guides[0],guides[1]};
    for(unsigned i=0;i<inputs.size();i++){
        if(!inputs[i])throw std::runtime_error("Missing shared scene input");
        D3D11_TEXTURE2D_DESC desc{};inputs[i]->GetDesc(&desc);
        if(desc.Width!=width_||desc.Height!=height_||desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1||desc.SampleDesc.Quality||(desc.Format!=formats[i]&&!(i==2&&desc.Format==DXGI_FORMAT_R16G16_TYPELESS)))throw std::runtime_error("Shared scene input layout mismatch");
        context11_->CopyResource(shared11_[i].Get(),inputs[i]);
    }
    phase_=Phase::uploaded;hasFrame_=true;frame_=frame;
    check(context11_->Signal(fence11_.Get(),++value_),"Signal scene producer");
    context11_->Flush();
    check(queue_->Wait(fence12_.Get(),value_),"Wait for scene producer");
}
void SharedTextureBridge::submit(ID3D12GraphicsCommandList*list){
    if(phase_!=Phase::uploaded)throw std::runtime_error("Scene submit without upload");
    ID3D12CommandList*lists[]={list};queue_->ExecuteCommandLists(1,lists);
    check(queue_->Signal(fence12_.Get(),++value_),"Signal scene model completion");phase_=Phase::submitted;
}
uint64_t SharedTextureBridge::completedValue(){
    const auto completed=fence12_->GetCompletedValue();
    if(completed==UINT64_MAX)throw std::runtime_error("Scene transport device removed");
    return completed;
}
void SharedTextureBridge::queueOutputDraw(uint64_t frame,ID3D11CommandList*commands){
    if(phase_!=Phase::submitted||frame!=frame_)throw std::runtime_error("Scene return has no matching submission");
    check(context11_->Wait(fence11_.Get(),value_),"Wait for scene model completion");
    context11_->ExecuteCommandList(commands,TRUE);
    check(context11_->Signal(fence11_.Get(),++value_),"Signal scene consumer retirement");phase_=Phase::idle;
}
}
