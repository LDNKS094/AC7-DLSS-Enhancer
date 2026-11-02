#pragma once
#include <cstdint>

namespace sl {struct FrameToken;}
namespace ac7 {
struct PresentationFrame {
    uint64_t engineFrame;
    sl::FrameToken*sdkToken;
    bool pcl=false;
    // Session mutex protects marker ordering across simulation and RHI threads.
    mutable unsigned markers=0;
};
}
