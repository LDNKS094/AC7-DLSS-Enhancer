#pragma once
#include "render_graph.h"
namespace ac7 {
// Changes only the 3D View rectangle; original AA and persistent settings stay with the game.
bool installViewResolutionHook(SceneSettings(*settings)());
}
