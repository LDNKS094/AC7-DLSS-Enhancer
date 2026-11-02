#pragma once
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <array>
#include "../settings/nr_settings.h"
// Order: color, depth, backward-pixel motion, output, exposure (SR only).
void configureModelEvaluation(NVSDK_NGX_Parameter*, const std::array<ID3D12Resource*,5>&,
    bool sr, bool reset, float frameMilliseconds,
    unsigned width, unsigned height, const ac7::NrSettings& settings = {});
