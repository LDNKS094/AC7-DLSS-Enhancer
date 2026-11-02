#pragma once
#include <d3d11.h>

namespace ac7 {
// Install synchronously before the loader returns to the game. Creates no graphics objects.
void installPresentationEntry(void(*failure)(const char*)noexcept);
}
