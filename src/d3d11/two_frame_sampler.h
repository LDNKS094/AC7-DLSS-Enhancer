#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ac7 {
// Fixed two/eight real Present intervals, at most two staging slots. No rendering or NGX changes.
class TwoFrameSampler {
public:
    struct Rect { uint32_t x, y, width, height; };
    enum class State { collecting, draining, complete, failed };
    TwoFrameSampler(ID3D11Device* device, const std::filesystem::path& newDirectory, uint32_t frameLimit = 2);
    ~TwoFrameSampler();
    TwoFrameSampler(const TwoFrameSampler&) = delete;
    TwoFrameSampler& operator=(const TwoFrameSampler&) = delete;
    bool collecting() const { return state_ == State::collecting; }
    State state() const { return state_; }
    const std::string& error() const { return error_; }
    uint32_t frames() const { return frames_; }
    uint64_t peakInFlightBytes() const { return peakBytes_; }
    void scene(ID3D11DeviceContext*, ID3D11Texture2D* color, ID3D11Texture2D* depth,
               ID3D11Texture2D* velocity, ID3D11Buffer* view, Rect);
    void exposure(ID3D11DeviceContext*, ID3D11Texture2D*);
    void present(ID3D11DeviceContext*);
    void fail(const std::string&) noexcept;
private:
    using Resource = Microsoft::WRL::ComPtr<ID3D11Resource>;
    struct Frame {
        std::array<Resource, 5> staging;
        Microsoft::WRL::ComPtr<ID3D11Query> ready;
        Rect rect{};
        uint32_t width = 0, height = 0;
        uint64_t bytes = 0;
        bool hasScene = false, hasExposure = false, pending = false;
    };
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    std::array<Frame, 2> data_;
    std::filesystem::path directory_;
    State state_ = State::collecting;
    uint32_t frames_ = 0, written_ = 0, polls_ = 0, limit_;
    uint32_t width_ = 0, height_ = 0;
    Rect rect_{};
    uint64_t bytes_ = 0, totalBytes_ = 0, peakBytes_ = 0;
    std::array<uint32_t, 8> counts_{};
    std::array<std::array<float, 4>, 8> times_{};
    double writeMilliseconds_ = 0;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    std::string error_;
    GUID deviceTag_{};
    void checkDevice(ID3D11Device*);
    void checkContext(ID3D11DeviceContext*);
    Resource copyTexture(ID3D11Texture2D*, DXGI_FORMAT, uint32_t, uint32_t);
    Resource copyView(ID3D11Buffer*);
    void drain();
    void saveFrame(Frame&, uint32_t);
    void saveManifest();
};
}
