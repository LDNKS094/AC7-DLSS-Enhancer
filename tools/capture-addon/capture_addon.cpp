// Diagnostic only: ReShade public API20, native D3D11 getters/copies, original draws always execute.
#include <reshade.hpp>
#include <d3d11.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <iterator>
#include "two_frame_sampler.h"
#include "guide_candidates.h"
#include "capture_request.h"
#include <fstream>
using Microsoft::WRL::ComPtr;
namespace {
enum class Role { guides, prewarp, tonemap };
struct Signature { size_t size; const char* hash; Role role; };
// Exact bytecode from the preserved 2026-10-28 flight capture; UIDs/draw ordinals are not live signatures.
constexpr Signature Signatures[] = {
    {4240, "85ec4635ea1fee2851d5979da08a1013609ead56312747cc4f54ba2e80620af0", Role::guides},
    {628, "af1247e3a473861934047c3cabe849dae6577dc61189bc56ba225f3351294ec0", Role::prewarp},
    {3220, "4a12c78e0f169b42270edae1fea1f1dcd4a3a618f278b7e8de230a1577f6dbf0", Role::tonemap}
};
struct Match { Role role; ComPtr<ID3D11PixelShader> shader; };
std::mutex mutex;
std::map<std::pair<reshade::api::device*, uint64_t>, Match> matches;
std::unique_ptr<ac7::TwoFrameSampler> sampler;
std::filesystem::path folder;
reshade::api::swapchain* owner = nullptr;
reshade::api::device* ownerDevice = nullptr;
ac7::GuideCandidates candidates;
D3D11_VIEWPORT selectedViewport{};
bool hasSelectedScene = false;
std::ofstream trace;
uint32_t traceEvents = 0;
bool attempted = false, logged = false, requestReadyLogged = false;

void log(const char* message) { reshade::log::message(reshade::log::level::info, message); }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string hash(const void* data, size_t size) {
    uint8_t bytes[32]{};
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), bytes, 32) >= 0, "Shader SHA256 failed");
    constexpr char hex[] = "0123456789abcdef"; std::string result;
    for (uint8_t b : bytes) { result += hex[b >> 4]; result += hex[b & 15]; } return result;
}
ComPtr<ID3D11Texture2D> texture(ID3D11ShaderResourceView* srv) {
    require(srv != nullptr, "Missing shader resource");
    D3D11_SHADER_RESOURCE_VIEW_DESC d{}; srv->GetDesc(&d);
    require(d.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && d.Texture2D.MostDetailedMip == 0 && d.Texture2D.MipLevels == 1, "Unexpected SRV subresource");
    ComPtr<ID3D11Resource> r; srv->GetResource(&r); ComPtr<ID3D11Texture2D> t;
    require(SUCCEEDED(r.As(&t)), "Expected Texture2D"); return t;
}
ComPtr<ID3D11Texture2D> psTexture(ID3D11DeviceContext* ctx, UINT slot) {
    ComPtr<ID3D11ShaderResourceView> srv; ctx->PSGetShaderResources(slot, 1, &srv); return texture(srv.Get());
}
D3D11_VIEWPORT viewport(ID3D11DeviceContext* ctx) {
    D3D11_VIEWPORT v{}; UINT count = 1; ctx->RSGetViewports(&count, &v);
    require(count == 1, "Single viewport required"); return v;
}
void resetGuides() { candidates.clear(); hasSelectedScene = false; }
void traceCandidate(Role role, ID3D11DeviceContext* ctx) {
    require(traceEvents++ < 64, "More than 64 recognized draw events in the bounded capture");
    const auto v = viewport(ctx);
    trace << "{\"role\":" << int(role) << ",\"completedIntervals\":" << sampler->frames()
        << ",\"viewport\":[" << v.TopLeftX << ',' << v.TopLeftY << ',' << v.Width << ',' << v.Height << "],\"guides\":" << candidates.size() << "}\n";
    trace.flush(); require(bool(trace), "Candidate trace write failed");
}
void onInitPipeline(reshade::api::device* device, reshade::api::pipeline_layout, uint32_t count,
    const reshade::api::pipeline_subobject* objects, reshade::api::pipeline pipeline) {
    if (device->get_api() != reshade::api::device_api::d3d11) return;
    try {
        for (uint32_t i = 0; i < count; ++i) {
            if (objects[i].type != reshade::api::pipeline_subobject_type::pixel_shader || objects[i].count != 1) continue;
            const auto& desc = *static_cast<const reshade::api::shader_desc*>(objects[i].data);
            for (const auto& s : Signatures) if (desc.code_size == s.size && desc.code && hash(desc.code, desc.code_size) == s.hash) {
                std::lock_guard<std::mutex> lock(mutex); matches.insert_or_assign({device, pipeline.handle}, Match{s.role, {}}); break;
            }
        }
    } catch (...) { log("AC7 sampler: shader recognition failed; original rendering continues."); }
}
void onDestroyPipeline(reshade::api::device* device, reshade::api::pipeline pipeline) {
    std::lock_guard<std::mutex> lock(mutex); matches.erase({device, pipeline.handle});
}
void onBindPipeline(reshade::api::command_list* cmd, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline) {
    if ((stages & reshade::api::pipeline_stage::pixel_shader) == reshade::api::pipeline_stage(0)) return;
    // No native getters are needed unless this is one of the three exact shader signatures.
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = matches.find({cmd->get_device(), pipeline.handle});
    if (it != matches.end()) {
        // bind_pipeline is documented after PSSetShader. Resolve the native shader through a getter, not opaque handle casts.
        auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(cmd->get_native());
        ComPtr<ID3D11PixelShader> shader; ctx->PSGetShader(&shader, nullptr, nullptr); it->second.shader = shader;
    }
}
void observe(reshade::api::command_list* cmd) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!sampler || !sampler->collecting() || cmd->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    try {
        auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(cmd->get_native());
        ComPtr<ID3D11PixelShader> shader; ctx->PSGetShader(&shader, nullptr, nullptr);
        const Match* match = nullptr;
        for (const auto& entry : matches) if (entry.first.first == cmd->get_device() && entry.second.shader && entry.second.shader.Get() == shader.Get()) { match = &entry.second; break; }
        if (!match) return;
        require(ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE, "Deferred candidate rendering is unsupported");
        traceCandidate(match->role, ctx);
        if (match->role == Role::guides) {
            ac7::GuideCandidate candidate;
            candidate.depth = psTexture(ctx, 0); candidate.velocity = psTexture(ctx, 3);
            ctx->PSGetConstantBuffers(1, 1, &candidate.view); require(candidate.view != nullptr, "Missing actual View binding");
            candidate.viewport = viewport(ctx); candidates.add(std::move(candidate));
        } else if (match->role == Role::prewarp) {
            ComPtr<ID3D11DepthStencilView> dsv; ctx->OMGetRenderTargets(0, nullptr, &dsv);
            require(dsv != nullptr, "Missing prewarp depth binding");
            ComPtr<ID3D11Resource> r; dsv->GetResource(&r);
            ComPtr<ID3D11Buffer> currentView; ctx->PSGetConstantBuffers(1, 1, &currentView);
            const auto v = viewport(ctx);
            const auto* candidate = candidates.find(r.Get(), currentView.Get(), v);
            if (!candidate) return; // Another view's warp must not borrow the main view's guides.
            const float values[] = {v.TopLeftX, v.TopLeftY, v.Width, v.Height};
            for (float f : values) require(std::isfinite(f) && f >= 0 && f <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION && std::floor(f) == f, "Invalid/non-integral viewport");
            auto color = psTexture(ctx, 0);
            sampler->scene(ctx, color.Get(), candidate->depth.Get(), candidate->velocity.Get(), currentView.Get(),
                {uint32_t(v.TopLeftX), uint32_t(v.TopLeftY), uint32_t(v.Width), uint32_t(v.Height)});
            selectedViewport = v; hasSelectedScene = true;
        } else {
            if (!hasSelectedScene) return;
            const auto v = viewport(ctx);
            if (!ac7::sameViewport(v, selectedViewport)) return;
            ComPtr<ID3D11ShaderResourceView> srv; ctx->VSGetShaderResources(0, 1, &srv);
            const auto exposure = texture(srv.Get()); sampler->exposure(ctx, exposure.Get());
        }
    } catch (const std::exception& e) { sampler->fail(e.what()); }
}
bool onDraw(reshade::api::command_list* cmd, uint32_t, uint32_t, uint32_t, uint32_t) { observe(cmd); return false; }
bool onDrawIndexed(reshade::api::command_list* cmd, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) { observe(cmd); return false; }
void onPresent(reshade::api::command_queue* queue, reshade::api::swapchain* swapchain, const reshade::api::rect*, const reshade::api::rect*, uint32_t, const reshade::api::rect*) {
    if (queue->get_device()->get_api() != reshade::api::device_api::d3d11) return;
    std::lock_guard<std::mutex> lock(mutex);
    try {
        const bool pairRequest = !attempted && ac7::freshCaptureRequest(folder / "AC7-Capture.request");
        const bool eightRequest = !attempted && ac7::freshCaptureRequest(folder / "AC7-Capture8.request");
        require(!(pairRequest && eightRequest), "Both two-frame and eight-frame requests exist; remove conflict before restart");
        if (!requestReadyLogged) { requestReadyLogged = true; log("AC7 sampler v4 ready: fixed2/8, two in-flight staging slots, stale requests archived."); }
        if (pairRequest || eightRequest) {
            attempted = true; owner = swapchain; ownerDevice = queue->get_device();
            auto* device = reinterpret_cast<ID3D11Device*>(queue->get_device()->get_native());
            const auto directory = folder / ((eightRequest?L"AC7-eight-frame-":L"AC7-two-frame-") + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            sampler = std::make_unique<ac7::TwoFrameSampler>(device, directory, eightRequest?8u:2u);
            trace.open(directory / "candidates.jsonl"); require(bool(trace), "Cannot open candidate trace");
            std::filesystem::remove(folder / (eightRequest?"AC7-Capture8.request":"AC7-Capture.request")); resetGuides();
            log(eightRequest?"AC7 sampler armed: exactly eight Present intervals, two staging slots, no rearm.":"AC7 sampler armed: exactly two Present intervals, no rearm."); return;
        }
        if (!sampler) return;
        if (swapchain != owner) sampler->fail("Multiple swapchains during capture");
        auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(queue->get_immediate_command_list()->get_native());
        sampler->present(ctx); resetGuides();
        if (!logged && (sampler->state() == ac7::TwoFrameSampler::State::complete || sampler->state() == ac7::TwoFrameSampler::State::failed)) {
            logged = true; trace.close(); log(sampler->state() == ac7::TwoFrameSampler::State::complete ? "AC7 sampler transport complete; temporal qualification pending." : sampler->error().c_str());
        }
    } catch (const std::exception& e) { if (sampler) sampler->fail(e.what()); attempted = true; log(e.what()); }
}
void onDestroyDevice(reshade::api::device* device) {
    std::lock_guard<std::mutex> lock(mutex);
    if (sampler && device == ownerDevice) { sampler->fail("Device destroyed"); sampler.reset(); resetGuides(); }
    for (auto it = matches.begin(); it != matches.end();) it = it->first.first == device ? matches.erase(it) : std::next(it);
}
}
extern "C" __declspec(dllexport) const char* NAME = "AC7 two-frame read-only sampler";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "One request, fixed two/eight prewarp frames, bounded staging. No NGX or rendering changes.";
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (!reshade::register_addon(module)) return FALSE;
        // DllMain also runs on small-stack host threads; keep the path buffer off its stack.
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length >= path.size()) { reshade::unregister_addon(module); return FALSE; }
        path.resize(length); folder = std::filesystem::path(path).parent_path();
        reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
        reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
        reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
        reshade::register_event<reshade::addon_event::draw>(onDraw);
        reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
        reshade::register_event<reshade::addon_event::present>(onPresent);
        reshade::register_event<reshade::addon_event::destroy_device>(onDestroyDevice);
        log("AC7 sampler v4 registered: fixed2/8, two staging slots, 256MiB logical memory and1GiB disk caps.");
    } else if (reason == DLL_PROCESS_DETACH) { reshade::unregister_addon(module); }
    return TRUE;
}
