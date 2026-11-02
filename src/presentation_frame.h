#pragma once
#include <cstdint>
#include "fg_settings.h"

namespace sl {struct FrameToken;}
namespace ac7 {
struct PresentationFrame {
    uint64_t engineFrame;
    sl::FrameToken*sdkToken;
    bool pcl=false;
    FgSettings fg{};
    // Session mutex protects marker ordering across simulation and RHI threads.
    mutable unsigned markers=0;
};
}
