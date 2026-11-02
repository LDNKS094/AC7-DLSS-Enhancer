#pragma once
#include "../presentation_frame.h"
#include <memory>

namespace ac7 {
void installFrameRouting(void(*failure)(const char*)noexcept);
std::shared_ptr<const PresentationFrame>currentPresentationFrame();
std::shared_ptr<const PresentationFrame>takePresentationFrame();
}
