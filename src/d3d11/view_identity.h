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
    std::array<float,2> previous{},current{};
    std::array<float,4> rect{};
    std::array<unsigned char,4096> bytes{};
    bool operator==(const ViewIdentity&other) const {return bytes==other.bytes;}
};
// Parse only the fixed captured View contract. No GPU readback or invented frame counter.
inline bool decodeViewIdentity(const void*data,size_t size,ViewIdentity&out,const char**reason=nullptr){
    const auto reject=[&](const char*why){if(reason)*reason=why;return false;};if(reason)*reason="valid";
    if(!data||size!=4096)return reject("size");
    ViewIdentity value;std::memcpy(value.bytes.data(),data,4096);
    const auto f=[&](unsigned row,unsigned column){float x;std::memcpy(&x,value.bytes.data()+row*16+column*4,4);return x;};
    value.rect={f(126,0),f(126,1),f(127,0),f(127,1)};
    const bool central=value.rect==std::array<float,4>{1280,0,2560,1440};const bool full=value.rect==std::array<float,4>{0,0,5120,1440};
    if(f(128,0)!=5120||f(128,1)!=1440||(!central&&!full))return reject("rect");
    for(unsigned c=0;c<4;c++)if(f(114,c)!=0)return reject("jitter");
    for(unsigned row=110;row<114;row++)for(unsigned c=0;c<4;c++)if(!std::isfinite(f(row,c)))return reject("matrix");
    for(unsigned c=0;c<2;c++){value.previous[c]=f(133,c+2);value.current[c]=f(136,c+2);if(!std::isfinite(value.previous[c])||!std::isfinite(value.current[c])||value.previous[c]<0||value.current[c]<value.previous[c])return reject("clock");}
    std::memcpy(&value.counter,value.bytes.data()+137*16+4,4);out=std::move(value);return true;
}
class ViewUploads {
public:
    void clear(){values_.clear();}
    size_t size() const {return values_.size();}
    uint64_t evictions() const {return evictions_;}
    void invalidate(ID3D11Buffer*buffer){if(auto*p=find(buffer))p->valid=false;}
    void observe(ID3D11Buffer*buffer,const void*bytes,size_t size,const char**reason=nullptr){
        if(!buffer)return;auto*p=find(buffer);ViewIdentity parsed;const bool valid=decodeViewIdentity(bytes,size,parsed,reason);
        if(!p){
            if(!valid)return;
            if(values_.size()==16){p=&*std::min_element(values_.begin(),values_.end(),[](const Upload&a,const Upload&b){return a.valid!=b.valid?!a.valid:a.serial<b.serial;});*p={buffer,{},false,0};evictions_++;}
            else{values_.push_back({buffer,{},false,0});p=&values_.back();}
        }
        // Keep a bounded recent-upload window; an evicted View is unknown until uploaded again.
        p->valid=valid;p->serial=++serial_;if(valid)p->identity=std::move(parsed);
    }
    const ViewIdentity* get(ID3D11Buffer*buffer) const {for(const auto&v:values_)if(v.buffer.Get()==buffer)return v.valid?&v.identity:nullptr;return nullptr;}
private:
    struct Upload {Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;ViewIdentity identity;bool valid=false;uint64_t serial=0;};
    Upload*find(ID3D11Buffer*b){for(auto&v:values_)if(v.buffer.Get()==b)return &v;return nullptr;}
    std::vector<Upload>values_;uint64_t serial_=0,evictions_=0;
};
class ViewFrames {
public:
    bool proposed(const ViewIdentity&view,uint64_t&token) const {
        if(!seen_){token=view.counter;return true;}
        const uint32_t delta=view.counter-last_.counter;if(!delta||delta>=0x80000000u)return false;
        for(unsigned c=0;c<2;c++)if(view.current[c]<last_.current[c]||(delta==1&&view.previous[c]!=last_.current[c]))return false;
        if(token_>UINT64_MAX-delta)return false;token=token_+delta;return true;
    }
    void commit(const ViewIdentity&view,uint64_t token){last_=view;token_=token;seen_=true;}
private:
    ViewIdentity last_{};uint64_t token_=0;bool seen_=false;
};
}
