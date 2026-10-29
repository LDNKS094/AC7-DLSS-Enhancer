#pragma once
#include "shared_texture_bridge.h"
#include <chrono>
#include <stdexcept>

namespace ac7 {
// Single render-thread submission. The caller owns the bridge, model, allocator/list and output
// until retirement, and orders source writes after upload on the same immediate D3D11 context.
class FrameSubmission {
public:
    using Clock = std::chrono::steady_clock;
    explicit FrameSubmission(SharedTextureBridge& bridge) : bridge_(bridge) {}
    FrameSubmission(const FrameSubmission&) = delete;
    FrameSubmission& operator=(const FrameSubmission&) = delete;

    // Carry one absolute deadline from input acquisition through all model stages.
    // false means no upload/submit took place; the caller keeps the original image.
    bool begin(uint64_t frame, const std::array<ID3D11Texture2D*, 4>& inputs,
        ID3D12GraphicsCommandList* closedList, Clock::time_point deadline) {
        if (bridge_.busy()) throw std::runtime_error("Model resources still in flight");
        expired_ = Clock::now() >= deadline;
        if (expired_) return false;
        if (!closedList) throw std::runtime_error("Missing closed command list");
        deadline_ = deadline;
        for (unsigned i = 0; i < 4; ++i) inputs_[i] = inputs[i];
        try {
            bridge_.upload(frame, inputs);
            bridge_.submit(closedList);
        } catch (...) {
            if (!bridge_.busy()) inputs_ = {};
            throw;
        }
        return true;
    }
    SharedTextureBridge::Result poll(uint64_t frame, ID3D11Texture2D* destination) {
        if (!bridge_.busy()) throw std::runtime_error("Poll without in-flight model");
        expired_ = expired_ || Clock::now() >= deadline_;
        const auto result = bridge_.poll(frame, destination, deadline_);
        if (result == SharedTextureBridge::Result::discarded && Clock::now() >= deadline_) expired_ = true;
        if (result != SharedTextureBridge::Result::pending) inputs_ = {};
        return result;
    }
    bool expired() const { return expired_; }
    const std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 4>& inputs() const { return inputs_; }
private:
    SharedTextureBridge& bridge_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 4> inputs_;
    Clock::time_point deadline_{};
    bool expired_ = false;
};
}
