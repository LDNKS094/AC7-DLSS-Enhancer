// Independent public shared-handle/fence validation using preserved frames; never launches AC7.
#include "frame_submission.h"
#include <d3d11sdklayers.h>
#include <d3d12sdklayers.h>
#include <dxgi1_2.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <functional>
#include <cstring>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <memory>
using Microsoft::WRL::ComPtr;
namespace {
void require(bool b, const char* why) { if (!b) throw std::runtime_error(why); }
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Validation HRESULT " + std::to_string(hr)); }
struct Image { uint32_t w, h, format, bpp; std::vector<unsigned char> bytes; };
Image read(const std::filesystem::path& p, unsigned format, unsigned bpp, bool exposure) {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    Image image{}; require(bool(file), "Missing fixture"); auto size = file.tellg(); file.seekg(0);
    uint32_t header[4]{}; file.read(reinterpret_cast<char*>(header), 16);
    image.w = header[0]; image.h = header[1]; image.format = header[2]; image.bpp = header[3];
    require(image.w == (exposure ? 1u : 2560u) && image.h == (exposure ? 1u : 1440u) && image.format == format && image.bpp == bpp, "Fixture layout mismatch");
    require(size == std::streamoff(16 + size_t(image.w) * image.h * bpp), "Fixture size mismatch");
    image.bytes.resize(size_t(image.w) * image.h * bpp); file.read(reinterpret_cast<char*>(image.bytes.data()), image.bytes.size()); require(bool(file), "Fixture read failed"); return image;
}
ComPtr<ID3D11Texture2D> texture(ID3D11Device* device, const Image& image) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = image.w; d.Height = image.h; d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT(image.format); d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{image.bytes.data(), image.w * image.bpp, 0};
    ComPtr<ID3D11Texture2D> t; check(device->CreateTexture2D(&d, &init, &t)); return t;
}
std::vector<unsigned char> read11(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source, unsigned bpp) {
    D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d); d.BindFlags = d.MiscFlags = 0; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; check(device->CreateTexture2D(&d, nullptr, &staging)); context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE m{}; check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m));
    std::vector<unsigned char> bytes(size_t(d.Width) * d.Height * bpp);
    for (unsigned y = 0; y < d.Height; ++y) std::memcpy(bytes.data() + size_t(y) * d.Width * bpp, static_cast<unsigned char*>(m.pData) + size_t(y) * m.RowPitch, size_t(d.Width) * bpp);
    context->Unmap(staging.Get(), 0); return bytes;
}
void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &b);
}
void rejects(const std::function<void()>& action) { bool refused = false; try { action(); } catch (const std::exception&) { refused = true; } require(refused, "Invalid operation accepted"); }
unsigned errors11(ID3D11Device* device) {
    ComPtr<ID3D11InfoQueue> q; check(device->QueryInterface(IID_PPV_ARGS(&q))); unsigned errors = 0;
    for (UINT64 i = 0; i < q->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0; check(q->GetMessage(i, nullptr, &size)); std::vector<unsigned char> b(size); auto* m = reinterpret_cast<D3D11_MESSAGE*>(b.data()); check(q->GetMessage(i, m, &size));
        if (m->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) { ++errors; std::cerr << m->pDescription << '\n'; }
    } return errors;
}
unsigned errors12(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> q; check(device->QueryInterface(IID_PPV_ARGS(&q))); unsigned errors = 0;
    for (UINT64 i = 0; i < q->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0; check(q->GetMessage(i, nullptr, &size)); std::vector<unsigned char> b(size); auto* m = reinterpret_cast<D3D12_MESSAGE*>(b.data()); check(q->GetMessage(i, m, &size));
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { ++errors; std::cerr << m->pDescription << '\n'; }
    } return errors;
}
}
int wmain(int argc, wchar_t** argv) try {
    require(argc == 3, "Usage: validate-shared-bridge PREPARED_EIGHT_SR_INPUT NEW_EVIDENCE_DIRECTORY");
    const std::filesystem::path source(argv[1]), output(argv[2]); require(!std::filesystem::exists(output), "Refusing existing evidence");
    // Check all bounded fixture paths before creating devices or issuing GPU work.
    for (unsigned f = 0; f < 8; ++f) for (const auto* name : {L"color.raw", L"depth.raw", L"motion.raw", L"exposure.raw"}) require(std::filesystem::is_regular_file(source / (L"frame" + std::to_wstring(f)) / name), "Missing late frame");
    std::filesystem::create_directories(output);
    ComPtr<ID3D12Debug> debug; check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); debug->EnableDebugLayer();
    ComPtr<ID3D11Device> d11; ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &d11, nullptr, &ctx));
    ComPtr<IDXGIDevice> dxgi; check(d11.As(&dxgi)); ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter));
    ComPtr<ID3D12Device> d12; check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12)));
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; ComPtr<ID3D12CommandQueue> queue; check(d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D11Device> warp; check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &warp, nullptr, nullptr));
    rejects([&] { ac7::SharedTextureBridge mismatch(warp.Get(), d12.Get(), queue.Get(), 2560, 1440); });
    std::unique_ptr<ac7::SharedTextureBridge> owned;
    try { owned = std::make_unique<ac7::SharedTextureBridge>(d11.Get(), d12.Get(), queue.Get(), 2560, 1440); }
    catch (...) { errors11(d11.Get()); errors12(d12.Get()); throw; }
    auto& bridge = *owned;
    require(!bridge.canPoll(), "Idle bridge advertises retirement polling");
    rejects([&] { bridge.abandon(); });
    std::array<ID3D12Resource*, 5> identities{}; for (unsigned i = 0; i < 5; ++i) identities[i] = bridge.resource(i);
    ComPtr<ID3D12CommandAllocator> allocator; check(d12->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list; check(d12->CreateCommandList(0, qd.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list))); check(list->Close());
    std::array<ComPtr<ID3D12Resource>, 4> readbacks;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 4> layouts{};
    for (unsigned i = 0; i < 4; ++i) {
        auto desc = bridge.resource(i)->GetDesc(); UINT64 bytes; d12->GetCopyableFootprints(&desc, 0, 1, 0, &layouts[i], nullptr, nullptr, &bytes);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC b{}; b.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; b.Width = bytes; b.Height = b.DepthOrArraySize = b.MipLevels = b.SampleDesc.Count = 1; b.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(d12->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &b, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbacks[i])));
    }
    ac7::FrameSubmission submission(bridge);
    std::array<ComPtr<ID3D11Texture2D>, 4> reusedInputs;
    unsigned exactInputs = 0, exactOutputs = 0, reusedExact = 0, expiredDiscards = 0;
    for (unsigned pass = 0; pass < 13; ++pass) {
        const bool reuse = pass >= 9;
        const bool drop = pass == 0 || pass == 10 || pass == 11;
        const unsigned f = reuse ? 0 : (pass ? pass - 1 : 0); const uint64_t frame = reuse ? 800 + pass : (pass ? 698 + f : 1);
        auto dir = source / (L"frame" + std::to_wstring(f));
        std::array<Image, 4> data = {read(dir / L"color.raw", 10, 8, false), read(dir / L"depth.raw", 41, 4, false), read(dir / L"motion.raw", 34, 4, false), read(dir / L"exposure.raw", 41, 4, true)};
        std::array<ComPtr<ID3D11Texture2D>, 4> inputs; std::array<ID3D11Texture2D*, 4> raw{};
        for (unsigned i = 0; i < 4; ++i) {
            if (!reuse || pass == 9) inputs[i] = texture(d11.Get(), data[i]);
            else { inputs[i] = reusedInputs[i]; ctx->UpdateSubresource(inputs[i].Get(), 0, nullptr, data[i].bytes.data(), data[i].w * data[i].bpp, 0); }
            raw[i] = inputs[i].Get();
        }
        if (pass == 9) reusedInputs = inputs;
        auto sentinel = data[0]; std::fill(sentinel.bytes.begin(), sentinel.bytes.end(), 0xcd); auto dest = texture(d11.Get(), sentinel);
        if (!pass) {
            auto missing = raw; missing[3] = nullptr; rejects([&] { bridge.upload(frame, missing); }); require(!bridge.busy(), "Invalid input started transaction");
            auto wrong = raw; wrong[0] = inputs[1].Get(); rejects([&] { bridge.upload(frame, wrong); }); require(!bridge.busy(), "Wrong layout started transaction");
        }
        ComPtr<ID3D11ShaderResourceView> bound; check(d11->CreateShaderResourceView(inputs[0].Get(), nullptr, &bound));
        ID3D11ShaderResourceView* boundRaw = bound.Get(); ctx->PSSetShaderResources(7, 1, &boundRaw);
        ComPtr<ID3D12Fence> gate;
        if (!pass || (reuse && pass != 12)) { check(d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))); check(queue->Wait(gate.Get(), 1)); }
        check(allocator->Reset()); check(list->Reset(allocator.Get(), nullptr));
        for (unsigned i = 0; i < 4; ++i) {
            barrier(list.Get(), bridge.resource(i), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION target{}; target.pResource = readbacks[i].Get(); target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; target.PlacedFootprint = layouts[i];
            D3D12_TEXTURE_COPY_LOCATION origin{}; origin.pResource = bridge.resource(i); origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&target, 0, 0, 0, &origin, nullptr);
        }
        barrier(list.Get(), bridge.resource(4), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(bridge.resource(4), bridge.resource(0));
        barrier(list.Get(), bridge.resource(4), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        for (unsigned i = 0; i < 4; ++i) barrier(list.Get(), bridge.resource(i), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        check(list->Close());
        if (pass == 12) {
            rejects([&] { submission.begin(frame, raw, nullptr, ac7::FrameSubmission::Clock::time_point::max()); });
            require(!bridge.busy(), "Missing command list started upload");
            require(!submission.begin(frame, raw, list.Get(), ac7::FrameSubmission::Clock::time_point::min()), "Pre-expired frame submitted");
            require(!bridge.busy(), "Pre-expired frame consumed bridge");
            require(read11(d11.Get(), ctx.Get(), dest.Get(), 8) == sentinel.bytes, "Pre-expired frame changed fallback");
            for (const auto& input : submission.inputs()) require(!input, "Pre-expired frame retained input");
        }
        const auto deadline = pass == 10 || pass == 11 ? ac7::FrameSubmission::Clock::now() + std::chrono::milliseconds(2) : ac7::FrameSubmission::Clock::time_point::max();
        require(submission.begin(frame, raw, list.Get(), deadline), "Unexpected pre-submit expiry");
        require(bridge.canPoll(), "Submitted bridge refuses retirement polling");
        rejects([&] { bridge.upload(frame + 1, raw); });
        rejects([&] { submission.begin(frame + 1, raw, list.Get(), deadline); });
        if (reuse) {
            for (unsigned i = 0; i < 4; ++i) {
                auto overwritten = data[i]; std::fill(overwritten.bytes.begin(), overwritten.bytes.end(), 0x5a);
                ctx->UpdateSubresource(inputs[i].Get(), 0, nullptr, overwritten.bytes.data(), overwritten.w * overwritten.bpp, 0);
                require(read11(d11.Get(), ctx.Get(), inputs[i].Get(), overwritten.bpp) == overwritten.bytes, "Source overwrite not observed");
                require(overwritten.bytes != data[i].bytes, "Overwrite control identical to input");
                require(submission.inputs()[i].Get() == inputs[i].Get(), "Pending input reference lost");
            }
            if (pass == 10 || pass == 11) {
                const auto watchdog = ac7::FrameSubmission::Clock::now() + std::chrono::seconds(1);
                while (ac7::FrameSubmission::Clock::now() < deadline) {
                    if (ac7::FrameSubmission::Clock::now() >= watchdog) ExitProcess(2);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            if (pass == 11) {
                // Complete GPU work before the first poll; the expired deadline must still win.
                ComPtr<ID3D12Fence> completed; check(d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completed)));
                check(queue->Signal(completed.Get(), 1)); check(gate->Signal(1));
                const auto watchdog = ac7::FrameSubmission::Clock::now() + std::chrono::seconds(5);
                while (completed->GetCompletedValue() < 1) { if (ac7::FrameSubmission::Clock::now() >= watchdog) ExitProcess(2); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            } else if (pass != 12) {
                require(submission.poll(frame, dest.Get()) == ac7::SharedTextureBridge::Result::pending, "Gated reuse did not stay pending");
                require(read11(d11.Get(), ctx.Get(), dest.Get(), 8) == sentinel.bytes, "Pending reuse changed fallback");
                for (const auto& input : submission.inputs()) require(bool(input), "Pending input released early");
                check(gate->Signal(1));
            }
        }
        if (!pass) {
            require(bridge.poll(frame, dest.Get()) == ac7::SharedTextureBridge::Result::pending, "Blocked queue unexpectedly published output");
            rejects([&] { bridge.poll(frame, inputs[1].Get()); });
            rejects([&] { bridge.submit(list.Get()); });
            check(gate->Signal(1));
        }
        const auto began = std::chrono::steady_clock::now(); ac7::SharedTextureBridge::Result result;
        do {
            result = submission.poll(pass ? frame : frame + 1, dest.Get());
            if (result != ac7::SharedTextureBridge::Result::pending) break;
            if (std::chrono::steady_clock::now() - began > std::chrono::seconds(5)) { std::cerr << "GPU retirement timeout; terminating standalone probe without releasing pending resources\n"; ExitProcess(2); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
        require(result == (drop ? ac7::SharedTextureBridge::Result::discarded : ac7::SharedTextureBridge::Result::published), "Wrong publication decision");
        require(!bridge.canPoll(), "Retired bridge advertises pending polling");
        for (const auto& input : submission.inputs()) require(!input, "Retired input reference retained");
        if (pass == 10 || pass == 11) { require(submission.expired(), "Expired flag missing"); ++expiredDiscards; }
        ComPtr<ID3D11ShaderResourceView> after; ctx->PSGetShaderResources(7, 1, &after); require(after.Get() == bound.Get(), "D3D11 binding changed by bridge");
        ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(7, 1, &none);
        require(read11(d11.Get(), ctx.Get(), dest.Get(), 8) == (drop ? sentinel.bytes : data[0].bytes), "D3D11 output or fallback differs");
        if (pass && !reuse) ++exactOutputs;
        for (unsigned i = 0; i < 4; ++i) {
            void* mapped = nullptr; D3D12_RANGE range{0, SIZE_T(readbacks[i]->GetDesc().Width)}; check(readbacks[i]->Map(0, &range, &mapped));
            bool exact = true; for (unsigned y = 0; y < data[i].h; ++y) if (std::memcmp(data[i].bytes.data() + size_t(y) * data[i].w * data[i].bpp,
                static_cast<unsigned char*>(mapped) + layouts[i].Offset + size_t(y) * layouts[i].Footprint.RowPitch, size_t(data[i].w) * data[i].bpp)) exact = false;
            D3D12_RANGE empty{}; readbacks[i]->Unmap(0, &empty); require(exact, "D3D12 input differs"); if (reuse) ++reusedExact; else if (pass) ++exactInputs;
        }
        for (unsigned i = 0; i < 5; ++i) require(identities[i] == bridge.resource(i), "Shared resource identity changed");
        rejects([&] { bridge.upload(frame, raw); });
        std::cout << "Pass=" << pass << " frame=" << frame << " shared inputs exact; " << (drop ? "discarded; original exact" : "output returned exact") << (reuse ? "; source overwritten; references retired" : "") << std::endl;
    }
    require(reusedExact == 16 && expiredDiscards == 2, "Reuse/deadline coverage incomplete");
    rejects([&] { bridge.abandon(); });
    require(!bridge.busy() && errors11(d11.Get()) == 0 && errors12(d12.Get()) == 0, "Bridge/debug final checks failed");
    check(d11->GetDeviceRemovedReason()); check(d12->GetDeviceRemovedReason());
    std::ofstream report(output / L"checks.json"); report << "{\n  \"frames\": 8,\n  \"inputReadbacksExact\": " << exactInputs << ",\n  \"outputRoundtripsExact\": " << exactOutputs
        << ",\n  \"reusedSourceReadbacksExact\": " << reusedExact << ",\n  \"deadlineDiscards\": " << expiredDiscards << ",\n  \"preExpiredRefusedWithoutTokenConsumption\": true,\n  \"completedBeforeFirstPollStillExpires\": true,\n  \"sameSourceObjectsReusedAcrossTransactions\": true,\n  \"inputReferencesReleasedAfterRetirement\": true,\n  \"pendingReturnsWithoutPublication\": true,\n  \"lateFrameDiscardPreservesOriginal\": true,\n  \"busyAndDuplicateFrameRefused\": true,\n  \"layoutMissingAndOtherAdapterRefused\": true,\n  \"sharedResourceIdentityStable\": true,\n  \"d3d11ShaderBindingPreserved\": true,\n  \"duplicateSubmitAndWrongDestinationRefused\": true,\n  \"d3d11DebugErrors\": 0,\n  \"d3d12DebugErrors\": 0,\n  \"modelEvaluations\": 0,\n  \"gameIntegration\": false\n}\n";
    report.close(); require(bool(report), "Report write failed");
    std::cout << "PASS shared D3D11->D3D12->D3D11: 32 baseline inputs/8 outputs; 16 overwritten-source input readbacks exact; pending/completed expiry and pre-expired refusal; debug0; no model/game" << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << std::endl; return 1; }
