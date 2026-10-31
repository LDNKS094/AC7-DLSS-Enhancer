#pragma once
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <chrono>

namespace ac7 {
// Same-adapter transport only. Caller records D3D12 work and returns all five textures to COMMON.
// Retain bridge and submitted D12 allocator/list until poll or queuedDrained retires GPU work.
class SharedTextureBridge {
public:
    enum class Result { pending, published, discarded };
    enum class ColorInput { fp16, ac7NativeHdr, ac7NativeScene };
    // Native HDR uses AC7's fixed 5120x1440 R11 allocation; output/guides remain 2560x1440.
    struct SceneQueries {ID3D11Query* inputBegin=nullptr,*inputEnd=nullptr,*returnBegin=nullptr,*returnEnd=nullptr;};
    struct SceneCpuTimes {
        uint64_t recordBegin=0,recordEnd=0,inputExecuteBegin=0,inputExecuteEnd=0,inputSignalBegin=0,inputSignalEnd=0,flushEnd=0,inputWaitEnd=0;
        uint64_t d12ExecuteBegin=0,d12ExecuteEnd=0,d12SignalEnd=0,returnRecordBegin=0,returnRecordEnd=0,returnWaitBegin=0,returnWaitEnd=0,returnExecuteEnd=0,returnSignalBegin=0,returnSignalEnd=0;
    };
    struct InitTimes {uint64_t begin=0,adapterEnd=0,contextEnd=0,texturesEnd=0,fenceEnd=0;};
    const SceneCpuTimes& sceneCpuTimes() const {return cpuTimes_;}
    const InitTimes& initialization() const {return initTimes_;}
    SharedTextureBridge(ID3D11Device*, ID3D12Device*, ID3D12CommandQueue*, uint32_t width, uint32_t height,
        ColorInput color = ColorInput::fp16, bool measureSceneCpu=false);
    SharedTextureBridge(const SharedTextureBridge&) = delete;
    SharedTextureBridge& operator=(const SharedTextureBridge&) = delete;
    void upload(uint64_t frame, const std::array<ID3D11Texture2D*, 4>& inputs);
    // Native scene transport: fixed5120 R11 input, central2560/full5120 R11 output.
    // Native-scene model guides are R32F depth and compatible RG16F backward-pixel motion.
    void uploadScene(uint64_t frame, ID3D11Texture2D* scene, const SceneQueries* queries=nullptr,
        const std::array<ID3D11Texture2D*,2>* guides=nullptr);
    // Queue same-frame GPU dependency and return-copy before the caller's original warp.
    // Does not mean CPU-visible completion; retain bridge until queuedDrained(). Never CPU-waits.
    void queueSceneOutput(uint64_t frame, ID3D11Texture2D* scene, uint32_t left, const SceneQueries* queries=nullptr);
    uint64_t completedValue();
    uint64_t submittedValue() const { return value_; }
    bool queuedDrained();
    ID3D11Texture2D* sceneOutput() const { return color_ == ColorInput::ac7NativeScene ? shared11_[4].Get() : nullptr; }
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
    void validateQueries(const SceneQueries*) const;
    uint64_t stamp() const;
    bool measureSceneCpu_=false;
    SceneCpuTimes cpuTimes_{};
    InitTimes initTimes_{};
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
    uint64_t value_ = 0, frame_ = 0, returnValue_ = 0;
    bool active_ = false, submitted_ = false, healthy_ = true, hasFrame_ = false, abandoned_ = false;
};
}
