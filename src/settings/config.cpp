#include "config.h"
#include <windows.h>
#include <array>
#include <cwchar>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
namespace ac7 {
namespace {
SrMode loadSrMode(const std::filesystem::path&path,unsigned&invalid,SrMode current=SrMode::quality){
    wchar_t value[64]{};invalid=0;const auto n=GetPrivateProfileStringW(L"SuperResolution",L"QualityMode",L"",value,64,path.c_str());
    if(!n)return current;
    if(n<63){
        wchar_t*first=value;while(*first==L' '||*first==L'\t')++first;auto*end=value+n;while(end>first&&(end[-1]==L' '||end[-1]==L'\t'))--end;*end=0;
        if(end==first+1&&*first>=L'0'&&*first<=L'2')return static_cast<SrMode>(*first-L'0');
    }
    invalid=1;return current;
}
bool loadSrEnabled(const std::filesystem::path&path,unsigned&invalid,bool current=true){
    wchar_t value[64]{};const auto n=GetPrivateProfileStringW(L"SuperResolution",L"Enabled",L"",value,64,path.c_str());if(!n)return current;
    if(n<63){
        wchar_t*first=value;while(*first==L' '||*first==L'\t')++first;auto*end=value+n;while(end>first&&(end[-1]==L' '||end[-1]==L'\t'))--end;*end=0;
        if(end==first+1&&(*first==L'0'||*first==L'1'))return *first==L'1';
    }
    ++invalid;return current;
}
float loadSrSharpness(const std::filesystem::path&path,unsigned&invalid,float current=0){
    wchar_t value[64]{};const auto n=GetPrivateProfileStringW(L"SuperResolution",L"Sharpness",L"",value,64,path.c_str());if(!n)return current;
    std::wistringstream input(value);input.imbue(std::locale::classic());float amount=0;wchar_t trailing=0;
    if(n==63||!(input>>amount)||(input>>trailing)||!std::isfinite(amount)||amount<0||amount>1){++invalid;return current;}return amount;
}
}
SrSettings loadSrSettings(const std::filesystem::path&path,unsigned&invalid,SrSettings current){
    current.qualityMode=loadSrMode(path,invalid,current.qualityMode);
    current.enabled=loadSrEnabled(path,invalid,current.enabled);
    current.sharpness=loadSrSharpness(path,invalid,current.sharpness);
    return current;
}
// Load at startup or an explicit reload; missing/invalid keys retain the supplied base.
NrSettings loadNrSettings(const std::filesystem::path&path,unsigned&invalid,const NrSettings&base){
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
}FgSettings loadFgSettings(const std::filesystem::path&path,unsigned&invalid,FgSettings current){
    invalid=0;
    const auto value=[&](const wchar_t*key,unsigned previous,bool toggle){
        wchar_t text[64]{};const auto n=GetPrivateProfileStringW(L"FrameGeneration",key,L"",text,64,path.c_str());
        if(!n)return previous;
        std::wistringstream input(text);input.imbue(std::locale::classic());input>>std::ws;
        unsigned long long number=0;wchar_t trailing=0;
        if(n==63||input.peek()==L'-'||!(input>>number)||(input>>trailing)||number>std::numeric_limits<unsigned>::max()||
            (toggle&&number>1)){++invalid;return previous;}
        return unsigned(number);
    };
    current.enabled=value(L"Enabled",unsigned(current.enabled),true)!=0;
    current.multiplier=value(L"Multiplier",current.multiplier,false);
    return current;
}}
