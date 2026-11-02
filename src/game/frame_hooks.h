#pragma once
#include "../presentation/frame_context.h"
#include <memory>

namespace ac7 {
void installFrameHooks(void(*failure)(const char*)noexcept);
std::shared_ptr<const FrameContext>currentPresentationFrame();
std::shared_ptr<const FrameContext>takePresentationFrame();
}
