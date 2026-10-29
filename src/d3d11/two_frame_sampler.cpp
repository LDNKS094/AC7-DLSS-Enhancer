#include "two_frame_sampler.h"
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <cmath>
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D11 HRESULT " + std::to_string(hr)); }
constexpr uint64_t MaxBytes = 256ull * 1024 * 1024;
constexpr uint64_t MaxDiskBytes = 1024ull * 1024 * 1024;
constexpr const char* Names[] = {"scene", "depth", "velocity", "view", "exposure"};
constexpr uint32_t Bpp[] = {4, 8, 4, 0, 8};
constexpr uint32_t Formats[] = {26, 19, 35, 0, 16};
}
namespace ac7 {
TwoFrameSampler::TwoFrameSampler(ID3D11Device* device, const std::filesystem::path& directory, uint32_t frameLimit)
    : device_(device), directory_(directory), limit_(frameLimit) {
    if (!device || (limit_ != 2 && limit_ != 8)) throw std::runtime_error("Expected device and fixed frame count 2 or 8");
    if (!std::filesystem::create_directory(directory_)) throw std::runtime_error("Refusing existing capture directory");
    std::ofstream incomplete(directory_ / "INCOMPLETE");
    if (!(incomplete << "Bounded capture; complete only when capture.json exists and INCOMPLETE is absent.\n")) throw std::runtime_error("Cannot mark capture incomplete");
    check(CoCreateGuid(&deviceTag_));
    const auto token = reinterpret_cast<uintptr_t>(this);
    check(device_->SetPrivateData(deviceTag_, sizeof(token), &token));
}
TwoFrameSampler::~TwoFrameSampler() { device_->SetPrivateData(deviceTag_, 0, nullptr); }
void TwoFrameSampler::checkDevice(ID3D11Device* device) {
    // Public private-data lookup also proves underlying identity through ReShade's device proxy.
    uintptr_t token = 0; UINT size = sizeof(token);
    check(device->GetPrivateData(deviceTag_, &size, &token));
    if (size != sizeof(token) || token != reinterpret_cast<uintptr_t>(this)) throw std::runtime_error("Capture resource belongs to another device");
}
void TwoFrameSampler::checkContext(ID3D11DeviceContext* ctx) {
    if (!ctx || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) throw std::runtime_error("Immediate context required");
    ComPtr<ID3D11Device> device; ctx->GetDevice(&device);
    checkDevice(device.Get());
    if (context_ && context_.Get() != ctx) throw std::runtime_error("Device/context changed during capture");
    context_ = ctx;
}
TwoFrameSampler::Resource TwoFrameSampler::copyTexture(ID3D11Texture2D* source, DXGI_FORMAT format, uint32_t width, uint32_t height) {
    if (!source) throw std::runtime_error("Missing capture texture");
    ComPtr<ID3D11Device> device; source->GetDevice(&device);
    D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d);
    checkDevice(device.Get());
    if (d.Format != format || d.Width != width || d.Height != height ||
        d.ArraySize != 1 || d.MipLevels != 1 || d.SampleDesc.Count != 1) throw std::runtime_error(
            "Capture texture descriptor mismatch: actual format=" + std::to_string(d.Format) + " size=" + std::to_string(d.Width) + "x" + std::to_string(d.Height) +
            " mips=" + std::to_string(d.MipLevels) + " array=" + std::to_string(d.ArraySize) + " samples=" + std::to_string(d.SampleDesc.Count) +
            "; expected format=" + std::to_string(format) + " size=" + std::to_string(width) + "x" + std::to_string(height));
    const uint32_t bpp = (format == DXGI_FORMAT_R32G8X24_TYPELESS || format == DXGI_FORMAT_R32G32_FLOAT) ? 8 : 4;
    const uint64_t size = uint64_t(width) * height * bpp;
    if (size > MaxBytes - bytes_) throw std::runtime_error("Capture exceeds 256 MiB logical data bound");
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging; check(device_->CreateTexture2D(&d, nullptr, &staging));
    context_->CopyResource(staging.Get(), source); bytes_ += size; data_[frames_ % 2].bytes += size;
    if (bytes_ > peakBytes_) peakBytes_ = bytes_;
    Resource result; check(staging.As(&result)); return result;
}
TwoFrameSampler::Resource TwoFrameSampler::copyView(ID3D11Buffer* source) {
    if (!source) throw std::runtime_error("Missing View buffer");
    ComPtr<ID3D11Device> device; source->GetDevice(&device);
    D3D11_BUFFER_DESC d{}; source->GetDesc(&d);
    checkDevice(device.Get());
    if (d.ByteWidth != 4096 || !(d.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) throw std::runtime_error("Expected captured 4096-byte View layout");
    if (d.ByteWidth > MaxBytes - bytes_) throw std::runtime_error("Capture byte bound");
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0; d.StructureByteStride = 0;
    ComPtr<ID3D11Buffer> staging; check(device_->CreateBuffer(&d, nullptr, &staging));
    context_->CopyResource(staging.Get(), source); bytes_ += d.ByteWidth; data_[frames_ % 2].bytes += d.ByteWidth;
    if (bytes_ > peakBytes_) peakBytes_ = bytes_;
    Resource result; check(staging.As(&result)); return result;
}
void TwoFrameSampler::scene(ID3D11DeviceContext* ctx, ID3D11Texture2D* color, ID3D11Texture2D* depth,
    ID3D11Texture2D* velocity, ID3D11Buffer* view, Rect rect) {
    if (!collecting()) return;
    try {
        checkContext(ctx);
        drain();
        auto& f = data_[frames_ % 2];
        if (f.pending) throw std::runtime_error("Two staging slots still busy; refusing to overwrite or skip a frame");
        if (f.hasScene) throw std::runtime_error("Multiple prewarp scene matches in one Present interval");
        if (!color) throw std::runtime_error("Missing scene");
        D3D11_TEXTURE2D_DESC d{}; color->GetDesc(&d);
        const uint64_t frameBytes = uint64_t(d.Width) * d.Height * 16 + 4096 + 8;
        const uint64_t pairBytes = 2 * frameBytes;
        const uint64_t diskBytes = uint64_t(limit_) * (frameBytes + 64);
        std::ofstream metadata(directory_ / ("frame" + std::to_string(frames_) + "-scene-metadata.json"));
        metadata << "{\"samplerVersion\":4,\"allocation\":[" << d.Width << ',' << d.Height << "],\"format\":" << d.Format
            << ",\"rect\":[" << rect.x << ',' << rect.y << ',' << rect.width << ',' << rect.height
            << "],\"plannedPairBytes\":" << pairBytes << ",\"byteLimit\":" << MaxBytes << ",\"targetFrames\":" << limit_ << ",\"plannedDiskBytes\":" << diskBytes << ",\"textures\":[";
        bool first = true;
        for (auto* texture : {color, depth, velocity}) {
            D3D11_TEXTURE2D_DESC td{}; if (texture) texture->GetDesc(&td);
            if (!first) metadata << ','; first = false;
            metadata << "{\"present\":" << (texture ? "true" : "false") << ",\"width\":" << td.Width << ",\"height\":" << td.Height
                << ",\"format\":" << td.Format << ",\"mips\":" << td.MipLevels << ",\"samples\":" << td.SampleDesc.Count << '}';
        }
        metadata << "]}\n"; metadata.close(); if (!metadata) throw std::runtime_error("Scene metadata write failed");
        const std::string dimensions = " allocation=" + std::to_string(d.Width) + "x" + std::to_string(d.Height) +
            " rect=(" + std::to_string(rect.x) + "," + std::to_string(rect.y) + "," + std::to_string(rect.width) + "," + std::to_string(rect.height) + ")";
        if (!d.Width || !d.Height || d.Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || d.Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::runtime_error("Invalid D3D11 scene dimensions:" + dimensions);
        if (!rect.width || !rect.height || rect.x >= d.Width || rect.y >= d.Height ||
            rect.width > d.Width - rect.x || rect.height > d.Height - rect.y)
            throw std::runtime_error("View rect exceeds actual scene allocation:" + dimensions);
        if (pairBytes > MaxBytes) throw std::runtime_error("Two-frame logical data exceeds 256 MiB before allocation: bytes=" + std::to_string(pairBytes) + dimensions);
        if (diskBytes > MaxDiskBytes - 1024 * 1024) throw std::runtime_error("Capture exceeds 1 GiB disk limit including metadata reserve");
        if (std::filesystem::space(directory_).available < diskBytes + 64ull * 1024 * 1024) throw std::runtime_error("Insufficient capture disk space plus 64 MiB margin");
        if (frames_ && (d.Width != width_ || d.Height != height_ ||
            std::memcmp(&rect, &rect_, sizeof(rect)))) throw std::runtime_error("Dimensions/View rect changed");
        if (!frames_) { width_ = d.Width; height_ = d.Height; rect_ = rect; }
        if (color == depth || color == velocity || depth == velocity) throw std::runtime_error("Aliased scene/guide inputs");
        f.width = d.Width; f.height = d.Height; f.rect = rect;
        f.staging[0] = copyTexture(color, DXGI_FORMAT_R11G11B10_FLOAT, d.Width, d.Height);
        f.staging[1] = copyTexture(depth, DXGI_FORMAT_R32G8X24_TYPELESS, d.Width, d.Height);
        f.staging[2] = copyTexture(velocity, DXGI_FORMAT_R16G16_UNORM, d.Width, d.Height);
        f.staging[3] = copyView(view); f.hasScene = true;
    } catch (const std::exception& e) { fail(e.what()); }
}
void TwoFrameSampler::exposure(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture) {
    if (!collecting()) return;
    try {
        checkContext(ctx); auto& f = data_[frames_ % 2];
        if (!f.hasScene || f.hasExposure) throw std::runtime_error("Missing prewarp scene or duplicate tonemap exposure");
        f.staging[4] = copyTexture(texture, DXGI_FORMAT_R32G32_FLOAT, 1, 1); f.hasExposure = true;
    } catch (const std::exception& e) { fail(e.what()); }
}
void TwoFrameSampler::present(ID3D11DeviceContext* ctx) {
    if (state_ == State::complete || state_ == State::failed) return;
    try {
        checkContext(ctx);
        if (++polls_ > 120 || std::chrono::steady_clock::now() - start_ > std::chrono::seconds(10)) throw std::runtime_error("Capture completion deadline exceeded");
        if (collecting()) {
            auto& f = data_[frames_ % 2];
            if (!f.hasScene || !f.hasExposure || f.pending) throw std::runtime_error("Incomplete scene/exposure in Present interval; refusing skipped frames");
            D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0}; check(device_->CreateQuery(&q, &f.ready));
            ctx->End(f.ready.Get()); f.pending = true; ++frames_;
            if (frames_ == limit_) state_ = State::draining;
        }
        drain();
        if (written_ == limit_) { saveManifest(); state_ = State::complete; }
    } catch (const std::exception& e) { fail(e.what()); }
}
void TwoFrameSampler::fail(const std::string& message) noexcept {
    if (state_ == State::complete || state_ == State::failed) return;
    state_ = State::failed; error_ = message;
    data_ = {}; bytes_ = 0;
    try { std::ofstream(directory_ / "failure.txt") << message << '\n'; } catch (...) {}
}
void TwoFrameSampler::drain() {
    while (written_ < frames_) {
        auto& f = data_[written_ % 2];
        if (!f.pending) throw std::runtime_error("Internal staging queue order mismatch");
        BOOL ready = FALSE;
        const HRESULT hr = context_->GetData(f.ready.Get(), &ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        check(hr); if (hr != S_OK || !ready) return;
        const auto began = std::chrono::steady_clock::now();
        saveFrame(f, written_);
        writeMilliseconds_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        totalBytes_ += f.bytes; bytes_ -= f.bytes; f = {}; ++written_;
    }
}
void TwoFrameSampler::saveFrame(Frame& f, uint32_t i) {
        for (uint32_t j = 0; j < 5; ++j) {
            D3D11_MAPPED_SUBRESOURCE m{};
            check(context_->Map(f.staging[j].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m));
            try {
                const std::string stem = "frame" + std::to_string(i) + "-" + Names[j];
                std::ofstream out(directory_ / (stem + (j == 3 ? ".bin" : ".raw")), std::ios::binary);
                if (j == 3) {
                    out.write(static_cast<const char*>(m.pData), 4096);
                    const auto* b = static_cast<const uint8_t*>(m.pData);
                    std::memcpy(&counts_[i], b + 137 * 16 + 4, 4);
                    std::memcpy(times_[i].data(), b + 133 * 16 + 8, 8);
                    std::memcpy(times_[i].data() + 2, b + 136 * 16 + 8, 8);
                } else {
                    const uint32_t w = j == 4 ? 1 : f.width, h = j == 4 ? 1 : f.height;
                    const uint32_t header[] = {w, h, Formats[j], Bpp[j]};
                    out.write(reinterpret_cast<const char*>(header), sizeof(header));
                    for (uint32_t y = 0; y < h; ++y) out.write(static_cast<const char*>(m.pData) + y * m.RowPitch, w * Bpp[j]);
                }
                out.close(); if (!out) throw std::runtime_error("Capture file write failed");
            } catch (...) { context_->Unmap(f.staging[j].Get(), 0); throw; }
            context_->Unmap(f.staging[j].Get(), 0);
        }
}
void TwoFrameSampler::saveManifest() {
    bool counterStep = true, timeChain = true;
    for (uint32_t i = 1; i < limit_; ++i) {
        counterStep = counterStep && counts_[i] == counts_[i - 1] + uint32_t(1);
        for (int c = 0; c < 2; ++c) timeChain = timeChain && std::isfinite(times_[i - 1][c + 2]) && std::isfinite(times_[i][c]) &&
            std::isfinite(times_[i][c + 2]) && std::abs(times_[i][c] - times_[i - 1][c + 2]) < 1e-6f && times_[i][c + 2] > times_[i][c];
    }
    std::ofstream out(directory_ / "capture.json");
    out << "{\n  \"samplerVersion\": 4,\n  \"transportComplete\": true,\n  \"frames\": " << limit_ << ",\n  \"logicalBytes\": " << totalBytes_
        << ",\n  \"peakInFlightLogicalBytes\": " << peakBytes_ << ",\n  \"maxInFlightSlots\": 2,\n  \"writeMilliseconds\": " << writeMilliseconds_
        << ",\n  \"allocation\": [" << width_ << ',' << height_ << "],\n  \"rect\": [" << rect_.x << ',' << rect_.y << ',' << rect_.width << ',' << rect_.height
        << "],\n  \"frameNumberCandidates\": [";
    for (uint32_t i = 0; i < limit_; ++i) { if (i) out << ','; out << counts_[i]; }
    out << "],\n  \"candidateCounterStep1\": " << (counterStep ? "true" : "false")
        << ",\n  \"candidateTimeChain\": " << (timeChain ? "true" : "false")
        << ",\n  \"temporalQualified\": false,\n  \"preExposureAssumption\": 1,\n  \"note\": \"Native prewarp inputs and actual jitter; two staging slots, nonblocking GPU queries. Synchronous disk writes can perturb frame times. Temporal correspondence requires independent audit.\"\n}\n";
    out.close(); if (!out) throw std::runtime_error("Capture manifest write failed");
    std::filesystem::remove(directory_ / "INCOMPLETE");
}
}
