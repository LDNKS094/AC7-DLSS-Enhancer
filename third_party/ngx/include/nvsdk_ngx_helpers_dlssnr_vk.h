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

#ifndef NVSDK_NGX_HELPERS_DLSSNR_VK_H
#define NVSDK_NGX_HELPERS_DLSSNR_VK_H
#pragma once

#include "nvsdk_ngx_defs_dlssnr.h"
#include "nvsdk_ngx_helpers_vk.h"

typedef struct NVSDK_NGX_VK_DLSSNR_Eval_Params
{
    NVSDK_NGX_Resource_VK*              pInColor;
    NVSDK_NGX_Resource_VK*              pInOutput;
    NVSDK_NGX_Resource_VK*              pInBackbuffer;
    NVSDK_NGX_Resource_VK*              pInMVec;
    NVSDK_NGX_Resource_VK*              pInDepth;
    NVSDK_NGX_Resource_VK*              pInBidirectionalDistortionField;
    NVSDK_NGX_Resource_VK*              pInControlMask;
    NVSDK_NGX_Resource_VK*              pInUI;
    NVSDK_NGX_Resource_VK*              pInUIAlpha;
    float                               InLocalToneStrength;
    float                               InLocalStructureStrength;
    unsigned int                        InStyle;
    int                                 InUseAutoMask;
    NVSDK_NGX_DLSSNR_Mask_Blend_Mode    InMaskBlendMode;
    float                               InSkinStructureStrength;
    int                                 InReset;
    int                                 InDepthInverted;
    int                                 InEnabled;
    int                                 InUICorrection;
    float                               InIntensity;
    float                               InMVecScaleX;
    float                               InMVecScaleY;
    NVSDK_NGX_Coordinates               InColorSubrectBase;
    NVSDK_NGX_Dimensions                InColorSubrectSize;
    NVSDK_NGX_Coordinates               InOutputSubrectBase;
    NVSDK_NGX_Dimensions                InOutputSubrectSize;
    NVSDK_NGX_Coordinates               InBackbufferSubrectBase;
    NVSDK_NGX_Dimensions                InBackbufferSubrectSize;
    NVSDK_NGX_Coordinates               InMVecSubrectBase;
    NVSDK_NGX_Dimensions                InMVecSubrectSize;
    NVSDK_NGX_Coordinates               InDepthSubrectBase;
    NVSDK_NGX_Dimensions                InDepthSubrectSize;
    NVSDK_NGX_Coordinates               InBidirectionalDistortionFieldSubrectBase;
    NVSDK_NGX_Dimensions                InBidirectionalDistortionFieldSubrectSize;
    NVSDK_NGX_Coordinates               InControlMaskSubrectBase;
    NVSDK_NGX_Dimensions                InControlMaskSubrectSize;
    NVSDK_NGX_Coordinates               InUISubrectBase;
    NVSDK_NGX_Dimensions                InUISubrectSize;
    NVSDK_NGX_Coordinates               InUIAlphaSubrectBase;
    NVSDK_NGX_Dimensions                InUIAlphaSubrectSize;
} NVSDK_NGX_VK_DLSSNR_Eval_Params;

static inline NVSDK_NGX_Result NGX_VULKAN_CREATE_DLSSNR_EXT1(
    VkDevice InDevice,
    VkCommandBuffer InCmdList,
    unsigned int InCreationNodeMask,
    unsigned int InVisibilityNodeMask,
    NVSDK_NGX_Handle **ppOutHandle,
    NVSDK_NGX_Parameter *pInParams,
    NVSDK_NGX_DLSSNR_Create_Params *pInDLSSNRCreateParams)
{
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_Width, pInDLSSNRCreateParams->Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_Height, pInDLSSNRCreateParams->Height);

    if (InDevice) return NVSDK_NGX_VULKAN_CreateFeature1(InDevice, InCmdList, NVSDK_NGX_Feature_DLSSNR, pInParams, ppOutHandle);
    else return NVSDK_NGX_VULKAN_CreateFeature(InCmdList, NVSDK_NGX_Feature_DLSSNR, pInParams, ppOutHandle);
}

static inline NVSDK_NGX_Result NGX_VULKAN_CREATE_DLSSNR_EXT(
    VkCommandBuffer InCmdList,
    unsigned int InCreationNodeMask,
    unsigned int InVisibilityNodeMask,
    NVSDK_NGX_Handle **ppOutHandle,
    NVSDK_NGX_Parameter *pInParams,
    NVSDK_NGX_DLSSNR_Create_Params *pInDLSSNRCreateParams)
{
    return NGX_VULKAN_CREATE_DLSSNR_EXT1(NULL, InCmdList, InCreationNodeMask, InVisibilityNodeMask, ppOutHandle, pInParams, pInDLSSNRCreateParams);
}

