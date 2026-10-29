#pragma once
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <array>
// Existing captured diagnostic policy shared by file, shared-feature and full GPU-chain probes.
// Order: color, depth, backward-pixel motion, output, exposure (SR only). Nonpositive time leaves legacy SR time unset.
void configureCapturedEvaluation(NVSDK_NGX_Parameter*, const std::array<ID3D12Resource*,5>&,
    bool sr, bool reset, bool zeroStrength, float frameMilliseconds, bool diagnostics = true);
