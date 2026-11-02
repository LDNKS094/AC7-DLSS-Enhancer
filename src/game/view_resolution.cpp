#include "view_resolution.h"
#include "render_graph.h"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
namespace ac7 {
namespace {
using PreVisibility=void(__fastcall*)(void*,void*);
using Inverse=void*(__fastcall*)(void*,const void*);
using Recompute=void(__fastcall*)(void*);
PreVisibility originalVisibility=nullptr;
Inverse inverseProjection=nullptr;
Recompute originalRecompute=nullptr;
unsigned char*moduleBase=nullptr;
SceneSettings(*renderSettings)()=nullptr;
struct Injected {unsigned char*view;float baseX,baseY,x,y;bool native=false;};
thread_local std::vector<Injected>*active=nullptr;
float halton(unsigned sample,unsigned base){float value=0,weight=1;while(sample){weight/=base;value+=weight*(sample%base);sample/=base;}return value;}
bool desktopView(unsigned char*view,int*rect,int*output){
    if(view[0xc42]||view[0xc43]||view[0xc44])return false;
    int featureLevel=0;std::memcpy(&featureLevel,view+0x13e8,4);if(featureLevel!=3)return false;
    std::memcpy(rect,view+0x70,16);std::memcpy(output,view+0x80,16);
    for(unsigned i=0;i<4;i++)if(output[i]<0||output[i]>16384||rect[i]<0||rect[i]>16384)return false;
    return output[2]>output[0]&&output[3]>output[1]&&rect[2]>rect[0]&&rect[3]>rect[1];
}
void updateMatrices(unsigned char*view){
    auto*matrices=view+0xc0;alignas(16) std::array<float,16> inverse{};
    const auto*result=inverseProjection(inverse.data(),matrices);std::memcpy(matrices+0x40,result,64);
    originalRecompute(matrices);
}
void __fastcall recompute(void*matrices){
    // The native TAA path replaces jitter metadata before this exact call. Keep its sample, not two offsets.
    if(active&&_ReturnAddress()==moduleBase+0x112b5d4)for(auto&v:*active)if(v.view+0xc0==matrices){
        auto*m=static_cast<unsigned char*>(matrices);auto*p=reinterpret_cast<float*>(m);const auto*j=reinterpret_cast<const float*>(m+0x360);
        p[8]=v.baseX+j[0];p[9]=v.baseY+j[1];alignas(16) std::array<float,16> inverse{};const auto*result=inverseProjection(inverse.data(),m);std::memcpy(m+0x40,result,64);v.native=true;break;
    }
    originalRecompute(matrices);
}
void __fastcall visibility(void*renderer,void*rhi){
    auto settings=renderSettings();
    if(!settings.enabled){originalVisibility(renderer,rhi);return;}
    auto*b=static_cast<unsigned char*>(renderer);unsigned char*views=nullptr;int count=0;std::memcpy(&views,b+0xb8,8);std::memcpy(&count,b+0xc0,4);
    if(!views||count!=1){originalVisibility(renderer,rhi);return;}
    std::vector<Injected> injected;injected.reserve(size_t(count));
    for(int i=0;i<count;i++){
        auto*v=views+size_t(i)*0x27c0;int rect[4],output[4];if(!desktopView(v,rect,output))continue;
        auto*p=reinterpret_cast<float*>(v+0xc0);auto*j=reinterpret_cast<float*>(v+0x420);
        if(p[15]!=0||j[0]!=0||j[1]!=0)continue;
        unsigned char*family=nullptr;std::memcpy(&family,v,8);if(!family)continue;
        if(!admitSceneView(renderer,rhi,v,settings))continue;
        const float scale=settings.scale>0?settings.scale:1.f;
        if(scale==1.f)continue; // Native-size NR/RCAS does not need SR projection jitter.
        std::memcpy(rect,v+0x70,16);
        const int width=rect[2]-rect[0],height=rect[3]-rect[1],outWidth=output[2]-output[0],outHeight=output[3]-output[1];
        const unsigned phases=unsigned(std::round(8.f*(float(outWidth)/width)*(float(outHeight)/height)));
        unsigned frame=0;std::memcpy(&frame,family+0x68,4);
        const unsigned sample=frame%phases+1;const float x=halton(sample,2)-.5f,y=halton(sample,3)-.5f;
        injected.push_back({v,p[8],p[9],x,y});j[0]=2*x/width;j[1]=-2*y/height;p[8]+=j[0];p[9]+=j[1];updateMatrices(v);
    }
    auto*previous=active;active=&injected;originalVisibility(renderer,rhi);active=previous;
    // The original clears pixel jitter even with AA off; matrices and their history already contain our sample.
    for(const auto&v:injected)if(!v.native){const float pixels[]={v.x,v.y};std::memcpy(v.view+0xab4,pixels,8);}
}
}
bool installViewResolutionHook(SceneSettings(*settings)()){
    auto*base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));if(!base)return false;
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.SizeOfImage<0x19bf590)return false;
    // Native AC7 functions/offsets were read from this build; source-engine layouts are not substituted.
    constexpr unsigned char visibilitySig[]={0x40,0x55,0x53,0x41,0x54,0x41,0x57,0x48,0x8d,0xac,0x24,0x68,0xff,0xff,0xff,0x48,0x81,0xec,0x98,0x01,0,0};
    constexpr unsigned char inverseSig[]={0x40,0x53,0x48,0x83,0xec,0x50,0x0f,0x57,0xc9,0x48,0x8b,0xc2,0x0f,0x2e,0x4a,0x10,0x48,0x8b,0xd9};
    constexpr unsigned char recomputeSig[]={0x48,0x8b,0xc4,0x48,0x83,0xec,0x58,0x0f,0x28,0x91,0x80,0,0,0,0x0f,0x28,0xc2,0x0f,0x29,0x70,0xe8};
    constexpr unsigned char nativeRecomputeCall[]={0xe8,0x8c,0x03,0xee,0xff};
    if(std::memcmp(base+0x112afa0,visibilitySig,sizeof(visibilitySig))||std::memcmp(base+0xfbc150,inverseSig,sizeof(inverseSig))||std::memcmp(base+0x100b960,recomputeSig,sizeof(recomputeSig))||std::memcmp(base+0x112b5cf,nativeRecomputeCall,sizeof(nativeRecomputeCall)))return false;
    moduleBase=base;renderSettings=settings;inverseProjection=reinterpret_cast<Inverse>(base+0xfbc150);
    void*targets[]={base+0x112afa0,base+0x100b960};void*detours[]={reinterpret_cast<void*>(&visibility),reinterpret_cast<void*>(&recompute)};void**originals[]={reinterpret_cast<void**>(&originalVisibility),reinterpret_cast<void**>(&originalRecompute)};
    unsigned created=0;for(;created<2;created++)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK)break;
    if(created==2){for(unsigned i=0;i<2;i++)if(MH_QueueEnableHook(targets[i])!=MH_OK){for(auto*target:targets)MH_RemoveHook(target);return false;}return true;}
    while(created)MH_RemoveHook(targets[--created]);return false;
}
}
