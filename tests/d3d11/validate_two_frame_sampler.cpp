#include "two_frame_sampler.h"
#include "guide_candidates.h"
#include "capture_request.h"
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void require(bool v, const char* m) { if (!v) throw std::runtime_error(m); }
static void check(HRESULT hr) { require(SUCCEEDED(hr), "D3D11 call failed"); }
static std::vector<uint8_t> read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary); require(bool(f), "Missing output"); return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) try {
    require(argc == 2 || (argc == 3 && (std::string(argv[2]) == "--eight" || std::string(argv[2]) == "--eight-gap")),
        "Usage: validate-two-frame-sampler NEW_OUTPUT_DIRECTORY [--eight|--eight-gap]");
    const uint32_t target = argc == 3 ? 8u : 2u;
    const bool gap = argc == 3 && std::string(argv[2]) == "--eight-gap";
    const std::filesystem::path root(argv[1]); require(std::filesystem::create_directory(root), "Refusing existing test output");
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> ctx; D3D_FEATURE_LEVEL level;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &ctx));
    ComPtr<ID3D11InfoQueue> debug; check(device.As(&debug));
    constexpr uint32_t W = 32, H = 16;
    auto texture = [&](DXGI_FORMAT fmt, uint32_t w, uint32_t h) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt;
        d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> t; check(device->CreateTexture2D(&d, nullptr, &t)); return t;
    };
    auto color = texture(DXGI_FORMAT_R11G11B10_FLOAT, W, H);
    auto depth = texture(DXGI_FORMAT_R32G8X24_TYPELESS, W, H);
    auto mv = texture(DXGI_FORMAT_R16G16_UNORM, W, H);
    auto exposure = texture(DXGI_FORMAT_R32G32_FLOAT, 1, 1);
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 4096; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> view; check(device->CreateBuffer(&bd, nullptr, &view));
    const D3D11_VIEWPORT viewport{2, 0, 28, 16, 0, 1}; ctx->RSSetViewports(1, &viewport);
    ctx->PSSetConstantBuffers(1, 1, view.GetAddressOf());
    ac7::TwoFrameSampler sample(device.Get(), root / "pair", target);
    std::array<std::array<std::vector<uint8_t>, 5>, 8> expected;
    for (uint32_t frame = 0; frame < target; ++frame) {
        expected[frame][0].resize(W * H * 4); expected[frame][1].resize(W * H * 8);
        expected[frame][2].resize(W * H * 4); expected[frame][3].resize(4096); expected[frame][4].resize(8);
        for (uint32_t j : {0u, 1u, 2u}) for (size_t b = 0; b < expected[frame][j].size(); ++b) expected[frame][j][b] = uint8_t(b * 7 + frame * 13 + j);
        auto& bytes = expected[frame][3]; const uint32_t count = 200 + frame + (gap && frame >= 3 ? 1 : 0);
        const float previous[] = {10.0f + frame, 20.0f + frame}, current[] = {11.0f + frame, 21.0f + frame};
        std::memcpy(bytes.data() + 137 * 16 + 4, &count, 4);
        std::memcpy(bytes.data() + 133 * 16 + 8, previous, 8); std::memcpy(bytes.data() + 136 * 16 + 8, current, 8);
        const float gain[] = {1.0f, 1.0f}; std::memcpy(expected[frame][4].data(), gain, 8);
        ctx->UpdateSubresource(color.Get(), 0, nullptr, expected[frame][0].data(), W * 4, 0);
        ctx->UpdateSubresource(depth.Get(), 0, nullptr, expected[frame][1].data(), W * 8, 0);
        ctx->UpdateSubresource(mv.Get(), 0, nullptr, expected[frame][2].data(), W * 4, 0);
        ctx->UpdateSubresource(view.Get(), 0, nullptr, bytes.data(), 0, 0);
        ctx->UpdateSubresource(exposure.Get(), 0, nullptr, gain, 8, 0);
        sample.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        require(sample.collecting(), sample.error().c_str());
        sample.exposure(ctx.Get(), exposure.Get()); sample.present(ctx.Get());
        require(sample.state() != ac7::TwoFrameSampler::State::failed, sample.error().c_str());
        if (target == 8) {
            // A test has no real Present to submit GPU work; never add this wait to the addon.
            ctx->Flush(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    require(!sample.collecting() && sample.frames() == target, "Sampler did not stop at target frame count");
    // No third snapshot may enter while readback is pending or complete.
    sample.scene(nullptr, nullptr, nullptr, nullptr, nullptr, {});
    for (int i = 0; sample.state() == ac7::TwoFrameSampler::State::draining && i < 100; ++i) {
        ctx->Flush(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); sample.present(ctx.Get());
    }
    require(sample.state() == ac7::TwoFrameSampler::State::complete, sample.error().c_str());
    const char* names[] = {"scene.raw", "depth.raw", "velocity.raw", "view.bin", "exposure.raw"};
    for (uint32_t f = 0; f < target; ++f) for (uint32_t j = 0; j < 5; ++j) {
        const auto actual = read(root / "pair" / ("frame" + std::to_string(f) + "-" + names[j]));
        const size_t header = j == 3 ? 0 : 16;
        require(actual.size() == header + expected[f][j].size(), "Output byte count mismatch");
        if (j == 1) {
            // X24 padding is undefined; only depth32 and stencil8 carry source data.
            for (size_t pixel = 0; pixel < W * H; ++pixel)
                require(std::memcmp(actual.data() + header + pixel * 8, expected[f][j].data() + pixel * 8, 5) == 0, "Queued depth/stencil mismatch");
        } else require(std::memcmp(actual.data() + header, expected[f][j].data(), expected[f][j].size()) == 0, "Queued snapshot does not preserve frame-specific source bytes");
    }
    require(!std::filesystem::exists(root / "pair" / "INCOMPLETE"), "Completion marker not finalized");
    require(!std::filesystem::exists(root / "pair" / ("frame" + std::to_string(target) + "-scene.raw")), "Captured beyond fixed target");
    const auto manifestBytes = read(root / "pair" / "capture.json");
    const std::string manifest(manifestBytes.begin(), manifestBytes.end());
    require(manifest.find("\"frames\": " + std::to_string(target)) != std::string::npos, "Wrong manifest count");
    require(manifest.find(gap ? "\"candidateCounterStep1\": false" : "\"candidateCounterStep1\": true") != std::string::npos, "Gap/consecutive candidate not recorded correctly");
    require(manifest.find("\"temporalQualified\": false") != std::string::npos, "Synthetic sequence mislabeled temporally qualified");
    require(sample.peakInFlightBytes() <= 2 * (uint64_t(W) * H * 16 + 4104), "More than two logical slots retained");
    UINT count = 1; D3D11_VIEWPORT after{}; ctx->RSGetViewports(&count, &after);
    require(std::memcmp(&viewport, &after, sizeof(after)) == 0, "Viewport changed");
    ComPtr<ID3D11Buffer> bound; ctx->PSGetConstantBuffers(1, 1, &bound); require(bound.Get() == view.Get(), "Bound View changed");
    bool refused = false; try { ac7::TwoFrameSampler overwrite(device.Get(), root / "pair"); } catch (...) { refused = true; }
    require(refused, "Existing output not refused");
    {
        ac7::TwoFrameSampler missing(device.Get(), root / "missing"); missing.present(ctx.Get());
        require(missing.state() == ac7::TwoFrameSampler::State::failed, "Missing frame silently skipped");
    }
    {
        ac7::TwoFrameSampler duplicate(device.Get(), root / "duplicate");
        duplicate.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        duplicate.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        require(duplicate.state() == ac7::TwoFrameSampler::State::failed, "Duplicate candidate accepted");
    }
    {
        ac7::TwoFrameSampler invalid(device.Get(), root / "invalid-rect");
        invalid.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {31, 0, 28, 16});
        require(invalid.state() == ac7::TwoFrameSampler::State::failed, "Out of bounds rect accepted");
    }
    {
        ac7::TwoFrameSampler deferred(device.Get(), root / "deferred"); ComPtr<ID3D11DeviceContext> dc; check(device->CreateDeferredContext(0, &dc));
        deferred.scene(dc.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        require(deferred.state() == ac7::TwoFrameSampler::State::failed, "Deferred context accepted");
    }
    {
        ComPtr<ID3D11Device> other; ComPtr<ID3D11DeviceContext> otherContext;
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &other, nullptr, &otherContext));
        ac7::TwoFrameSampler foreign(device.Get(), root / "foreign-device"); foreign.present(otherContext.Get());
        require(foreign.state() == ac7::TwoFrameSampler::State::failed, "Another device passed identity check");
    }
    {
        // A legal allocation may be taller than the display; keep the byte bound, not a captured resolution cap.
        auto tallColor = texture(DXGI_FORMAT_R11G11B10_FLOAT, W, 1456);
        auto tallDepth = texture(DXGI_FORMAT_R32G8X24_TYPELESS, W, 1456);
        auto tallMotion = texture(DXGI_FORMAT_R16G16_UNORM, W, 1456);
        ac7::TwoFrameSampler padded(device.Get(), root / "padded-allocation");
        padded.scene(ctx.Get(), tallColor.Get(), tallDepth.Get(), tallMotion.Get(), view.Get(), {0, 0, W, 1440});
        require(padded.collecting(), "Legal padded allocation rejected");
        require(std::filesystem::exists(root / "padded-allocation" / "frame0-scene-metadata.json"), "Missing dimension diagnostics");
        padded.fail("Test stops after successful scene-copy eligibility check");
    }
    {
        auto largeColor = texture(DXGI_FORMAT_R11G11B10_FLOAT, 4096, 2049);
        ac7::TwoFrameSampler budget(device.Get(), root / "over-budget");
        budget.scene(ctx.Get(), largeColor.Get(), depth.Get(), mv.Get(), view.Get(), {0, 0, 4096, 2049});
        require(budget.state() == ac7::TwoFrameSampler::State::failed && budget.error().find("before allocation") != std::string::npos,
            "Oversized pair not refused before staging allocation");
        const auto bytes = read(root / "over-budget" / "frame0-scene-metadata.json");
        const std::string metadata(bytes.begin(), bytes.end());
        require(metadata.find("[4096,2049]") != std::string::npos, "Rejected dimensions not recorded");
    }
    {
        ac7::GuideCandidates candidates;
        ac7::GuideCandidate main{depth, mv, view, viewport}; candidates.add(main); candidates.add(main);
        require(candidates.size() == 1, "Identical guide bindings not deduplicated");
        auto auxiliary = main; auxiliary.viewport.Width = 8; candidates.add(auxiliary);
        require(candidates.find(depth.Get(), view.Get(), viewport)->velocity.Get() == mv.Get(), "Auxiliary view confused main matching");
        auto absent = viewport; absent.Width = 9;
        require(!candidates.find(depth.Get(), view.Get(), absent), "Unmatched view borrowed guides");
        auto ambiguous = main; ambiguous.velocity = texture(DXGI_FORMAT_R16G16_UNORM, W, H); candidates.add(ambiguous);
        bool refusedAmbiguous = false; try { candidates.find(depth.Get(), view.Get(), viewport); } catch (...) { refusedAmbiguous = true; }
        require(refusedAmbiguous, "Ambiguous velocities accepted"); candidates.clear();
        for (int i = 0; i < 16; ++i) { auto candidate = main; candidate.viewport.TopLeftX = float(i); candidates.add(candidate); }
        bool refusedOverflow = false; try { auto candidate = main; candidate.viewport.TopLeftX = 16; candidates.add(candidate); } catch (...) { refusedOverflow = true; }
        require(refusedOverflow, "Guide candidate bound not enforced");
    }
    {
        const auto request = root / "AC7-Capture.request";
        require(!ac7::freshCaptureRequest(request), "Missing request accepted");
        std::ofstream(request) << "test";
        HANDLE file = CreateFileW(request.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        require(file != INVALID_HANDLE_VALUE, "Cannot open test request"); FILETIME stale{1, 0};
        const bool ok = SetFileTime(file, nullptr, nullptr, &stale) != FALSE; CloseHandle(file); require(ok, "Cannot date stale request");
        require(!ac7::freshCaptureRequest(request) && !std::filesystem::exists(request), "Stale request armed instead of archived");
        std::ofstream(request) << "fresh";
        require(ac7::freshCaptureRequest(request), "Fresh in-process request refused");
    }
    {
        ac7::TwoFrameSampler partial(device.Get(), root / "partial-eight", 8);
        partial.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        partial.exposure(ctx.Get(), exposure.Get()); partial.present(ctx.Get());
        partial.present(ctx.Get());
        require(partial.state() == ac7::TwoFrameSampler::State::failed, "Missing interval in eight-frame sequence silently skipped");
        require(std::filesystem::exists(root / "partial-eight" / "INCOMPLETE") && std::filesystem::exists(root / "partial-eight" / "failure.txt") &&
            !std::filesystem::exists(root / "partial-eight" / "capture.json"), "Partial capture published complete manifest");
    }
    {
        auto withinGpuButLargeDisk = texture(DXGI_FORMAT_R11G11B10_FLOAT, 4096, 2047);
        ac7::TwoFrameSampler disk(device.Get(), root / "eight-disk-budget", 8);
        disk.scene(ctx.Get(), withinGpuButLargeDisk.Get(), depth.Get(), mv.Get(), view.Get(), {0, 0, 4096, 2047});
        require(disk.state() == ac7::TwoFrameSampler::State::failed && disk.error().find("disk limit") != std::string::npos,
            "Eight-frame disk budget was not checked before staging");
    }
    {
        ac7::TwoFrameSampler writeFailure(device.Get(), root / "write-failure", 8);
        writeFailure.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        writeFailure.exposure(ctx.Get(), exposure.Get());
        std::filesystem::create_directory(root / "write-failure" / "frame0-scene.raw");
        writeFailure.present(ctx.Get()); ctx->Flush();
        for (int i = 0; i < 100 && writeFailure.state() != ac7::TwoFrameSampler::State::failed; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            writeFailure.scene(ctx.Get(), color.Get(), depth.Get(), mv.Get(), view.Get(), {2, 0, 28, 16});
        }
        require(writeFailure.state() == ac7::TwoFrameSampler::State::failed && writeFailure.error().find("write failed") != std::string::npos &&
            std::filesystem::exists(root / "write-failure" / "INCOMPLETE") && !std::filesystem::exists(root / "write-failure" / "capture.json"),
            "Write failure published a successful capture");
    }
    uint32_t errors = 0;
    for (UINT64 i = 0; i < debug->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0; debug->GetMessage(i, nullptr, &size); std::vector<uint8_t> storage(size);
        auto* m = reinterpret_cast<D3D11_MESSAGE*>(storage.data()); check(debug->GetMessage(i, m, &size));
        if (m->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) { ++errors; std::cerr << m->pDescription << '\n'; }
    }
    require(errors == 0, "D3D11 debug errors");
    std::cout << "PASS: target=" << target << " gap=" << gap << " peakInFlightBytes=" << sample.peakInFlightBytes() << "; snapshots exact in defined components, fixed cap and partial failure; original refusal/state/request/guide tests passed; debug errors=0. Synthetic only.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
