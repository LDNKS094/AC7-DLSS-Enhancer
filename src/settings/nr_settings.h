#pragma once
#include <cstdint>
namespace ac7 {
struct NrSettings {
    bool enabled=false;
    float intensity=1.f,tone=.3f,structure=.7f;
    uint32_t style=0;
    bool peripheralCompression=false;
    float centerAspect=4.f/3.f,edgeScale=.5f,transitionWidth=.05f;
    bool operator==(const NrSettings&other) const{return enabled==other.enabled&&intensity==other.intensity&&tone==other.tone&&structure==other.structure&&style==other.style&&peripheralCompression==other.peripheralCompression&&centerAspect==other.centerAspect&&edgeScale==other.edgeScale&&transitionWidth==other.transitionWidth;}
};
}
