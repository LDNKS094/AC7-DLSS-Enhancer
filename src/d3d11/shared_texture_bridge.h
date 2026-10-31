#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <chrono>

namespace ac7 {
// Same-adapter transport only. Caller records D3D12 work and returns all five textures to COMMON.
// Caller retires queued GPU work before releasing the bridge or its D12 allocator/list.
class SharedTextureBridge {
public:
    enum class Result { pending, published, discarded };
    enum class ColorInput { fp16, ac7NativeHdr, ac7NativeScene };
    // Native HDR uses AC7's fixed 5120x1440 R11 allocation; output/guides use the View extent.
    SharedTextureBridge(ID3D11Device*, ID3D12Device*, ID3D12CommandQueue*, uint32_t width, uint32_t height,
        ColorInput color = ColorInput::fp16);
    SharedTextureBridge(const SharedTextureBridge&) = delete;
    SharedTextureBridge& operator=(const SharedTextureBridge&) = delete;
    void upload(uint64_t frame, const std::array<ID3D11Texture2D*, 4>& inputs);
    // Native scene transport: fixed5120 R11 input, central2560/full5120 R11 output.
    // Native-scene model guides are R32F depth and compatible RG16F backward-pixel motion.
    void uploadScene(uint64_t frame, ID3D11Texture2D* scene,
        const std::array<ID3D11Texture2D*,2>* guides=nullptr);
    // Queue same-frame GPU dependency and return-copy before the caller's original warp.
    // Retire when !busy() and completedValue() >= submittedValue(); never CPU-waits here.
    void queueSceneOutput(uint64_t frame, ID3D11Texture2D* scene, uint32_t left);
    uint64_t completedValue();
    uint64_t submittedValue() const { return value_; }
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
    void beginUpload(uint64_t frame);
    void executeCopies();
    Microsoft::WRL::ComPtr<ID3D11Device> device11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uploadedScene_;
    Microsoft::WRL::ComPtr<ID3D12Device> device12_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence12_;
    Microsoft::WRL::ComPtr<ID3D11Fence> fence11_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 5> shared12_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 5> shared11_;
    uint32_t width_, height_;
    ColorInput color_;
    uint64_t value_ = 0, frame_ = 0;
    bool active_ = false, submitted_ = false, healthy_ = true, hasFrame_ = false, abandoned_ = false;
};
}
