#pragma once
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <locale>
#include <sstream>
#include <string>
namespace ac7 {
struct NrSettings {
    bool enabled=false;
    float intensity=1.f,tone=.3f,structure=.7f;
    uint32_t style=0;
    bool peripheralCompression=false;
    float centerAspect=4.f/3.f,edgeScale=.5f,transitionWidth=.05f;
    bool operator==(const NrSettings&other) const{return enabled==other.enabled&&intensity==other.intensity&&tone==other.tone&&structure==other.structure&&style==other.style&&peripheralCompression==other.peripheralCompression&&centerAspect==other.centerAspect&&edgeScale==other.edgeScale&&transitionWidth==other.transitionWidth;}
};
// Load at startup or an explicit reload; missing/invalid keys retain the supplied base.
inline NrSettings loadNrSettings(const std::filesystem::path&path,unsigned&invalid,const NrSettings&base={}){
    NrSettings settings=base;invalid=0;std::array<wchar_t,4096>section{};
    const DWORD count=GetPrivateProfileSectionW(L"NeuralRendering",section.data(),DWORD(section.size()),path.c_str());
    if(count==section.size()-2){invalid++;return settings;}
    const wchar_t*names[]={L"EffectStrength",L"ToneStrength",L"DetailStrength",L"Preset",L"Enabled",L"OptimizeEdges",L"CenterAspectRatio",L"EdgeQuality",L"BlendWidth"};
    float*fields[]={&settings.intensity,&settings.tone,&settings.structure};
    for(const wchar_t*entry=section.data();*entry;entry+=std::wcslen(entry)+1){
        const std::wstring line(entry);const auto equal=line.find(L'=');if(equal==std::wstring::npos)continue;
        const auto key=line.substr(0,equal);const auto first=key.find_first_not_of(L" \t"),last=key.find_last_not_of(L" \t");if(first==std::wstring::npos)continue;const auto trimmed=key.substr(first,last-first+1);
        for(unsigned i=0;i<9;i++)if(_wcsicmp(trimmed.c_str(),names[i])==0){
            std::wistringstream input(line.substr(equal+1));input.imbue(std::locale::classic());float value=0;wchar_t trailing=0;
            const float minimum=i==6?.1f:i==7?.25f:0.f,maximum=i==3?2.f:i==6?8.f:i==8?.5f:1.f;
            if(!(input>>value)||(input>>trailing)||!std::isfinite(value)||value<minimum||value>maximum||(i>=3&&i<=5&&std::floor(value)!=value)){invalid++;break;}
            if(i==8)settings.transitionWidth=value;else if(i==7)settings.edgeScale=value;else if(i==6)settings.centerAspect=value;else if(i==5)settings.peripheralCompression=value!=0;else if(i==4)settings.enabled=value!=0;else if(i==3)settings.style=uint32_t(value);else *fields[i]=value;break;
        }
    }
    return settings;
}
}
