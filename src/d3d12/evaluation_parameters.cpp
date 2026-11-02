#include "evaluation_parameters.h"
#include <nvsdk_ngx_defs_dlssnr.h>
#include <string>
void configureModelEvaluation(NVSDK_NGX_Parameter* p,const std::array<ID3D12Resource*,5>& t,
    bool sr,bool reset,float frameMilliseconds,
    unsigned width,unsigned height,const ac7::NrSettings&settings) {
    if(sr) {
        const char* keys[]={NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_Output,NVSDK_NGX_Parameter_ExposureTexture};
        for(unsigned i=0;i<5;++i)p->Set(keys[i],t[i]);
        p->Set(NVSDK_NGX_Parameter_Reset,int(reset));
        p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
        p->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.f);p->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.f);
        p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.f);p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.f);
        if(frameMilliseconds>0)p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,frameMilliseconds);
        p->Set(NVSDK_NGX_Parameter_Sharpness,0.f);
        p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,width);p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,height);
    } else {
        const char* keys[]={NVSDK_NGX_Parameter_DLSSNR_Color,NVSDK_NGX_Parameter_DLSSNR_Depth,NVSDK_NGX_Parameter_DLSSNR_MVec,NVSDK_NGX_Parameter_DLSSNR_Output};
        for(unsigned i=0;i<4;++i){p->Set(keys[i],t[i]);const std::string k(keys[i]);p->Set((k+"SubrectBaseX").c_str(),0u);p->Set((k+"SubrectBaseY").c_str(),0u);p->Set((k+"SubrectWidth").c_str(),width);p->Set((k+"SubrectHeight").c_str(),height);}
        p->Set(NVSDK_NGX_Parameter_DLSSNR_Enabled,1);p->Set(NVSDK_NGX_Parameter_DLSSNR_Reset,int(reset));
        p->Set(NVSDK_NGX_Parameter_DLSSNR_DepthInverted,1);
        p->Set(NVSDK_NGX_Parameter_DLSSNR_MVecScaleX,1.f);p->Set(NVSDK_NGX_Parameter_DLSSNR_MVecScaleY,1.f);
        p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
        p->Set(NVSDK_NGX_Parameter_DLSSNR_Intensity,settings.intensity);
        p->Set(NVSDK_NGX_Parameter_DLSSNR_LocalToneStrength,settings.tone);
        p->Set(NVSDK_NGX_Parameter_DLSSNR_LocalStructureStrength,settings.structure);
        // No skin-mask workflow is exposed; preserve the previous default SDK value.
        p->Set(NVSDK_NGX_Parameter_DLSSNR_SkinStructureStrength,.5f);
        p->Set(NVSDK_NGX_Parameter_DLSSNR_Style,settings.style);p->Set(NVSDK_NGX_Parameter_DLSSNR_UseAutoMask,0);
        p->Set(NVSDK_NGX_Parameter_DLSSNR_MaskBlendMode,0);p->Set(NVSDK_NGX_Parameter_DLSSNR_UICorrection,0);
        for(const char* k:{NVSDK_NGX_Parameter_DLSSNR_ControlMask,NVSDK_NGX_Parameter_DLSSNR_Backbuffer,NVSDK_NGX_Parameter_DLSSNR_UI,NVSDK_NGX_Parameter_DLSSNR_UIAlpha,NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionField})p->Set(k,static_cast<ID3D12Resource*>(nullptr));
    }
}
