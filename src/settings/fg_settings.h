#pragma once
#include "../fg_settings.h"
#include <windows.h>
#include <filesystem>
#include <limits>
#include <locale>
#include <sstream>

namespace ac7 {
inline FgSettings loadFgSettings(const std::filesystem::path&path,unsigned&invalid,FgSettings current={}){
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
}
}
