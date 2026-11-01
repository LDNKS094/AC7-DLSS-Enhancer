/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVSDK_NGX_DEFS_DLSSNR_H
#define NVSDK_NGX_DEFS_DLSSNR_H
#pragma once

#include "nvsdk_ngx_defs.h"

// Feature ID - reuses the former CG2R Reserved18 slot for the renamed feature.
// Defined as a macro (not constexpr) because this header is consumed by the
// VS2010 SDK lib build, which predates C++11 constexpr.
#define NVSDK_NGX_Feature_DLSSNR ((NVSDK_NGX_Feature)NVSDK_NGX_Feature_Reserved18)

typedef struct NVSDK_NGX_DLSSNR_Create_Params
{
    unsigned int                        Width;
    unsigned int                        Height;
} NVSDK_NGX_DLSSNR_Create_Params;

// Callback function types
// InParams: NGX parameter block used for callback input and stats output.
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_DLSSNR_GetStatsCallback)(NVSDK_NGX_Parameter *InParams);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_DLSSNR_ComputeScalingRatioCallback)(NVSDK_NGX_Parameter *InParams);

// ---------------------------------------------------------------------------
// Render preset hint
//
// Preset_Default lets the SDK pick the preset.  The choice is fixed by the SDK
// version the application links against, so it does not change with a driver
// update or OTA; adopting a newer default requires rebuilding against a newer
// SDK.
//
// Select a lettered preset to pin a specific model rather than tracking the
// SDK's current recommendation.
// ---------------------------------------------------------------------------
typedef enum NVSDK_NGX_DLSSNR_Hint_Render_Preset
{
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_Default = 0,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_A       = 1,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_B       = 2,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_C       = 3,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_D       = 4,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_E       = 5,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_F       = 6,
    NVSDK_NGX_DLSSNR_Hint_Render_Preset_G       = 7,
} NVSDK_NGX_DLSSNR_Hint_Render_Preset;

// Controls how the manual control mask and skin automask are combined when
// both are enabled. None preserves manual-mask precedence and is the default
// for zero-initialized evaluate params.
typedef enum NVSDK_NGX_DLSSNR_Mask_Blend_Mode
{
    NVSDK_NGX_DLSSNR_Mask_Blend_Mode_None = 0,
    NVSDK_NGX_DLSSNR_Mask_Blend_Mode_Mean = 1,
    NVSDK_NGX_DLSSNR_Mask_Blend_Mode_Min  = 2,
    NVSDK_NGX_DLSSNR_Mask_Blend_Mode_Max  = 3,
} NVSDK_NGX_DLSSNR_Mask_Blend_Mode;

// ---------------------------------------------------------------------------
// Capability / query parameters
// ---------------------------------------------------------------------------
#define NVSDK_NGX_Parameter_DLSSNR_Available              "DLSSNR.Available"
#define NVSDK_NGX_Parameter_DLSSNR_NeedsUpdatedDriver     "DLSSNR.NeedsUpdatedDriver"
#define NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMajor  "DLSSNR.MinDriverVersionMajor"
#define NVSDK_NGX_Parameter_DLSSNR_MinDriverVersionMinor  "DLSSNR.MinDriverVersionMinor"
#define NVSDK_NGX_Parameter_DLSSNR_FeatureInitResult      "DLSSNR.FeatureInitResult"

// Stats callback. Registered by the snippet during PopulateParameters. On success,
// it writes aggregate snippet stats into InParams:
//   - NVSDK_NGX_Parameter_SizeInBytes
//   - NVSDK_NGX_EParameter_OptLevel
//   - NVSDK_NGX_EParameter_IsDevSnippetBranch
#define NVSDK_NGX_Parameter_DLSSNRGetStatsCallback        "DLSSNRGetStatsCallback"

// Scaling-ratio computation callback. Registered by the snippet during PopulateParameters.
// Input:
//   - NVSDK_NGX_Parameter_PerfQualityValue
// Output:
//   - NVSDK_NGX_Parameter_DLSSNR_ScalingRatio
#define NVSDK_NGX_Parameter_DLSSNRComputeScalingRatioCallback "DLSSNRComputeScalingRatioCallback"

// ---------------------------------------------------------------------------
// Creation parameters
// ---------------------------------------------------------------------------
#define NVSDK_NGX_Parameter_DLSSNR_Width                  "DLSSNR.Width"
#define NVSDK_NGX_Parameter_DLSSNR_Height                 "DLSSNR.Height"
#define NVSDK_NGX_Parameter_DLSSNR_Hint_Render_Preset     "DLSSNR.Hint.Render.Preset"

// ---------------------------------------------------------------------------
// Evaluate: resource parameters + subrects
// ---------------------------------------------------------------------------

// Color input
#define NVSDK_NGX_Parameter_DLSSNR_Color                      "DLSSNR.Color"
#define NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseX          "DLSSNR.ColorSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseY          "DLSSNR.ColorSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_ColorSubrectWidth          "DLSSNR.ColorSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_ColorSubrectHeight         "DLSSNR.ColorSubrectHeight"

