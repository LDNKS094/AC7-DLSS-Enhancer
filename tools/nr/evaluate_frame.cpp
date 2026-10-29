// One real capture, reset history, no live interop or synthetic temporal frames.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs_dlssnr.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
constexpr unsigned width=2560,height=1440;
void check(HRESULT hr,const char* stage) {
    if(FAILED(hr)) throw std::runtime_error(std::string(stage)+" HRESULT="+std::to_string(static_cast<unsigned>(hr)));
}
struct Image {
    std::array<unsigned,4> header;
    std::vector<unsigned char> bytes;
};
Image read(const std::filesystem::path& path,DXGI_FORMAT format,unsigned bpp,unsigned w=width,unsigned h=height) {
    Image image{{w,h,static_cast<unsigned>(format),bpp},{}};
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    const size_t size=16+size_t(w)*h*bpp;
    if(!f || f.tellg()!=static_cast<std::streamoff>(size)) throw std::runtime_error("Unexpected input file length: "+path.string());
    f.seekg(0);std::array<unsigned,4> header{};
    f.read(reinterpret_cast<char*>(header.data()),16);
    if(header!=image.header) throw std::runtime_error("Unexpected input layout: "+path.string());
    image.bytes.resize(size-16);f.read(reinterpret_cast<char*>(image.bytes.data()),image.bytes.size());
    if(!f) throw std::runtime_error("Input read failed");
    return image;
}
ComPtr<ID3D12Resource> buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE heap) {
    D3D12_HEAP_PROPERTIES hp{};hp.Type=heap;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,heap==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)),"Create buffer");return resource;
}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,0,before,after};list->ResourceBarrier(1,&b);
}
}
int evaluateCapturedFrame(ID3D12Device* device,ID3D12CommandQueue* queue,NVSDK_NGX_Handle* feature,NVSDK_NGX_Parameter* parameters,const std::filesystem::path& input,const std::filesystem::path& output,bool zeroStrength,bool srDiagnostic) {
    const auto outputFile=output/(srDiagnostic?L"sr-output.raw":L"nr-output.raw");
    if(std::filesystem::exists(outputFile)) throw std::runtime_error("Refusing to overwrite prior NR output");
    std::vector<Image> images={read(input/L"color.raw",DXGI_FORMAT_R16G16B16A16_FLOAT,8),read(input/L"depth.raw",DXGI_FORMAT_R32_FLOAT,4),read(input/L"motion.raw",DXGI_FORMAT_R16G16_FLOAT,4),Image{{width,height,10,8},std::vector<unsigned char>(size_t(width)*height*8)}};
    if(srDiagnostic) {
        images.push_back(read(input/L"exposure.raw",DXGI_FORMAT_R32_FLOAT,4,1,1));
        float gain=0;std::memcpy(&gain,images[4].bytes.data(),4);
        if(!std::isfinite(gain)||gain<=0)throw std::runtime_error("Explicit diagnostic exposure is invalid");
    }
    // A NaN sentinel distinguishes an unwritten output from a valid black image.
    for(size_t i=0;i<images[3].bytes.size();i+=2) {images[3].bytes[i]=0;images[3].bytes[i+1]=0x7e;}
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create allocator");
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Create list");
    check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence");
    std::vector<ComPtr<ID3D12Resource>> textures(images.size()),uploads(images.size()),readbacks(images.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(images.size());
    for(unsigned i=0;i<images.size();++i) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=images[i].header[0];d.Height=images[i].header[1];d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=static_cast<DXGI_FORMAT>(images[i].header[2]);
        if(i==3) d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&textures[i])),"Create texture");
        UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);
        uploads[i]=buffer(device,bytes,D3D12_HEAP_TYPE_UPLOAD);readbacks[i]=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK);
        void* data=nullptr;D3D12_RANGE empty{};check(uploads[i]->Map(0,&empty,&data),"Map upload");
        for(unsigned y=0;y<images[i].header[1];++y)std::memcpy(static_cast<unsigned char*>(data)+footprints[i].Offset+size_t(y)*footprints[i].Footprint.RowPitch,images[i].bytes.data()+size_t(y)*images[i].header[0]*images[i].header[3],size_t(images[i].header[0])*images[i].header[3]);
        uploads[i]->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=textures[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=uploads[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprints[i];
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        transition(list.Get(),textures[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,i==3?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    const char* nrKeys[]={NVSDK_NGX_Parameter_DLSSNR_Color,NVSDK_NGX_Parameter_DLSSNR_Depth,NVSDK_NGX_Parameter_DLSSNR_MVec,NVSDK_NGX_Parameter_DLSSNR_Output};
    const char* srKeys[]={NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_Output,NVSDK_NGX_Parameter_ExposureTexture};
    const char** keys=srDiagnostic?srKeys:nrKeys;
    if(srDiagnostic) {
        for(unsigned i=0;i<images.size();++i)parameters->Set(keys[i],textures[i].Get());
        parameters->Set(NVSDK_NGX_Parameter_Reset,1);
        parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
        parameters->Set(NVSDK_NGX_Parameter_MV_Scale_X,1.f);parameters->Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.f);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,1.f);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale,1.f);
        parameters->Set(NVSDK_NGX_Parameter_Sharpness,0.f);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,width);
        parameters->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,height);
        std::cout<<"SR_RESET_DIAGNOSTIC: PreExposure=1 and ExposureScale=1 are explicit standalone-domain assumptions; NOT AC7 calibration; Reset=1; no fabricated temporal history"<<std::endl;
    } else {
    for(unsigned i=0;i<4;++i) {
        parameters->Set(keys[i],textures[i].Get());const std::string prefix(keys[i]);
        parameters->Set((prefix+"SubrectBaseX").c_str(),0u);parameters->Set((prefix+"SubrectBaseY").c_str(),0u);
        parameters->Set((prefix+"SubrectWidth").c_str(),width);parameters->Set((prefix+"SubrectHeight").c_str(),height);
    }
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Enabled,1);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Reset,1);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_DepthInverted,1);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecScaleX,1.f);parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MVecScaleY,1.f);
    parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.f);parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,0.f);
    std::cout<<"NRStrengthControl="<<(zeroStrength?"all-zero":"reference-settings")<<std::endl;
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Intensity,zeroStrength?0.f:1.f);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_LocalToneStrength,zeroStrength?0.f:.3f);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_LocalStructureStrength,zeroStrength?0.f:.7f);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_SkinStructureStrength,zeroStrength?0.f:.5f);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Style,0u);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UseAutoMask,0);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_MaskBlendMode,0);
    parameters->Set(NVSDK_NGX_Parameter_DLSSNR_UICorrection,0);
    for(const char* key:{NVSDK_NGX_Parameter_DLSSNR_ControlMask,NVSDK_NGX_Parameter_DLSSNR_Backbuffer,NVSDK_NGX_Parameter_DLSSNR_UI,NVSDK_NGX_Parameter_DLSSNR_UIAlpha,NVSDK_NGX_Parameter_DLSSNR_BidirectionalDistortionField}) parameters->Set(key,static_cast<ID3D12Resource*>(nullptr));
    }
    const auto result=NVSDK_NGX_D3D12_EvaluateFeature(list.Get(),feature,parameters,nullptr);
    std::cout<<"EvaluateFeature result=0x"<<std::hex<<static_cast<unsigned>(result)<<std::dec<<std::endl;
    if(NVSDK_NGX_FAILED(result)) {std::cout<<"STOP evaluate failed; no submission or output"<<std::endl;return 9;}
    for(unsigned i=0;i<images.size();++i) {
        transition(list.Get(),textures[i].Get(),i==3?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readbacks[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[i];
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=textures[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
    check(list->Close(),"Close evaluate list");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event) throw std::runtime_error("Create fence event failed");
    ID3D12CommandList* submit[]={list.Get()};queue->ExecuteCommandLists(1,submit);
    const HRESULT signal=queue->Signal(fence.Get(),1),completion=fence->SetEventOnCompletion(1,event);
    const DWORD waited=SUCCEEDED(signal)&&SUCCEEDED(completion)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);
    if(waited!=WAIT_OBJECT_0 || fence->GetCompletedValue()==UINT64_MAX) {
        std::cerr<<"STOP evaluate GPU completion failed; terminating without releasing pending resources"<<std::endl;ExitProcess(10);
    }
    check(device->GetDeviceRemovedReason(),"Device health");std::cout<<"Evaluate GPU fence completed=1"<<std::endl;
    for(unsigned i=0;i<images.size();++i) {
        void* data=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(readbacks[i]->GetDesc().Width)};
        check(readbacks[i]->Map(0,&range,&data),"Map readback");auto bytes=images[i].bytes;
        for(unsigned y=0;y<images[i].header[1];++y)std::memcpy(bytes.data()+size_t(y)*images[i].header[0]*images[i].header[3],static_cast<unsigned char*>(data)+footprints[i].Offset+size_t(y)*footprints[i].Footprint.RowPitch,size_t(images[i].header[0])*images[i].header[3]);
        D3D12_RANGE empty{};readbacks[i]->Unmap(0,&empty);
        if(i!=3) {if(bytes!=images[i].bytes)throw std::runtime_error("Input upload/readback mismatch or model altered input");std::cout<<"InputRoundtrip "<<keys[i]<<" bit-exact"<<std::endl;}
        else {
            std::ofstream file(outputFile,std::ios::binary);file.write(reinterpret_cast<const char*>(images[i].header.data()),16);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();if(!file)throw std::runtime_error("Output write failed");
            std::cout<<(srDiagnostic?"OutputWritten sr-output.raw":"OutputWritten nr-output.raw")<<std::endl;
        }
    }
    ComPtr<ID3D12InfoQueue> info;
    if(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info)))) {
        unsigned errors=0;
        for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T bytes=0;check(info->GetMessage(i,nullptr,&bytes),"Debug message size");std::vector<unsigned char> storage(bytes);auto message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());check(info->GetMessage(i,message,&bytes),"Debug message");
            if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {++errors;std::cerr<<"D3D12 error "<<message->ID<<": "<<message->pDescription<<std::endl;}
        }
        std::cout<<"D3D12DebugErrors="<<errors<<std::endl;if(errors) return 11;
    }
    return 0;
}
