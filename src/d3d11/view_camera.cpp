#include "view_camera.h"
#include <sl_matrix_helpers.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <stdexcept>

namespace ac7 {
namespace {
struct Layout {unsigned projection,previous,jitter,position,forward,up,right;};
template<class T>T load(const void*data,size_t offset){T result;std::memcpy(&result,static_cast<const unsigned char*>(data)+offset,sizeof(result));return result;}
bool readable(const void*data,size_t size){
    MEMORY_BASIC_INFORMATION info{};
    if(!data||VirtualQuery(data,&info,sizeof(info))!=sizeof(info)||info.State!=MEM_COMMIT||(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const auto offset=reinterpret_cast<uintptr_t>(data)-reinterpret_cast<uintptr_t>(info.BaseAddress);
    return offset<info.RegionSize&&size<=info.RegionSize-offset;
}
Layout readLayout(const unsigned char*image){
    // This is the same registered View layout already owned by the native uniform buffer.
    const auto*metadata=image+0x3caa680;
    if(!readable(metadata,0x80))throw std::runtime_error("Missing registered native View metadata");
    const auto*members=load<const unsigned char*>(metadata,0x70);
    const auto count=load<unsigned>(metadata,0x78),capacity=load<unsigned>(metadata,0x7c),bytes=load<unsigned>(metadata,0x48);
    if(!count||count>capacity||capacity>4096||!bytes||bytes>4096||!readable(members,size_t(count)*48))throw std::runtime_error("Unsupported registered native View member table");
    const auto field=[&](const wchar_t*name,unsigned rows,unsigned columns){
        const auto chars=std::wcslen(name)+1;
        for(unsigned i=0;i<count;i++){
            const auto*m=members+48*i;const auto*actual=load<const wchar_t*>(m,0);
            if(!readable(actual,chars*sizeof(wchar_t))||std::wmemcmp(actual,name,chars))continue;
            const auto offset=load<unsigned>(m,0x10);
            if(load<unsigned>(m,0x14)!=4||load<unsigned>(m,0x1c)!=rows||load<unsigned>(m,0x20)!=columns||load<unsigned>(m,0x24)||offset%4||uint64_t(offset)+rows*columns*4>bytes)
                throw std::runtime_error("Native camera member type or extent changed");
            return offset;
        }
        throw std::runtime_error("Required native camera member is missing");
    };
    Layout result{field(L"ViewToClip",4,4),field(L"ClipToPrevClip",4,4),field(L"TemporalAAJitter",1,4),
        field(L"WorldCameraOrigin",1,3),field(L"ViewForward",1,3),field(L"ViewUp",1,3),field(L"ViewRight",1,3)};
    if(result.previous!=110*16||result.jitter!=114*16)throw std::runtime_error("Native camera metadata conflicts with the established View identity ABI");
    return result;
}
template<size_t N>bool finite(const std::array<float,N>&values){return std::all_of(values.begin(),values.end(),[](float f){return std::isfinite(f);});}
bool inverse(const std::array<float,16>&matrix,std::array<float,16>&result){
    static_assert(sizeof(sl::float4x4)==sizeof(matrix));
    sl::float4x4 input,output;std::memcpy(&input,matrix.data(),sizeof(input));
    sl::matrixFullInvert(output,input);std::memcpy(result.data(),&output,sizeof(output));return finite(result);
}
}
std::optional<FrameCamera>readViewCamera(const ViewIdentity&view,const unsigned char*image,bool panini,const char*&refusal){
    static const Layout layout=readLayout(image);
    refusal=nullptr;
    if(panini){refusal="FG camera requires the native Panini distortion mapping";return {};}
    FrameCamera result;
    result.viewToClip=load<std::array<float,16>>(view.bytes.data(),layout.projection);
    result.clipToPrevious=load<std::array<float,16>>(view.bytes.data(),layout.previous);
    const auto jitter=load<std::array<float,4>>(view.bytes.data(),layout.jitter);
    result.position=load<std::array<float,3>>(view.bytes.data(),layout.position);
    result.forward=load<std::array<float,3>>(view.bytes.data(),layout.forward);
    result.up=load<std::array<float,3>>(view.bytes.data(),layout.up);
    result.right=load<std::array<float,3>>(view.bytes.data(),layout.right);
    result.jitter=view.jitter;result.cameraCut=view.cameraCut;
    if(!finite(result.viewToClip)||!finite(result.clipToPrevious)||!finite(jitter)||!finite(result.position)||!finite(result.forward)||!finite(result.up)||!finite(result.right))
        throw std::runtime_error("Nonfinite actual native camera parameters");
    const auto nonzero=[](const auto&v){return std::any_of(v.begin(),v.end(),[](float f){return f!=0;});};
    if(!nonzero(result.forward)||!nonzero(result.up)||!nonzero(result.right)){refusal="Native camera basis is degenerate";return {};}
    auto&p=result.viewToClip;p[8]-=jitter[0];p[9]-=jitter[1];
    if(p[8]!=0||p[9]!=0){refusal="FG camera pinhole mapping is not implemented for this off-center projection";return {};}
    // Canonical perspective only; never reinterpret oblique or orthographic planes.
    for(unsigned i=0;i<p.size();i++)if(i!=0&&i!=5&&i!=10&&i!=11&&i!=14&&p[i]!=0){refusal="FG camera projection is not canonical perspective";return {};}
    if(!p[0]||!p[5]||p[11]!=1||p[10]>0||p[14]<=0){refusal="FG camera projection is not the established reversed-Z perspective";return {};}
    const double nearPlane=double(p[14])/(1.-double(p[10]));
    const double farPlane=p[10]==0?std::numeric_limits<double>::infinity():-double(p[14])/double(p[10]);
    if(!std::isfinite(nearPlane)||nearPlane<=0||nearPlane>std::numeric_limits<float>::max()||farPlane<=nearPlane||(p[10]!=0&&(!std::isfinite(farPlane)||farPlane>std::numeric_limits<float>::max()))){
        refusal="FG camera clipping planes cannot be represented by the backend";return {};
    }
    result.nearPlane=float(nearPlane);result.farPlane=p[10]==0?std::numeric_limits<float>::infinity():float(farPlane);
    if(result.farPlane<=result.nearPlane){refusal="FG camera clipping planes collapse at backend float precision";return {};}
    result.verticalFov=2.f*std::atan(1.f/std::abs(p[5]));result.aspect=std::abs(p[5]/p[0]);
    if(!std::isfinite(result.verticalFov)||!std::isfinite(result.aspect)||result.verticalFov<=0||result.aspect<=0||!inverse(p,result.clipToView)||!inverse(result.clipToPrevious,result.previousToClip)){
        refusal="FG camera transform is singular or invalid for this View";return {};
    }
    return result;
}
}
