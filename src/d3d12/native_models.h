#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <array>
#include <cstdint>
#include <memory>
#include "../settings/nr_settings.h"
#include "nr_spatial.h"
#include "hdr_proxy.h"
#include "model_context.h"
namespace ac7 {
// One persistent NR/SR history on the owner's DIRECT queue. Retire before destruction.
class NativeModels {
public:
    enum class ColorInput {ldr,sceneHdr};
    NativeModels(ModelContext&,unsigned width,unsigned height,unsigned outputWidth=0,unsigned outputHeight=0,ColorInput input=ColorInput::sceneHdr,bool srEnabled=true,unsigned nrWidth=0);
    ~NativeModels();
    NativeModels(const NativeModels&)=delete;
    NativeModels& operator=(const NativeModels&)=delete;
    void invalidateHistory(){seen_=false;}
    void initialize();
    // HDR/depth/motion enter and leave NON_PIXEL_SHADER_RESOURCE; outputs leave COMMON.
    void recordScene(ID3D12GraphicsCommandList*,ID3D12Resource* color,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* output,uint64_t frame,float realTime,float previousRealTime,uint64_t sequence,bool cameraCut=false,const std::array<float,2>& jitter={},float sharpness=0,const NrSettings& settings={},unsigned slot=0);
    void commit(uint64_t frame,float realTime,bool nrEnabled=true,const NrSettings& settings={},uint64_t viewSequence=0){lastFrame_=frame;lastTime_=realTime;lastNrEnabled_=nrEnabled;lastSettings_=settings;lastSequence_=viewSequence;seen_=true;}
private:
    void prepareSharpening();
    struct Model {NVSDK_NGX_Parameter* parameters=nullptr;NVSDK_NGX_Handle* feature=nullptr;};
    ModelContext&context_;
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>initAllocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>initList_;
    Microsoft::WRL::ComPtr<ID3D12Fence>initFence_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>sharpenPipeline_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>sharpenRoot_;
    Microsoft::WRL::ComPtr<ID3D12Resource>sharpenInput_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>sharpenHeap_;
    struct Outputs {Microsoft::WRL::ComPtr<ID3D12Resource>nr;std::unique_ptr<HdrProxy12>codec;std::unique_ptr<NrSpatial>spatial;};
    std::array<Outputs,3>outputs_{};
    std::array<Model,2>models_{};
    unsigned width_,height_,outputWidth_,outputHeight_,nrWidth_;
    ColorInput colorInput_;
    uint64_t lastFrame_=0,lastSequence_=0;
    NrSettings lastSettings_{};
    float lastTime_=0;
    bool seen_=false,lastNrEnabled_=true,sharpenDescriptorsReady_=false,srEnabled_=true;
};
}
