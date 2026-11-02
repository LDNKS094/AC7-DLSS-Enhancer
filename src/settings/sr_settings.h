#pragma once
#include <cmath>
#include <initializer_list>
namespace ac7 {
enum class SrMode {quality=0,balanced=1,performance=2};
inline float srScale(SrMode mode){return mode==SrMode::quality?2.f/3.f:mode==SrMode::balanced?.58f:.5f;}
inline const char*srModeName(SrMode mode){return mode==SrMode::quality?"Quality":mode==SrMode::balanced?"Balanced":"Performance";}
struct SrSettings {
    bool enabled=true;
    SrMode qualityMode=SrMode::quality;
    float sharpness=0;
};
// Use the rendered frame's real grid, so queued Views retain their mode across an INI reload.
inline SrMode srModeForExtent(unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight){
    const float scale=(float(width)/outputWidth+float(height)/outputHeight)*.5f;
    SrMode best=SrMode::quality;
    for(auto mode:{SrMode::balanced,SrMode::performance})if(std::abs(scale-srScale(mode))<std::abs(scale-srScale(best)))best=mode;
    return best;
}}
