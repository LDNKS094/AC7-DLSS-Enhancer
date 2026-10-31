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
uint64_t SharedTextureBridge::stamp() const {return measureSceneCpu_?uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()):0;}
void SharedTextureBridge::validateQueries(const SceneQueries*q) const {
    if(!q)return;
    for(auto*query:{q->inputBegin,q->inputEnd,q->returnBegin,q->returnEnd}){
        if(!query)throw std::invalid_argument("Incomplete scene timestamp set");
        ComPtr<ID3D11Device>owner;query->GetDevice(&owner);D3D11_QUERY_DESC desc{};query->GetDesc(&desc);
        if(owner.Get()!=device11_.Get()||desc.Query!=D3D11_QUERY_TIMESTAMP)throw std::invalid_argument("Scene timestamp device/type mismatch");
    }
}
void SharedTextureBridge::check(HRESULT hr, const char* stage) {
    if (FAILED(hr)) { healthy_ = false; throw std::runtime_error(std::string(stage) + " HRESULT " + std::to_string(hr)); }
}
SharedTextureBridge::SharedTextureBridge(ID3D11Device* d11, ID3D12Device* d12, ID3D12CommandQueue* queue,
    uint32_t width, uint32_t height, ColorInput color, bool measureSceneCpu) : measureSceneCpu_(measureSceneCpu),device11_(d11), device12_(d12), queue_(queue), width_(width), height_(height), color_(color) {
    initTimes_.begin=stamp();
    if (color != ColorInput::fp16 && color != ColorInput::ac7NativeHdr && color != ColorInput::ac7NativeScene) throw std::runtime_error("Unknown bridge color layout");
    if (color != ColorInput::fp16 && (height != 1440 || (width != 2560 && (color != ColorInput::ac7NativeScene || width != 5120)))) throw std::runtime_error("Native HDR bridge requires known AC7 extent");
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
    initTimes_.adapterEnd=stamp();
    ComPtr<ID3D11Device5> device5; check(d11->QueryInterface(IID_PPV_ARGS(&device5)));
    ComPtr<ID3D11DeviceContext> immediate; d11->GetImmediateContext(&immediate);
    check(immediate.As(&context11_));
    if (color_ == ColorInput::ac7NativeScene) check(d11->CreateDeferredContext(0, &deferred_));
    initTimes_.contextEnd=stamp();
    for (unsigned i = 0; i < 5; ++i) {
        D3D11_TEXTURE2D_DESC texture{}; texture.Width = i == 3 ? 1 : width; texture.Height = i == 3 ? 1 : height;
        texture.ArraySize = texture.MipLevels = texture.SampleDesc.Count = 1; texture.Format = Formats[i];
        if(i == 0 && color_ != ColorInput::fp16) { texture.Width = 5120; texture.Format = DXGI_FORMAT_R11G11B10_FLOAT; }
        if(i == 4 && color_ == ColorInput::ac7NativeScene) texture.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        texture.Usage = D3D11_USAGE_DEFAULT;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texture.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(d11->CreateTexture2D(&texture, nullptr, &shared11_[i]), "Create shared D3D11 texture");
        ComPtr<IDXGIResource1> resource; check(shared11_[i].As(&resource));
        Handle handle; check(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle.value), "Export shared texture handle");
        check(d12->OpenSharedHandle(handle.value, IID_PPV_ARGS(&shared12_[i])), "Open D3D11 texture in D3D12");
    }
    initTimes_.texturesEnd=stamp();
    check(d12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12_)));
    Handle handle; check(d12->CreateSharedHandle(fence12_.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value));
    check(device5->OpenSharedFence(handle.value, IID_PPV_ARGS(&fence11_)));
    initTimes_.fenceEnd=stamp();
}
void SharedTextureBridge::validate(ID3D11Texture2D* texture, unsigned index) const {
    if (!texture) throw std::runtime_error("Missing bridge texture");
    ComPtr<ID3D11Device> owner; texture->GetDevice(&owner);
    if (owner.Get() != device11_.Get()) throw std::runtime_error("Bridge D3D11 texture device mismatch");
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
    const bool native = index == 0 && color_ != ColorInput::fp16;
    const bool packedOutput = index == 4 && color_ == ColorInput::ac7NativeScene;
    if (d.Width != (native ? 5120 : index == 3 ? 1 : width_) || d.Height != (index == 3 ? 1 : height_) || d.Format != ((native || packedOutput) ? DXGI_FORMAT_R11G11B10_FLOAT : Formats[index]) && !(index==2&&color_==ColorInput::ac7NativeScene&&d.Format==DXGI_FORMAT_R16G16_TYPELESS) ||
        d.MipLevels != 1 || d.ArraySize != 1 || d.SampleDesc.Count != 1 || d.SampleDesc.Quality)
        throw std::runtime_error("Bridge texture layout mismatch");
    if (index == 4 && texture == shared11_[4].Get()) throw std::runtime_error("Bridge destination aliases shared output");
    if (color_ == ColorInput::ac7NativeScene && index == 0 &&
        (d.Usage != D3D11_USAGE_DEFAULT || !(d.BindFlags & D3D11_BIND_SHADER_RESOURCE) || texture == shared11_[0].Get() || texture == shared11_[4].Get()))
        throw std::runtime_error("Native scene input must be an independent GPU shader resource");
}
void SharedTextureBridge::beginUpload(uint64_t frame) {
    if (!healthy_ || active_ || value_ > std::numeric_limits<uint64_t>::max() - 3 || (hasFrame_ && frame <= frame_))
        throw std::runtime_error("Bridge busy, unhealthy or non-monotonic frame");
}
void SharedTextureBridge::executeCopies() {
    ComPtr<ID3D11CommandList> commands;
    check(deferred_->FinishCommandList(FALSE, &commands), "Finish bridge copies");
    context11_->ExecuteCommandList(commands.Get(), TRUE);
    // D11 runtime retains queued command/resource lifetime; D12 allocator reuse is separately fenced.
}
void SharedTextureBridge::uploadScene(uint64_t frame, ID3D11Texture2D* scene, const SceneQueries*q, const std::array<ID3D11Texture2D*,2>*guides) {
    if (color_ != ColorInput::ac7NativeScene) throw std::runtime_error("Scene upload needs native scene transport");
    validate(scene, 0);if(guides)for(unsigned i=0;i<2;i++)validate((*guides)[i],i+1);validateQueries(q);beginUpload(frame);uploadedScene_ = scene;
    active_ = hasFrame_ = true; submitted_ = abandoned_ = false; frame_ = frame;
    cpuTimes_={};cpuTimes_.recordBegin=stamp();
    deferred_->CopyResource(shared11_[0].Get(), scene);
    if(guides)for(unsigned i=0;i<2;i++)deferred_->CopyResource(shared11_[i+1].Get(),(*guides)[i]);
    ComPtr<ID3D11CommandList>commands;check(deferred_->FinishCommandList(FALSE,&commands),"Finish scene input");cpuTimes_.recordEnd=stamp();
    if(q)context11_->End(q->inputBegin);cpuTimes_.inputExecuteBegin=stamp();context11_->ExecuteCommandList(commands.Get(),TRUE);cpuTimes_.inputExecuteEnd=stamp();if(q)context11_->End(q->inputEnd);
    cpuTimes_.inputSignalBegin=stamp();check(context11_->Signal(fence11_.Get(), ++value_));cpuTimes_.inputSignalEnd=stamp();
    // Published OptiScaler interop submits the producer before the other API waits on it.
    context11_->Flush();cpuTimes_.flushEnd=stamp();
    check(queue_->Wait(fence12_.Get(), value_));cpuTimes_.inputWaitEnd=stamp();
}
void SharedTextureBridge::upload(uint64_t frame, const std::array<ID3D11Texture2D*, 4>& inputs) {
    beginUpload(frame);
    for (unsigned i = 0; i < 4; ++i) validate(inputs[i], i);
    active_ = hasFrame_ = true; submitted_ = abandoned_ = false; frame_ = frame;
    if (color_ == ColorInput::ac7NativeScene) uploadedScene_ = inputs[0];
    for (unsigned i = 0; i < 4; ++i) (deferred_ ? deferred_.Get() : static_cast<ID3D11DeviceContext*>(context11_.Get()))->CopyResource(shared11_[i].Get(), inputs[i]);
    if (deferred_) executeCopies();
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
    ID3D12CommandList* lists[] = {list};cpuTimes_.d12ExecuteBegin=stamp();queue_->ExecuteCommandLists(1, lists);cpuTimes_.d12ExecuteEnd=stamp();
    check(queue_->Signal(fence12_.Get(), ++value_));cpuTimes_.d12SignalEnd=stamp();
}
uint64_t SharedTextureBridge::completedValue() {
    const auto completed = fence12_->GetCompletedValue();
    if (completed == UINT64_MAX) { healthy_ = false; throw std::runtime_error("Bridge retirement device removal"); }
    return completed;
}
bool SharedTextureBridge::queuedDrained() { return !active_ && completedValue() >= returnValue_; }
void SharedTextureBridge::queueSceneOutput(uint64_t frame, ID3D11Texture2D* scene, uint32_t left, const SceneQueries*q) {
    if (color_ != ColorInput::ac7NativeScene || !healthy_ || !active_ || !submitted_ || abandoned_ || frame != frame_ ||
        scene != uploadedScene_.Get() || left != (width_ == 2560 ? 1280u : 0u)) throw std::runtime_error("Invalid queued scene return");
    validate(scene, 0);validateQueries(q);cpuTimes_.returnRecordBegin=stamp();
    deferred_->CopySubresourceRegion(scene, 0, left, 0, 0, shared11_[4].Get(), 0, nullptr);
    ComPtr<ID3D11CommandList> commands;check(deferred_->FinishCommandList(FALSE, &commands), "Finish scene return");cpuTimes_.returnRecordEnd=stamp();cpuTimes_.returnWaitBegin=stamp();
    check(context11_->Wait(fence11_.Get(), value_), "Queue D11 consumer wait");cpuTimes_.returnWaitEnd=stamp();
    if(q)context11_->End(q->returnBegin);context11_->ExecuteCommandList(commands.Get(), TRUE);cpuTimes_.returnExecuteEnd=stamp();if(q)context11_->End(q->returnEnd);
    cpuTimes_.returnSignalBegin=stamp();check(context11_->Signal(fence11_.Get(), ++value_), "Signal scene return retirement");cpuTimes_.returnSignalEnd=stamp();
    returnValue_ = value_;active_ = submitted_ = false;
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
    if (deferred_) { deferred_->CopyResource(destination, shared11_[4].Get()); executeCopies(); }
    else context11_->CopyResource(destination, shared11_[4].Get());
    return Result::published;
}
}
