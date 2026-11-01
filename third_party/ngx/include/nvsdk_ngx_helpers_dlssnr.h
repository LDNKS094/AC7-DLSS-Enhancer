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

#ifndef NVSDK_NGX_HELPERS_DLSSNR_H
#define NVSDK_NGX_HELPERS_DLSSNR_H
#pragma once

#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_defs_dlssnr.h"

// pInParams: NGX capability parameter block containing DLSSNRGetStatsCallback.
// pVRAMAllocatedBytes: receives allocated VRAM size in bytes.
// pOptLevel: receives snippet optimization level.
// IsDevSnippetBranch: receives 1 for dev snippet branch, 0 otherwise.
static inline NVSDK_NGX_Result NGX_DLSSNR_GET_STATS_2(
    NVSDK_NGX_Parameter *pInParams,
    unsigned long long *pVRAMAllocatedBytes,
    unsigned int *pOptLevel, unsigned int *IsDevSnippetBranch)
{
    void *Callback = NULL;
    NVSDK_NGX_Parameter_GetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNRGetStatsCallback, &Callback);
    if (!Callback)
    {
        // Possible reasons for this:
        // - Installed DLSSNR is out of date and does not support the feature we need
        // - You used NVSDK_NGX_AllocateParameters() for creating InParams. Try using NVSDK_NGX_GetCapabilityParameters() instead
        return NVSDK_NGX_Result_FAIL_OutOfDate;
    }

    NVSDK_NGX_Result Res = NVSDK_NGX_Result_Success;
    PFN_NVSDK_NGX_DLSSNR_GetStatsCallback PFNCallback = (PFN_NVSDK_NGX_DLSSNR_GetStatsCallback)Callback;
    Res = PFNCallback(pInParams);
    if (NVSDK_NGX_FAILED(Res))
    {
        return Res;
    }
    NVSDK_NGX_Parameter_GetULL(pInParams, NVSDK_NGX_Parameter_SizeInBytes, pVRAMAllocatedBytes);
    NVSDK_NGX_Parameter_GetUI(pInParams, NVSDK_NGX_EParameter_OptLevel, pOptLevel);
    NVSDK_NGX_Parameter_GetUI(pInParams, NVSDK_NGX_EParameter_IsDevSnippetBranch, IsDevSnippetBranch);
    return Res;
}

// pInParams: NGX capability parameter block containing DLSSNRGetStatsCallback.
// pVRAMAllocatedBytes: receives allocated VRAM size in bytes.
// pOptLevel: receives snippet optimization level.
static inline NVSDK_NGX_Result NGX_DLSSNR_GET_STATS_1(
    NVSDK_NGX_Parameter *pInParams,
    unsigned long long *pVRAMAllocatedBytes,
    unsigned int *pOptLevel)
{
    unsigned int dummy = 0;
    return NGX_DLSSNR_GET_STATS_2(pInParams, pVRAMAllocatedBytes, pOptLevel, &dummy);
}

// pInParams: NGX capability parameter block containing DLSSNRGetStatsCallback.
// pVRAMAllocatedBytes: receives allocated VRAM size in bytes.
static inline NVSDK_NGX_Result NGX_DLSSNR_GET_STATS(
    NVSDK_NGX_Parameter *pInParams,
    unsigned long long *pVRAMAllocatedBytes)
{
    unsigned int dummy = 0;
    return NGX_DLSSNR_GET_STATS_2(pInParams, pVRAMAllocatedBytes, &dummy, &dummy);
}

// pInParams: NGX capability/evaluate/create parameter block.
// InPerfQualityValue: DLSS SDK performance mode used to derive DLSSNR.ScalingRatio.
static inline NVSDK_NGX_Result NGX_DLSSNR_SET_SCALING_RATIO(
    NVSDK_NGX_Parameter *pInParams,
    NVSDK_NGX_PerfQuality_Value InPerfQualityValue)
{
    void *Callback = NULL;
    NVSDK_NGX_Parameter_GetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNRComputeScalingRatioCallback, &Callback);
    if (!Callback)
    {
        // Possible reasons for this:
        // - Installed DLSSNR is out of date and does not support the feature we need
        // - You used NVSDK_NGX_AllocateParameters() for creating InParams. Try using NVSDK_NGX_GetCapabilityParameters() instead
        return NVSDK_NGX_Result_FAIL_OutOfDate;
    }

    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_PerfQualityValue, InPerfQualityValue);

    PFN_NVSDK_NGX_DLSSNR_ComputeScalingRatioCallback PFNCallback = (PFN_NVSDK_NGX_DLSSNR_ComputeScalingRatioCallback)Callback;
    NVSDK_NGX_Result Res = PFNCallback(pInParams);
    if (NVSDK_NGX_FAILED(Res))
    {
        return Res;
    }

    return Res;
}

#include "nvsdk_ngx_helpers_dlssnr_d3d.h"

#endif // NVSDK_NGX_HELPERS_DLSSNR_H
