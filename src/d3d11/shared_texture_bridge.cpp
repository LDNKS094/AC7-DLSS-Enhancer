#include "shared_texture_bridge.h"
#include <dxgi1_2.h>
#include <limits>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
constexpr DXGI_FORMAT Formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
struct Handle { HANDLE value = nullptr; ~Handle() { if (value) CloseHandle(value); } };
}
void SharedTextureBridge::check(HRESULT hr, const char* stage) {
    if (FAILED(hr)) { healthy_ = false; throw std::runtime_error(std::string(stage) + " HRESULT " + std::to_string(hr)); }
}
SharedTextureBridge::SharedTextureBridge(ID3D11Device* d11, ID3D12Device* d12, ID3D12CommandQueue* queue,
    uint32_t width, uint32_t height) : device11_(d11), device12_(d12), queue_(queue), width_(width), height_(height) {
    if (!d11 || !d12 || !queue || !width || !height || width > 5120 || height > 1440)
        throw std::runtime_error("Invalid bounded bridge configuration");
    ComPtr<IDXGIDevice> dxgi; check(d11->QueryInterface(IID_PPV_ARGS(&dxgi)));
    ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter));
    DXGI_ADAPTER_DESC desc{}; check(adapter->GetDesc(&desc));
    const auto luid = d12->GetAdapterLuid();
    if (desc.AdapterLuid.LowPart != luid.LowPart || desc.AdapterLuid.HighPart != luid.HighPart)
        throw std::runtime_error("Bridge adapter LUID mismatch");
    ComPtr<ID3D12Device> owner; check(queue->GetDevice(IID_PPV_ARGS(&owner)));
    if (owner.Get() != d12 || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        throw std::runtime_error("Bridge requires this device's direct queue");
    ComPtr<ID3D11Device5> device5; check(d11->QueryInterface(IID_PPV_ARGS(&device5)));
    ComPtr<ID3D11DeviceContext> immediate; d11->GetImmediateContext(&immediate);
    check(immediate.As(&context11_));
    for (unsigned i = 0; i < 5; ++i) {
        D3D11_TEXTURE2D_DESC texture{}; texture.Width = i == 3 ? 1 : width; texture.Height = i == 3 ? 1 : height;
        texture.ArraySize = texture.MipLevels = texture.SampleDesc.Count = 1; texture.Format = Formats[i];
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texture.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(d11->CreateTexture2D(&texture, nullptr, &shared11_[i]), "Create shared D3D11 texture");
        ComPtr<IDXGIResource1> resource; check(shared11_[i].As(&resource));
        Handle handle; check(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle.value), "Export shared texture handle");
        check(d12->OpenSharedHandle(handle.value, IID_PPV_ARGS(&shared12_[i])), "Open D3D11 texture in D3D12");
    }
    check(d12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12_)));
    Handle handle; check(d12->CreateSharedHandle(fence12_.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    check(device5->OpenSharedFence(handle.value, IID_PPV_ARGS(&fence11_)));
}
void SharedTextureBridge::validate(ID3D11Texture2D* texture, unsigned index) const {
    if (!texture) throw std::runtime_error("Missing bridge texture");
    ComPtr<ID3D11Device> owner; texture->GetDevice(&owner);
    if (owner.Get() != device11_.Get()) throw std::runtime_error("Bridge D3D11 texture device mismatch");
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
    if (d.Width != (index == 3 ? 1 : width_) || d.Height != (index == 3 ? 1 : height_) || d.Format != Formats[index] ||
        d.MipLevels != 1 || d.ArraySize != 1 || d.SampleDesc.Count != 1)
        throw std::runtime_error("Bridge texture layout mismatch");
}
void SharedTextureBridge::upload(uint64_t frame, const std::array<ID3D11Texture2D*, 4>& inputs) {
    if (!healthy_ || active_ || value_ > std::numeric_limits<uint64_t>::max() - 2 || (hasFrame_ && frame <= frame_))
        throw std::runtime_error("Bridge busy, unhealthy or non-monotonic frame");
    for (unsigned i = 0; i < 4; ++i) validate(inputs[i], i);
    active_ = hasFrame_ = true; submitted_ = abandoned_ = false; frame_ = frame;
    for (unsigned i = 0; i < 4; ++i) context11_->CopyResource(shared11_[i].Get(), inputs[i]);
    check(context11_->Signal(fence11_.Get(), ++value_));
    context11_->Flush();
    check(queue_->Wait(fence12_.Get(), value_));
}
void SharedTextureBridge::submit(ID3D12GraphicsCommandList* list) {
    if (!healthy_ || !active_ || submitted_ || !list) throw std::runtime_error("Invalid bridge submit phase");
    ComPtr<ID3D12Device> owner; check(list->GetDevice(IID_PPV_ARGS(&owner)));
    if (owner.Get() != device12_.Get() || list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
        throw std::runtime_error("Bridge command-list device/type mismatch");
    submitted_ = true;
    ID3D12CommandList* lists[] = {list}; queue_->ExecuteCommandLists(1, lists);
    check(queue_->Signal(fence12_.Get(), ++value_));
}
void SharedTextureBridge::abandon() {
    if (!healthy_ || !active_ || !submitted_) throw std::runtime_error("Invalid bridge abandon phase");
    abandoned_ = true;
}
SharedTextureBridge::Result SharedTextureBridge::poll(uint64_t currentFrame, ID3D11Texture2D* destination,
    std::chrono::steady_clock::time_point deadline) {
    if (!healthy_ || !active_ || !submitted_) throw std::runtime_error("Invalid bridge poll phase");
    validate(destination, 4);
    check(device11_->GetDeviceRemovedReason()); check(device12_->GetDeviceRemovedReason());
    const auto completed = fence12_->GetCompletedValue();
    if (completed == UINT64_MAX) { healthy_ = false; throw std::runtime_error("Bridge fence reports device removal"); }
    if (std::chrono::steady_clock::now() >= deadline) abandoned_ = true;
    if (completed < value_) return Result::pending;
    active_ = submitted_ = false;
    if (abandoned_ || currentFrame != frame_) return Result::discarded;
    // Never enqueue a D3D11 GPU wait for an unfinished D3D12 result on the rendering thread.
    check(context11_->Wait(fence11_.Get(), value_));
    context11_->CopyResource(destination, shared11_[4].Get());
    return Result::published;
}
}
