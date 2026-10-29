#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <chrono>

namespace ac7 {
// Same-adapter transport only. Caller records D3D12 work and returns all five textures to COMMON.
// Keep this object and the submitted command allocator/list alive until poll retires the transaction.
class SharedTextureBridge {
public:
    enum class Result { pending, published, discarded };
    SharedTextureBridge(ID3D11Device*, ID3D12Device*, ID3D12CommandQueue*, uint32_t width, uint32_t height);
    SharedTextureBridge(const SharedTextureBridge&) = delete;
    SharedTextureBridge& operator=(const SharedTextureBridge&) = delete;
    void upload(uint64_t frame, const std::array<ID3D11Texture2D*, 4>& inputs);
    void submit(ID3D12GraphicsCommandList* closedList);
    // Mark an in-flight result unusable without cancelling GPU work or releasing its resources.
    void abandon();
    // Absolute caller deadline; checked even if the GPU already finished before this poll.
    Result poll(uint64_t currentFrame, ID3D11Texture2D* destination,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
    ID3D12Resource* resource(unsigned index) const { return shared12_.at(index).Get(); }
    bool busy() const { return active_; }
    // An uploaded but unsubmitted or failed transaction cannot be retired by normal polling.
    bool canPoll() const { return healthy_ && active_ && submitted_; }
private:
    void check(HRESULT, const char* stage = "bridge API");
    void validate(ID3D11Texture2D*, unsigned index) const;
    Microsoft::WRL::ComPtr<ID3D11Device> device11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context11_;
    Microsoft::WRL::ComPtr<ID3D12Device> device12_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence12_;
    Microsoft::WRL::ComPtr<ID3D11Fence> fence11_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 5> shared12_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 5> shared11_;
    uint32_t width_, height_;
    uint64_t value_ = 0, frame_ = 0;
    bool active_ = false, submitted_ = false, healthy_ = true, hasFrame_ = false, abandoned_ = false;
};
}
