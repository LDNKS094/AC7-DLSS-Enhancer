#pragma once
#include <array>

namespace ac7 {
struct FrameCamera {
    std::array<float,16>viewToClip{},clipToView{},clipToPrevious{},previousToClip{};
    std::array<float,3>position{},forward{},up{},right{};
    std::array<float,2>jitter{};
    float nearPlane=0,farPlane=0,verticalFov=0,aspect=0;
    bool cameraCut=false;
};
}
