#pragma once
#include "view_identity.h"
#include "../presentation/camera_state.h"
#include <optional>

namespace ac7 {
std::optional<CameraState>readViewCamera(const ViewIdentity&,const unsigned char*image,bool panini,const char*&refusal);
}
