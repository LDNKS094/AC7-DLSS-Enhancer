#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
namespace ac7 {
struct ViewIdentity {
    uint32_t counter=0;
    bool cameraCut=false;
    std::array<float,2> previous{},current{},jitter{};
    std::array<float,4> rect{};
    std::array<uint32_t,2> allocation{};
    std::array<unsigned char,4096> bytes{};
    bool operator==(const ViewIdentity&other) const {return bytes==other.bytes;}
    bool sameFrame(const ViewIdentity&other) const {return counter==other.counter&&current==other.current&&rect==other.rect&&allocation==other.allocation&&std::memcmp(bytes.data()+110*16,other.bytes.data()+110*16,64)==0;}
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
class ViewUploads {
public:
    void clear(){values_.clear();}
    void invalidate(ID3D11Buffer*buffer){if(auto*p=find(buffer))p->valid=false;}
    bool observe(ID3D11Buffer*buffer,const void*bytes,size_t size){
        if(!buffer)return false;auto*p=find(buffer);ViewIdentity parsed;const bool valid=decodeViewIdentity(bytes,size,parsed);
        if(!p){
            if(!valid)return false;
            if(values_.size()==16){p=&*std::min_element(values_.begin(),values_.end(),[](const Upload&a,const Upload&b){return a.valid!=b.valid?!a.valid:a.serial<b.serial;});*p={buffer,{},false,0};}
            else{values_.push_back({buffer,{},false,0});p=&values_.back();}
        }
        // Keep a bounded recent-upload window; an evicted View is unknown until uploaded again.
        p->valid=valid;p->serial=++serial_;if(valid)p->identity=std::move(parsed);return valid;
    }
    const ViewIdentity* get(ID3D11Buffer*buffer) const {for(const auto&v:values_)if(v.buffer.Get()==buffer)return v.valid?&v.identity:nullptr;return nullptr;}
private:
    struct Upload {Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;ViewIdentity identity;bool valid=false;uint64_t serial=0;};
    Upload*find(ID3D11Buffer*b){for(auto&v:values_)if(v.buffer.Get()==b)return &v;return nullptr;}
    std::vector<Upload>values_;uint64_t serial_=0;
};
class ViewFrames {
public:
    // A token orders transport submissions; the raw counter remains in ViewIdentity.
    bool proposed(const ViewIdentity&view,uint64_t&token,uint64_t*sequence=nullptr) const {
        if(!seen_){token=view.counter;if(sequence)*sequence=1;return true;}
        if(view.sameFrame(last_))return false;
        const bool restart=restarted(view);const uint64_t step=restart?1:uint32_t(view.counter-last_.counter);
        if(token_>UINT64_MAX-step||(restart&&sequence_==UINT64_MAX))return false;
        token=token_+step;if(sequence)*sequence=sequence_+unsigned(restart);return true;
    }
    void commit(const ViewIdentity&view,uint64_t token){sequence_=seen_?sequence_+unsigned(restarted(view)):1;last_=view;token_=token;seen_=true;}
    uint64_t sequence() const{return sequence_;}
private:
    bool restarted(const ViewIdentity&view) const {const uint32_t delta=view.counter-last_.counter;return !delta||delta>=0x80000000u||view.current[0]<last_.current[0]||view.current[1]<last_.current[1]||view.rect!=last_.rect||view.allocation!=last_.allocation;}
    ViewIdentity last_{};uint64_t token_=0,sequence_=0;bool seen_=false;
};
}
