#pragma once
#include <cstdint>
#include "../settings/fg_settings.h"

namespace sl {struct FrameToken;}
namespace ac7 {
struct FrameContext {
    sl::FrameToken*sdkToken;
    bool pcl=false;
    FgSettings fg{};
    mutable float gameFpsLimit=0;
    mutable bool gameFpsLimitKnown=false;
    mutable bool allowsGeneration=false;
    // Session mutex protects marker ordering across simulation and RHI threads.
    mutable unsigned markers=0;
};
}
