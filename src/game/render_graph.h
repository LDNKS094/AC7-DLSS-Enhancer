#pragma once
#include "view_identity.h"
#include "../rendering/guide_normalizer.h"
#include "../settings/nr_settings.h"
namespace ac7 {
struct SceneFrame;
struct SceneSettings {float scale=0;NrSettings nr{};float sharpness=0;bool enabled=false;};
bool installRenderGraph(void(*execute)(SceneFrame*)noexcept,void(*error)(const char*)noexcept);
// Reserve model outputs before changing the View; native-only admission preserves its original grid.
bool admitSceneView(void*renderer,void*rhi,unsigned char*view,SceneSettings&)noexcept;
void executeSceneFrame(SceneFrame*);
void invalidateSceneHistory()noexcept;
}
