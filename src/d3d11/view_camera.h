#pragma once
#include "view_identity.h"
#include "../frame_camera.h"
#include <optional>

namespace ac7 {
std::optional<FrameCamera>readViewCamera(const ViewIdentity&,const unsigned char*image,bool panini,const char*&refusal);
}
