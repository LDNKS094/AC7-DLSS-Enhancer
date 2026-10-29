// Captured NR/SR diagnostics with fixed 2/8 history and optional per-feature shared-resource transport.
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
#include <memory>
#include <chrono>
#include <thread>
#include <dxgi1_2.h>
#include <d3d11sdklayers.h>
#include "shared_texture_bridge.h"
#include "evaluation_parameters.h"
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
ComPtr<ID3D11Texture2D> texture11(ID3D11Device* device,const Image& image) {
    D3D11_TEXTURE2D_DESC d{};d.Width=image.header[0];d.Height=image.header[1];d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=static_cast<DXGI_FORMAT>(image.header[2]);d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{image.bytes.data(),d.Width*image.header[3],0};ComPtr<ID3D11Texture2D> t;
    check(device->CreateTexture2D(&d,&data,&t),"Create D3D11 fixture texture");return t;
}
std::vector<unsigned char> read11(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);d.BindFlags=d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;check(device->CreateTexture2D(&d,nullptr,&staging),"Create D3D11 result readback");context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE m{};check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Read returned D3D11 output");
    std::vector<unsigned char> result(size_t(width)*height*8);
    for(unsigned y=0;y<height;++y)std::memcpy(result.data()+size_t(y)*width*8,static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(width)*8);
    context->Unmap(staging.Get(),0);return result;
}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,0,before,after};list->ResourceBarrier(1,&b);
}
}
int evaluateCapturedFrame(ID3D12Device* device,ID3D12CommandQueue* queue,NVSDK_NGX_Handle* feature,NVSDK_NGX_Parameter* parameters,const std::filesystem::path& input,const std::filesystem::path& output,bool zeroStrength,bool srDiagnostic,int sequenceMode) {
    const bool shared=sequenceMode>=5;
    const unsigned frameCount=sequenceMode>=3?8u:sequenceMode?2u:1u;
    ComPtr<ID3D11Device> device11;ComPtr<ID3D11DeviceContext> context11;
    std::unique_ptr<ac7::SharedTextureBridge> bridge;
    if(shared) {
        ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create interop adapter factory");
        ComPtr<IDXGIAdapter1> adapter;const auto wanted=device->GetAdapterLuid();
        for(unsigned i=0;;++i){ComPtr<IDXGIAdapter1> candidate;const auto hr=factory->EnumAdapters1(i,&candidate);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"Enumerate interop adapter");DXGI_ADAPTER_DESC1 d{};check(candidate->GetDesc1(&d),"Read interop LUID");if(d.AdapterLuid.LowPart==wanted.LowPart&&d.AdapterLuid.HighPart==wanted.HighPart){adapter=candidate;break;}}
        if(!adapter)throw std::runtime_error("No matching D3D11 adapter for NGX device");
        check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device11,nullptr,&context11),"Create interop D3D11 device");
        bridge=std::make_unique<ac7::SharedTextureBridge>(device11.Get(),device,queue,width,height);
        std::cout<<"SHARED_BRIDGE: D3D11 input copies, public shared handles/fence; D3D12 input uploads=0; diagnostic NaN output seed only"<<std::endl;
    }
    std::vector<ComPtr<ID3D12Resource>> textures,uploads,readbacks;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
    for(unsigned frame=0;frame<frameCount;++frame) {
    const auto frameInput=sequenceMode?input/(L"frame"+std::to_wstring(frame)):input;
    const auto frameOutput=sequenceMode?output/(L"frame"+std::to_wstring(frame)):output;
    if(sequenceMode && !std::filesystem::create_directory(frameOutput)) throw std::runtime_error("Refusing existing sequence output");
    const auto outputFile=frameOutput/(srDiagnostic?L"sr-output.raw":L"nr-output.raw");
    if(std::filesystem::exists(outputFile)) throw std::runtime_error("Refusing to overwrite prior NR output");
    std::vector<Image> images={read(frameInput/L"color.raw",DXGI_FORMAT_R16G16B16A16_FLOAT,8),read(frameInput/L"depth.raw",DXGI_FORMAT_R32_FLOAT,4),read(frameInput/L"motion.raw",DXGI_FORMAT_R16G16_FLOAT,4),Image{{width,height,10,8},std::vector<unsigned char>(size_t(width)*height*8)}};
    if(srDiagnostic) {
        images.push_back(read(frameInput/L"exposure.raw",DXGI_FORMAT_R32_FLOAT,4,1,1));
        float gain=0;std::memcpy(&gain,images[4].bytes.data(),4);
        if(!std::isfinite(gain)||gain<=0)throw std::runtime_error("Explicit diagnostic exposure is invalid");
    }
    // A NaN sentinel distinguishes an unwritten output from a valid black image.
    for(size_t i=0;i<images[3].bytes.size();i+=2) {images[3].bytes[i]=0;images[3].bytes[i+1]=0x7e;}
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create allocator");
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Create list");
    std::array<ComPtr<ID3D11Texture2D>,4> inputs11;ComPtr<ID3D11Texture2D> returned11;
    if(shared) {
        for(unsigned i=0;i<3;++i)inputs11[i]=texture11(device11.Get(),images[i]);
        const float unity=1.f;Image exposure{{1,1,41,4},std::vector<unsigned char>(4)};std::memcpy(exposure.bytes.data(),&unity,4);
        inputs11[3]=texture11(device11.Get(),srDiagnostic?images[4]:exposure);
        returned11=texture11(device11.Get(),images[3]);
    }
    if(!shared)check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Create fence");
    if(frame==0) { textures.resize(images.size());uploads.resize(images.size());readbacks.resize(images.size());footprints.resize(images.size()); }
    for(unsigned i=0;i<images.size();++i) {
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=images[i].header[0];d.Height=images[i].header[1];d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=static_cast<DXGI_FORMAT>(images[i].header[2]);
        if(i==3) d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if(frame==0) {
            if(shared) {const unsigned mapping[]={0,1,2,4,3};textures[i]=bridge->resource(mapping[i]);d=textures[i]->GetDesc();}
            else {D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&textures[i])),"Create texture");}
            UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);
            if(!shared || i==3)uploads[i]=buffer(device,bytes,D3D12_HEAP_TYPE_UPLOAD);
            readbacks[i]=buffer(device,bytes,D3D12_HEAP_TYPE_READBACK);
        }
        if(shared && i!=3) {transition(list.Get(),textures[i].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);continue;}
        if(shared || frame>0)transition(list.Get(),textures[i].Get(),shared?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
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
    const int reset=frame==0 || sequenceMode==2 || sequenceMode==4 || sequenceMode==6 ? 1 : 0;
    float frameTime=0;
    if(srDiagnostic && sequenceMode>=3) {
        std::ifstream time(frameInput/L"frame-time.bin",std::ios::binary|std::ios::ate);
        if(!time || time.tellg()!=4)throw std::runtime_error("Invalid captured frame time file");
        time.seekg(0);time.read(reinterpret_cast<char*>(&frameTime),4);
        if(!time || !std::isfinite(frameTime) || frameTime<=0)throw std::runtime_error("Invalid captured frame time value");
    }
    std::array<ID3D12Resource*,5> evaluation{};for(unsigned i=0;i<textures.size();++i)evaluation[i]=textures[i].Get();
    std::cout<<(srDiagnostic?"SR_FRAME":"NR_FRAME")<<" index="<<frame<<" Reset="<<reset<<" sharedFeature="<<feature<<" colorResource="<<textures[0].Get()<<" outputResource="<<textures[3].Get()<<std::endl;
    configureCapturedEvaluation(parameters,evaluation,srDiagnostic,reset,zeroStrength,frameTime);
    if(srDiagnostic)std::cout<<"SR_DIAGNOSTIC: PreExposure=1/ExposureScale=1 explicit assumptions, NOT AC7 calibration; actual captured jitter=0; sequenceMode="<<sequenceMode<<std::endl;
    const auto result=NVSDK_NGX_D3D12_EvaluateFeature(list.Get(),feature,parameters,nullptr);
    std::cout<<"EvaluateFeature result=0x"<<std::hex<<static_cast<unsigned>(result)<<std::dec<<std::endl;
    if(NVSDK_NGX_FAILED(result)) {std::cout<<"STOP evaluate failed; no submission or output"<<std::endl;return 9;}
    for(unsigned i=0;i<images.size();++i) {
        transition(list.Get(),textures[i].Get(),i==3?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readbacks[i].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[i];
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=textures[i].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        if(shared)transition(list.Get(),textures[i].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    }
    check(list->Close(),"Close evaluate list");
    std::vector<unsigned char> returned;
    if(shared) {
        try {
            bridge->upload(frame,{inputs11[0].Get(),inputs11[1].Get(),inputs11[2].Get(),inputs11[3].Get()});
            bridge->submit(list.Get());const auto began=std::chrono::steady_clock::now();
            ac7::SharedTextureBridge::Result status;
            while((status=bridge->poll(frame,returned11.Get()))==ac7::SharedTextureBridge::Result::pending) {
                if(std::chrono::steady_clock::now()-began>std::chrono::seconds(10))throw std::runtime_error("Shared model retirement timeout");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(status!=ac7::SharedTextureBridge::Result::published)throw std::runtime_error("Shared model result was not published for its source frame");
        } catch(const std::exception& e) {std::cerr<<"STOP shared model GPU retirement failed: "<<e.what()<<"; terminating standalone probe without releasing pending resources"<<std::endl;ExitProcess(10);}
        returned=read11(device11.Get(),context11.Get(),returned11.Get());
        std::cout<<"Evaluate shared GPU fence completed; frame="<<frame<<std::endl;
    } else {
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event) throw std::runtime_error("Create fence event failed");
    ID3D12CommandList* submit[]={list.Get()};queue->ExecuteCommandLists(1,submit);
    const HRESULT signal=queue->Signal(fence.Get(),1),completion=fence->SetEventOnCompletion(1,event);
    const DWORD waited=SUCCEEDED(signal)&&SUCCEEDED(completion)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);
    if(waited!=WAIT_OBJECT_0 || fence->GetCompletedValue()==UINT64_MAX) {
        std::cerr<<"STOP evaluate GPU completion failed; terminating without releasing pending resources"<<std::endl;ExitProcess(10);
    }
    std::cout<<"Evaluate GPU fence completed=1"<<std::endl;
    }
    check(device->GetDeviceRemovedReason(),"Device health");
    for(unsigned i=0;i<images.size();++i) {
        void* data=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(readbacks[i]->GetDesc().Width)};
        check(readbacks[i]->Map(0,&range,&data),"Map readback");auto bytes=images[i].bytes;
        for(unsigned y=0;y<images[i].header[1];++y)std::memcpy(bytes.data()+size_t(y)*images[i].header[0]*images[i].header[3],static_cast<unsigned char*>(data)+footprints[i].Offset+size_t(y)*footprints[i].Footprint.RowPitch,size_t(images[i].header[0])*images[i].header[3]);
        D3D12_RANGE empty{};readbacks[i]->Unmap(0,&empty);
        if(i!=3) {if(bytes!=images[i].bytes)throw std::runtime_error("Input upload/readback mismatch or model altered input");std::cout<<"InputRoundtrip "<<keys[i]<<" bit-exact"<<std::endl;}
        else {
            if(shared) {if(returned!=bytes)throw std::runtime_error("D3D12 model output differs from D3D11 returned output");bytes=returned;std::cout<<"SharedOutputRoundtrip D3D12-to-D3D11 bit-exact"<<std::endl;}
            std::ofstream file(outputFile,std::ios::binary);file.write(reinterpret_cast<const char*>(images[i].header.data()),16);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();if(!file)throw std::runtime_error("Output write failed");
            std::cout<<(srDiagnostic?"OutputWritten sr-output.raw":"OutputWritten nr-output.raw")<<std::endl;
        }
    }
    if(shared) {
        ComPtr<ID3D11InfoQueue> q;check(device11.As(&q),"Read D3D11 debug queue");unsigned errors=0;
        for(UINT64 i=0;i<q->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T n=0;check(q->GetMessage(i,nullptr,&n),"D3D11 debug message size");std::vector<unsigned char> b(n);auto* m=reinterpret_cast<D3D11_MESSAGE*>(b.data());check(q->GetMessage(i,m,&n),"D3D11 debug message");if(m->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<std::endl;}}
        std::cout<<"D3D11DebugErrors="<<errors<<std::endl;if(errors)return 11;
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
    }
    return 0;
}