static inline NVSDK_NGX_Result NGX_VULKAN_EVALUATE_DLSSNR_EXT(
    VkCommandBuffer InCmdList,
    NVSDK_NGX_Handle *pInHandle,
    NVSDK_NGX_Parameter *pInParams,
    NVSDK_NGX_VK_DLSSNR_Eval_Params *pInDLSSNREvalParams)
{
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_Color, pInDLSSNREvalParams->pInColor);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_Output, pInDLSSNREvalParams->pInOutput);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_Backbuffer, pInDLSSNREvalParams->pInBackbuffer);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVec, pInDLSSNREvalParams->pInMVec);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_Depth, pInDLSSNREvalParams->pInDepth);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionField, pInDLSSNREvalParams->pInBidirectionalDistortionField);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_ControlMask, pInDLSSNREvalParams->pInControlMask);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_UI, pInDLSSNREvalParams->pInUI);
    NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_Parameter_DLSSNR_UIAlpha, pInDLSSNREvalParams->pInUIAlpha);
    NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_Parameter_DLSSNR_LocalToneStrength, pInDLSSNREvalParams->InLocalToneStrength);
    NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_Parameter_DLSSNR_LocalStructureStrength, pInDLSSNREvalParams->InLocalStructureStrength);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_Style, pInDLSSNREvalParams->InStyle);
    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UseAutoMask, pInDLSSNREvalParams->InUseAutoMask);
    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_DLSSNR_MaskBlendMode, (int)pInDLSSNREvalParams->InMaskBlendMode);
    NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_Parameter_DLSSNR_SkinStructureStrength, pInDLSSNREvalParams->InSkinStructureStrength);
    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_DLSSNR_Reset, pInDLSSNREvalParams->InReset);
    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_DLSSNR_DepthInverted, pInDLSSNREvalParams->InDepthInverted);
    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_DLSSNR_Enabled, pInDLSSNREvalParams->InEnabled);
    NVSDK_NGX_Parameter_SetI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UICorrection, pInDLSSNREvalParams->InUICorrection);
    NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_Parameter_DLSSNR_Intensity, pInDLSSNREvalParams->InIntensity);
    NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVecScaleX, pInDLSSNREvalParams->InMVecScaleX);
    NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVecScaleY, pInDLSSNREvalParams->InMVecScaleY);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseX, pInDLSSNREvalParams->InColorSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectBaseY, pInDLSSNREvalParams->InColorSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectWidth, pInDLSSNREvalParams->InColorSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ColorSubrectHeight, pInDLSSNREvalParams->InColorSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseX, pInDLSSNREvalParams->InOutputSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectBaseY, pInDLSSNREvalParams->InOutputSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectWidth, pInDLSSNREvalParams->InOutputSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_OutputSubrectHeight, pInDLSSNREvalParams->InOutputSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectBaseX, pInDLSSNREvalParams->InBackbufferSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectBaseY, pInDLSSNREvalParams->InBackbufferSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectWidth, pInDLSSNREvalParams->InBackbufferSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BackbufferSubrectHeight, pInDLSSNREvalParams->InBackbufferSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseX, pInDLSSNREvalParams->InMVecSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectBaseY, pInDLSSNREvalParams->InMVecSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectWidth, pInDLSSNREvalParams->InMVecSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_MVecSubrectHeight, pInDLSSNREvalParams->InMVecSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseX, pInDLSSNREvalParams->InDepthSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectBaseY, pInDLSSNREvalParams->InDepthSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectWidth, pInDLSSNREvalParams->InDepthSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_DepthSubrectHeight, pInDLSSNREvalParams->InDepthSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectBaseX, pInDLSSNREvalParams->InBidirectionalDistortionFieldSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectBaseY, pInDLSSNREvalParams->InBidirectionalDistortionFieldSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectWidth, pInDLSSNREvalParams->InBidirectionalDistortionFieldSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionFieldSubrectHeight, pInDLSSNREvalParams->InBidirectionalDistortionFieldSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectBaseX, pInDLSSNREvalParams->InControlMaskSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectBaseY, pInDLSSNREvalParams->InControlMaskSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectWidth, pInDLSSNREvalParams->InControlMaskSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_ControlMaskSubrectHeight, pInDLSSNREvalParams->InControlMaskSubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UISubrectBaseX, pInDLSSNREvalParams->InUISubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UISubrectBaseY, pInDLSSNREvalParams->InUISubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UISubrectWidth, pInDLSSNREvalParams->InUISubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UISubrectHeight, pInDLSSNREvalParams->InUISubrectSize.Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectBaseX, pInDLSSNREvalParams->InUIAlphaSubrectBase.X);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectBaseY, pInDLSSNREvalParams->InUIAlphaSubrectBase.Y);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectWidth, pInDLSSNREvalParams->InUIAlphaSubrectSize.Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_DLSSNR_UIAlphaSubrectHeight, pInDLSSNREvalParams->InUIAlphaSubrectSize.Height);
    return NVSDK_NGX_VULKAN_EvaluateFeature_C(InCmdList, pInHandle, pInParams, NULL);
}

#endif // NVSDK_NGX_HELPERS_DLSSNR_VK_H
