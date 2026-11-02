#include "nr_capture.h"
#include "../settings/sr_settings.h"
#include <wincodec.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Capture HRESULT "+std::to_string(hr));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};l->ResourceBarrier(1,&b);}
struct File {HANDLE handle=INVALID_HANDLE_VALUE;~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
void writeNew(const std::filesystem::path&path,const void*data,size_t size){
    File file;file.handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file.handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create capture file "+std::to_string(GetLastError()));DWORD written=0;if(size>MAXDWORD||!WriteFile(file.handle,data,DWORD(size),&written,nullptr)||written!=size)throw std::runtime_error("Capture file write incomplete");
}
void writePng(const std::filesystem::path&path,unsigned char*pixels,unsigned width,unsigned height,unsigned stride){
    struct Apartment {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment;
    if(FAILED(apartment.hr)&&apartment.hr!=RPC_E_CHANGED_MODE)check(apartment.hr);
    ComPtr<IWICImagingFactory>factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IStream>stream;check(CreateStreamOnHGlobal(nullptr,TRUE,&stream));ComPtr<IWICBitmapEncoder>encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>options;check(encoder->CreateNewFrame(&frame,&options));check(frame->Initialize(options.Get()));check(frame->SetSize(width,height));auto format=GUID_WICPixelFormat24bppBGR;check(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat24bppBGR)throw std::runtime_error("PNG encoder changed pixel format");check(frame->WritePixels(height,stride,stride*height,pixels));check(frame->Commit());check(encoder->Commit());
    STATSTG stat{};check(stream->Stat(&stat,STATFLAG_NONAME));if(!stat.cbSize.QuadPart||stat.cbSize.QuadPart>MAXDWORD)throw std::runtime_error("PNG encoded size invalid");HGLOBAL handle=nullptr;check(GetHGlobalFromStream(stream.Get(),&handle));
    struct Locked {HGLOBAL handle;void*data;~Locked(){if(data)GlobalUnlock(handle);}} locked{handle,GlobalLock(handle)};if(!locked.data)throw std::runtime_error("PNG encoded buffer unavailable");writeNew(path,locked.data,size_t(stat.cbSize.QuadPart));
}
unsigned char preview(unsigned word,unsigned mantissaBits){
    const unsigned exponent=word>>mantissaBits,mantissa=word&((1u<<mantissaBits)-1);if(exponent==31)return 0;
    const double x=exponent?std::ldexp(1.0+double(mantissa)/(1u<<mantissaBits),int(exponent)-15):std::ldexp(double(mantissa),-14-int(mantissaBits));
    const double tone=std::clamp(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14),0.0,1.0);return static_cast<unsigned char>(std::lround(std::pow(tone,1.0/2.2)*255));
}
}
void NrCapture::record(ID3D12GraphicsCommandList*l,ID3D12Resource*before,ID3D12Resource*after,ViewRect rect,uint64_t frame,bool nrEnabled,const NrSettings&settings,uint64_t viewSequence,uint32_t viewCounter,bool hdr,bool cameraCut,const std::array<float,2>&jitter,float sharpness,bool srEnabled,unsigned nrWorkWidth){
    if(busy())return;
    info_={rect,frame,nrEnabled,settings,viewSequence,viewCounter};info_.hdr=hdr;info_.cameraCut=cameraCut;info_.jitter=jitter;info_.sharpness=sharpness;info_.srEnabled=srEnabled;info_.nrWorkWidth=nrWorkWidth?nrWorkWidth:rect.width;
    try {
        folder_=directory_/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-frame"+std::to_wstring(frame));
        const auto desc=after->GetDesc(),source=before->GetDesc();const auto format=source.Format;info_.packedHdr=format==DXGI_FORMAT_R11G11B10_FLOAT;
        if((format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&!(hdr&&info_.packedHdr))||desc.Format!=format||!rect.width||!rect.height||rect.x+rect.width>source.Width||rect.y+rect.height>source.Height)throw std::runtime_error("Capture texture/rectangle mismatch");
        info_.allocationWidth=UINT(source.Width);info_.allocationHeight=source.Height;info_.outputWidth=UINT(desc.Width);info_.outputHeight=desc.Height;
        check(l->GetDevice(IID_PPV_ARGS(&device_)));D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        for(unsigned i=0;i<2;i++){auto copyDesc=desc;if(!i){copyDesc.Width=rect.width;copyDesc.Height=rect.height;}uint64_t bytes=0;device_->GetCopyableFootprints(&copyDesc,0,1,0,&footprint_[i],nullptr,nullptr,&bytes);D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;check(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(readbacks_[i].ReleaseAndGetAddressOf())));}
        check(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf())));
        ID3D12Resource*inputs[]={before,after};const D3D12_RESOURCE_STATES states[]={D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON};
        for(unsigned i=0;i<2;i++){transition(l,inputs[i],states[i],D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION from{};from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.pResource=inputs[i];D3D12_TEXTURE_COPY_LOCATION to{};to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.pResource=readbacks_[i].Get();to.PlacedFootprint=footprint_[i];const D3D12_BOX box{rect.x,rect.y,0,rect.x+rect.width,rect.y+rect.height,1};l->CopyTextureRegion(&to,0,0,0,&from,i?nullptr:&box);transition(l,inputs[i],D3D12_RESOURCE_STATE_COPY_SOURCE,states[i]);}
        phase_=recorded;
    }catch(const std::exception&e){error_=e.what();phase_=error;}
}
void NrCapture::submitted(ID3D12CommandQueue*q){if(phase_!=recorded)return;const auto hr=q->Signal(fence_.Get(),1);if(FAILED(hr)){error_="Capture fence signal failed "+std::to_string(hr);poisoned_=true;phase_=error;}else phase_=gpu;}
NrCapture::Result NrCapture::poll(){
    if(phase_==gpu){const auto completed=fence_->GetCompletedValue();if(completed==UINT64_MAX){error_="Capture device removed";phase_=error;}else if(completed>=1){auto data=std::move(readbacks_);const auto footprint=footprint_;const auto info=info_;const auto folder=folder_;try{work_=std::async(std::launch::async,[data=std::move(data),footprint,info,folder]()mutable{return save(std::move(data),footprint,info,folder);});phase_=worker;}catch(const std::exception&e){error_=e.what();phase_=error;}}}
    if(phase_==worker&&work_.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{auto result=work_.get();phase_=idle;fence_.Reset();return result;}catch(const std::exception&e){error_=e.what();phase_=error;}}
    if(phase_==error){Result result;result.state=Result::failed;result.folder=folder_;result.frame=info_.frame;result.error=error_;phase_=poisoned_?untracked:idle;return result;}return {};
}
NrCapture::Result NrCapture::save(std::array<ComPtr<ID3D12Resource>,2>data,std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2> footprint,Info info,std::filesystem::path folder){
    Result result;result.folder=folder;result.frame=info.frame;
    struct Mapped {ID3D12Resource*resource=nullptr;const unsigned char*data=nullptr;~Mapped(){if(data){const D3D12_RANGE written{0,0};resource->Unmap(0,&written);}}};
    try {
        std::array<Mapped,2>mapped;for(unsigned i=0;i<2;i++){mapped[i].resource=data[i].Get();const auto bytes=size_t(data[i]->GetDesc().Width);const D3D12_RANGE read{0,bytes};void*bytesMapped=nullptr;check(data[i]->Map(0,&read,&bytesMapped));mapped[i].data=static_cast<const unsigned char*>(bytesMapped);}
        std::array<std::vector<unsigned char>,2>images;
        std::array<unsigned char,2048>rg;std::array<unsigned char,1024>b;for(unsigned i=0;i<rg.size();i++)rg[i]=preview(i,6);for(unsigned i=0;i<b.size();i++)b[i]=preview(i,5);
        for(unsigned i=0;i<2;i++){
            const auto&fp=footprint[i];const unsigned width=fp.Footprint.Width,height=fp.Footprint.Height,stride=(width*3+3)&~3u;
            auto&image=images[i];image.resize(size_t(stride)*height);
            for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
                const auto*raw=mapped[i].data+fp.Offset+size_t(y)*fp.Footprint.RowPitch+x*(info.packedHdr?4:8);auto*p=image.data()+size_t(y)*stride+x*3;
                if(info.packedHdr){uint32_t packed;std::memcpy(&packed,raw,4);p[0]=b[packed>>22];p[1]=rg[(packed>>11)&2047];p[2]=rg[packed&2047];}
                else for(unsigned c=0;c<3;c++){uint16_t half;std::memcpy(&half,raw+c*2,2);const unsigned exponent=(half>>10)&31,mantissa=half&1023;double value=exponent==31?0:exponent?std::ldexp(1.+double(mantissa)/1024,int(exponent)-15):std::ldexp(double(mantissa),-24);if(half&0x8000)value=0;p[2-c]=info.hdr?(half&0x8000?0:preview(half,10)):static_cast<unsigned char>(std::lround(std::clamp(value,0.,1.)*255));}
            }
        }
        const unsigned width=info.rect.width,height=info.rect.height;const bool comparable=width==info.outputWidth&&height==info.outputHeight;
        if(comparable){const unsigned stride=(width*3+3)&~3u,bpp=info.packedHdr?4:8;for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){const auto*a=mapped[0].data+footprint[0].Offset+size_t(y)*footprint[0].Footprint.RowPitch+x*bpp;const auto*b=mapped[1].data+footprint[1].Offset+size_t(y)*footprint[1].Footprint.RowPitch+x*bpp;result.rawDifferentPixels+=std::memcmp(a,b,bpp)!=0;result.previewDifferentPixels+=std::memcmp(images[0].data()+size_t(y)*stride+x*3,images[1].data()+size_t(y)*stride+x*3,3)!=0;}}
        std::filesystem::create_directories(folder.parent_path());if(!std::filesystem::create_directory(folder))throw std::runtime_error("Capture directory already exists; refusing overwrite");writePng(folder/L"before.png",images[0].data(),width,height,(width*3+3)&~3u);writePng(folder/L"after.png",images[1].data(),info.outputWidth,info.outputHeight,(info.outputWidth*3+3)&~3u);
        std::ostringstream json;json.imbue(std::locale::classic());json<<std::setprecision(9)<<"{\n  \"frame\":"<<info.frame<<",\n  \"frameIdentity\":\"monotonic transport token\",\"viewCounter\":"<<info.viewCounter<<",\"viewSequence\":"<<info.viewSequence<<",\n  \"width\":"<<width<<",\"height\":"<<height<<",\"sourceRect\":["<<info.rect.x<<","<<info.rect.y<<","<<width<<","<<height<<"],\n  \"sourceAllocation\":["<<info.allocationWidth<<","<<info.allocationHeight<<"],\n  \"inputWidth\":"<<width<<",\"inputHeight\":"<<height<<",\"outputWidth\":"<<info.outputWidth<<",\"outputHeight\":"<<info.outputHeight<<",\n  \"srMode\":\""<<(!info.srEnabled?"Native":comparable?"DLAA":srModeName(srModeForExtent(width,height,info.outputWidth,info.outputHeight)))<<"\",\n  \"jitterPixels\":["<<info.jitter[0]<<","<<info.jitter[1]<<"],\n  \"rcasSharpness\":"<<info.sharpness<<",\n  \"aaControl\":\"game-settings\",\n  \"cameraCut\":"<<(info.cameraCut?"true":"false")<<",\n  \"nrEnabled\":"<<(info.nrEnabled?"true":"false")<<",\n  \"stage\":\""<<(!info.srEnabled?"native-size HDR processing without SR; before MotionBlur/tonemap/UI":info.hdr?(info.nrEnabled?"temporal graph HDR input vs NR+SR HDR output after upstream translucency/Distortion; before MotionBlur/tonemap/UI":"temporal graph HDR input vs SR HDR output after upstream translucency/Distortion; before MotionBlur/tonemap/UI"):info.sharpness>0?"post-tonemap LDR input vs upscale SR+RCAS output; UI boundary unqualified":"post-tonemap LDR input vs upscale SR output; UI boundary unqualified")<<"\",\n  \"previewMapping\":\""<<(info.hdr?"same ACES-fit at P=1 then gamma1/2.2, BGR8 PNG":"stored perceptual LDR, BGR8 PNG")<<"\",\n  \"srIncluded\":"<<(info.srEnabled?"true":"false")<<",\"gamePostIncluded\":"<<(info.hdr?"false":"true")<<",\"exposureCalibrated\":false,\n  \"nrWorkWidth\":"<<(info.nrEnabled?info.nrWorkWidth:0)<<",\n  \"nrWorkHeight\":"<<(info.nrEnabled?height:0)<<",\n  \"peripheralCompression\":"<<((info.nrEnabled&&info.nrWorkWidth<width)?"true":"false")<<",\n  \"centerAspect\":"<<info.settings.centerAspect<<",\n  \"edgeScale\":"<<info.settings.edgeScale<<",\n  \"transitionWidth\":"<<info.settings.transitionWidth<<",\n  \"intensity\":"<<info.settings.intensity<<",\"tone\":"<<info.settings.tone<<",\"structure\":"<<info.settings.structure<<",\"style\":"<<info.settings.style<<",\n  \"rawDifferentPixels\":"<<(comparable?std::to_string(result.rawDifferentPixels):"null")<<",\"previewDifferentPixels\":"<<(comparable?std::to_string(result.previewDifferentPixels):"null")<<"\n}\n";const auto text=json.str();writeNew(folder/L"capture.json",text.data(),text.size());result.state=Result::saved;
    }catch(const std::exception&e){result.state=Result::failed;result.error=e.what();}return result;
}
}
