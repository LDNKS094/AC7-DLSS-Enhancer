#pragma once
#include "view_identity.h"
#include "guide_normalizer.h"
#include "../settings/nr_settings.h"
namespace ac7 {
class NrCapture;
struct SceneSrFrame;
struct SceneSettings {float scale=0;NrSettings nr{};float sharpness=0;};
struct SceneSrResult {ViewIdentity view;uint64_t token=0,sequence=0;uint32_t nativeCounter=0;ViewRect output{};bool submitted=false,modelProcessed=false,nrEnabled=false,srEnabled=false;float sharpness=0;unsigned nrWidth=0;};
bool installSceneSr(void(*execute)(SceneSrFrame*)noexcept,void(*error)(const char*)noexcept);
// Reserve model outputs before changing the View; native-only admission preserves its original grid.
bool admitSceneView(void*renderer,void*rhi,unsigned char*view,const SceneSettings&)noexcept;
SceneSrResult executeSceneSr(SceneSrFrame*,NrCapture*);
void invalidateSceneSr()noexcept;
}
