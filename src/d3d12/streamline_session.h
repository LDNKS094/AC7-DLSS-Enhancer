#pragma once
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include "../frame_camera.h"
#include "../presentation_frame.h"
#include <memory>

namespace ac7 {
class GuideSnapshot;
struct ScenePresentation {
    ID3D12Resource*hudless;
    const GuideSnapshot*guides;
    const FrameCamera*camera;
    std::array<unsigned,4>region;
    uint64_t sequence;
};
// Process-pinned, like ModelContext; never shut down from DllMain or a layout destructor.
void initializeStreamline(void(*log)(unsigned,const char*)noexcept);
Microsoft::WRL::ComPtr<ID3D12Device> bindStreamlineDevice(ID3D12Device*);
Microsoft::WRL::ComPtr<ID3D12CommandQueue> nativeStreamlineQueue(ID3D12CommandQueue*);
Microsoft::WRL::ComPtr<IDXGIFactory2> streamlineFactory(IDXGIFactory*);
std::shared_ptr<const PresentationFrame>acquireStreamlineFrame(uint64_t engineFrame);
void sleepStreamlineFrame(const PresentationFrame&);
void startStreamlineSimulation(const PresentationFrame&);
void endStreamlineSimulation(const PresentationFrame&);
void startStreamlineRender(const PresentationFrame&);
void endStreamlineRender(const PresentationFrame&);
void markStreamlinePresentStart(const PresentationFrame&);
void markStreamlinePresentEnd(const PresentationFrame&);
void beginStreamlinePresent(ID3D12Resource*backbuffer,const PresentationFrame*frame,const ScenePresentation*scene=nullptr);
void reportStreamlineInputRejection(const char*);
}
