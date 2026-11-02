#pragma once
#include <d3d11.h>
#include <array>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <utility>
namespace ac7 {
struct ViewIdentity {
    uint32_t counter=0;
    bool cameraCut=false;
    std::array<float,2> previous{},current{},jitter{};
    std::array<float,4> rect{};
    std::array<uint32_t,2> allocation{};
    std::array<unsigned char,4096> bytes{};
    bool operator==(const ViewIdentity&other) const {return bytes==other.bytes;}
};
// Parse the captured View ABI; dimensions come from the actual View, not a resolution preset.
inline bool decodeViewIdentity(const void*data,size_t size,ViewIdentity&out){
    if(!data||size!=4096)return false;
    ViewIdentity value;std::memcpy(value.bytes.data(),data,4096);
    const auto f=[&](unsigned row,unsigned column){float x;std::memcpy(&x,value.bytes.data()+row*16+column*4,4);return x;};
    value.rect={f(126,0),f(126,1),f(127,0),f(127,1)};
    const float aw=f(128,0),ah=f(128,1);
    const auto extent=[](float x){return std::isfinite(x)&&x>0&&x<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION&&std::floor(x)==x;};
    if(!extent(aw)||!extent(ah)||!extent(value.rect[2])||!extent(value.rect[3]))return false;
    for(unsigned i=0;i<2;i++)if(!std::isfinite(value.rect[i])||value.rect[i]<0||std::floor(value.rect[i])!=value.rect[i])return false;
    if(value.rect[0]+value.rect[2]>aw||value.rect[1]+value.rect[3]>ah)return false;
    value.allocation={uint32_t(aw),uint32_t(ah)};
    for(unsigned c=0;c<4;c++)if(!std::isfinite(f(114,c)))return false;
    value.jitter={f(114,0)*value.rect[2]*.5f,-f(114,1)*value.rect[3]*.5f};
    if(std::abs(value.jitter[0])>.501f||std::abs(value.jitter[1])>.501f)return false;
    for(unsigned row=110;row<114;row++)for(unsigned c=0;c<4;c++)if(!std::isfinite(f(row,c)))return false;
    for(unsigned c=0;c<2;c++){value.previous[c]=f(133,c+2);value.current[c]=f(136,c+2);if(!std::isfinite(value.previous[c])||!std::isfinite(value.current[c])||value.current[c]<value.previous[c])return false;}
    const float cameraCut=f(137,3);if(!std::isfinite(cameraCut)||cameraCut<0||cameraCut>1)return false;value.cameraCut=cameraCut!=0;
    std::memcpy(&value.counter,value.bytes.data()+137*16+4,4);out=std::move(value);return true;
}
}
