#pragma once
#include <windows.h>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <locale>
#include <sstream>
namespace ac7 {
enum class SrMode {quality=0,balanced=1,performance=2};
inline float srScale(SrMode mode){return mode==SrMode::quality?2.f/3.f:mode==SrMode::balanced?.58f:.5f;}
inline const char*srModeName(SrMode mode){return mode==SrMode::quality?"Quality":mode==SrMode::balanced?"Balanced":"Performance";}
inline SrMode loadSrMode(const std::filesystem::path&path,unsigned&invalid,SrMode current=SrMode::quality){
    wchar_t value[64]{};invalid=0;const auto n=GetPrivateProfileStringW(L"SuperResolution",L"QualityMode",L"",value,64,path.c_str());
    if(!n)return current;
    if(n<63){
        wchar_t*first=value;while(*first==L' '||*first==L'\t')++first;auto*end=value+n;while(end>first&&(end[-1]==L' '||end[-1]==L'\t'))--end;*end=0;
        if(end==first+1&&*first>=L'0'&&*first<=L'2')return static_cast<SrMode>(*first-L'0');
    }
    invalid=1;return current;
}
inline bool loadSrEnabled(const std::filesystem::path&path,unsigned&invalid,bool current=true){
    wchar_t value[64]{};const auto n=GetPrivateProfileStringW(L"SuperResolution",L"Enabled",L"",value,64,path.c_str());if(!n)return current;
    if(n<63){
        wchar_t*first=value;while(*first==L' '||*first==L'\t')++first;auto*end=value+n;while(end>first&&(end[-1]==L' '||end[-1]==L'\t'))--end;*end=0;
        if(end==first+1&&(*first==L'0'||*first==L'1'))return *first==L'1';
    }
    ++invalid;return current;
}
inline float loadSrSharpness(const std::filesystem::path&path,unsigned&invalid,float current=0){
    wchar_t value[64]{};const auto n=GetPrivateProfileStringW(L"SuperResolution",L"Sharpness",L"",value,64,path.c_str());if(!n)return current;
    std::wistringstream input(value);input.imbue(std::locale::classic());float amount=0;wchar_t trailing=0;
    if(n==63||!(input>>amount)||(input>>trailing)||!std::isfinite(amount)||amount<0||amount>1){++invalid;return current;}return amount;
}
// Use the rendered frame's real grid, so queued Views retain their mode across an INI reload.
inline SrMode srModeForExtent(unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight){
    const float scale=(float(width)/outputWidth+float(height)/outputHeight)*.5f;
    SrMode best=SrMode::quality;
    for(auto mode:{SrMode::balanced,SrMode::performance})if(std::abs(scale-srScale(mode))<std::abs(scale-srScale(best)))best=mode;
    return best;
}
}