// Motion vectors
#define NVSDK_NGX_Parameter_DLSSNR_MVec                       "DLSSNR.MVec"
#define NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseX           "DLSSNR.MVecSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseY           "DLSSNR.MVecSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_MVecSubrectWidth           "DLSSNR.MVecSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_MVecSubrectHeight          "DLSSNR.MVecSubrectHeight"
#define NVSDK_NGX_Parameter_DLSSNR_MVecScaleX                 "DLSSNR.MVecScaleX"
#define NVSDK_NGX_Parameter_DLSSNR_MVecScaleY                 "DLSSNR.MVecScaleY"

// Depth
#define NVSDK_NGX_Parameter_DLSSNR_Depth                      "DLSSNR.Depth"
#define NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseX          "DLSSNR.DepthSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseY          "DLSSNR.DepthSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_DepthSubrectWidth          "DLSSNR.DepthSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_DepthSubrectHeight         "DLSSNR.DepthSubrectHeight"

// Output (/ Backbuffer)
#define NVSDK_NGX_Parameter_DLSSNR_Output                     "DLSSNR.Output"
#define NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseX         "DLSSNR.OutputSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseY         "DLSSNR.OutputSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_OutputSubrectWidth         "DLSSNR.OutputSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_OutputSubrectHeight        "DLSSNR.OutputSubrectHeight"

// Control Mask (4-channel: R=intensity mask, G=local tone strength mask, B=local structure strength mask, A=unused)
#define NVSDK_NGX_Parameter_DLSSNR_ControlMask                "DLSSNR.ControlMask"
#define NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectBaseX    "DLSSNR.ControlMaskSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectBaseY    "DLSSNR.ControlMaskSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectWidth    "DLSSNR.ControlMaskSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectHeight   "DLSSNR.ControlMaskSubrectHeight"

// ---------------------------------------------------------------------------
// Optional buffers for end of pipeline evaluation (ex. right before DLSS-FG).
// These should not be necessary for most integrations which should run earlier.
// ---------------------------------------------------------------------------

// Backbuffer (when passed, usually == Output)
#define NVSDK_NGX_Parameter_DLSSNR_Backbuffer                    "DLSSNR.Backbuffer"
#define NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectBaseX        "DLSSNR.BackbufferSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectBaseY        "DLSSNR.BackbufferSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectWidth        "DLSSNR.BackbufferSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectHeight       "DLSSNR.BackbufferSubrectHeight"

// Bidirectional distortion field
#define NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionField              "DLSSNR.BidirectionalDistortionField"
#define NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectBaseX  "DLSSNR.BidirectionalDistortionFieldSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectBaseY  "DLSSNR.BidirectionalDistortionFieldSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectWidth  "DLSSNR.BidirectionalDistortionFieldSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectHeight "DLSSNR.BidirectionalDistortionFieldSubrectHeight"

// UI Color+Alpha
#define NVSDK_NGX_Parameter_DLSSNR_UI                         "DLSSNR.UI"
#define NVSDK_NGX_Parameter_DLSSNR_UISubrectBaseX             "DLSSNR.UISubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_UISubrectBaseY             "DLSSNR.UISubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_UISubrectWidth             "DLSSNR.UISubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_UISubrectHeight            "DLSSNR.UISubrectHeight"

// UI Alpha
#define NVSDK_NGX_Parameter_DLSSNR_UIAlpha                    "DLSSNR.UIAlpha"
#define NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectBaseX        "DLSSNR.UIAlphaSubrectBaseX"
#define NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectBaseY        "DLSSNR.UIAlphaSubrectBaseY"
#define NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectWidth        "DLSSNR.UIAlphaSubrectWidth"
#define NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectHeight       "DLSSNR.UIAlphaSubrectHeight"

// ---------------------------------------------------------------------------
// Evaluate: control parameters
// ---------------------------------------------------------------------------
#define NVSDK_NGX_Parameter_DLSSNR_Enabled                    "DLSSNR.Enabled"
#define NVSDK_NGX_Parameter_DLSSNR_Reset                      "DLSSNR.Reset"
#define NVSDK_NGX_Parameter_DLSSNR_DepthInverted              "DLSSNR.DepthInverted"
#define NVSDK_NGX_Parameter_DLSSNR_UICorrection               "DLSSNR.UICorrection"
#define NVSDK_NGX_Parameter_DLSSNR_Intensity                  "DLSSNR.Intensity"
// Optional. Pass at CreateFeature to size network capacity. Passing a different
// value at EvaluateFeature is supported, but recreating the feature at the new
// ratio is preferred for optimal VRAM use.
#define NVSDK_NGX_Parameter_DLSSNR_ScalingRatio               "DLSSNR.ScalingRatio"
#define NVSDK_NGX_Parameter_DLSSNR_LocalToneStrength          "DLSSNR.LocalToneStrength"
#define NVSDK_NGX_Parameter_DLSSNR_LocalStructureStrength     "DLSSNR.LocalStructureStrength"
#define NVSDK_NGX_Parameter_DLSSNR_UseAutoMask                "DLSSNR.UseAutoMask"
#define NVSDK_NGX_Parameter_DLSSNR_MaskBlendMode              "DLSSNR.MaskBlendMode"
#define NVSDK_NGX_Parameter_DLSSNR_SkinStructureStrength      "DLSSNR.SkinStructureStrength"
#define NVSDK_NGX_Parameter_DLSSNR_Style                      "DLSSNR.Style"

#endif // NVSDK_NGX_DEFS_DLSSNR_H
