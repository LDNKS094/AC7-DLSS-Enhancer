#pragma once
#include "view_identity.h"
#include "guide_normalizer.h"
#include "guide_snapshot_pool.h"
#include "../frame_camera.h"
#include "../presentation_frame.h"
#include <memory>
#include <optional>

namespace ac7 {
// Filled by the scene RHI job, then read by the later compositor/present RHI work.
struct SceneGuideFrame {
    ViewIdentity view;
    uint64_t token=0,sequence=0;
    unsigned nativeCounter=0;
    std::shared_ptr<GuideSnapshot>snapshot;
    std::shared_ptr<const PresentationFrame>presentation;
    std::optional<FrameCamera>camera;
    const char*cameraRefusal="Native camera parameters have not been mapped";
};
struct FinalCompositeInputs {
    ViewIdentity view;
    ViewRect output{};
    unsigned nativeCounter=0;
    std::shared_ptr<const SceneGuideFrame>guides;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>scene;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,4>ui;
};
void installFinalCompositeDraws(ID3D11DeviceContext*,void(*failure)(const char*)noexcept);
void beginFinalComposite(std::shared_ptr<const FinalCompositeInputs>);
void endFinalComposite();
}
