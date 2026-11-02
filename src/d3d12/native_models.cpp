#include "native_models.h"
#include "evaluation_parameters.h"
#include "ac7_rcas.h"
#include "../settings/sr_settings.h"
#include <nvsdk_ngx_defs_dlssnr.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cmath>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Native models D3D HRESULT "+std::to_string(hr));}
void ngx(NVSDK_NGX_Result r,const char* stage){if(NVSDK_NGX_FAILED(r))throw std::runtime_error(std::string(stage)+" NGX result "+std::to_string(unsigned(r)));}
void transition(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};l->ResourceBarrier(1,&b);}
}
NativeModels::NativeModels(ModelContext&context,unsigned width,unsigned height,unsigned outputWidth,unsigned outputHeight,ColorInput input,bool srEnabled,unsigned nrWidth):context_(context),device_(context.device()),width_(width),height_(height),outputWidth_(outputWidth?outputWidth:width),outputHeight_(outputHeight?outputHeight:height),nrWidth_(nrWidth?nrWidth:width),colorInput_(input),srEnabled_(srEnabled){
    auto*d=device_.Get();
    if(!d)throw std::invalid_argument("Native models require an initialized device context");
    if(!nrWidth_||nrWidth_>width_)throw std::invalid_argument("Invalid NR working width");
    if(!srEnabled_&&(width_!=outputWidth_||height_!=outputHeight_))throw std::invalid_argument("SR bypass requires a native-size scene");
    const auto mode=srModeForExtent(width,height,outputWidth_,outputHeight_);
    const auto quality=outputWidth_==width&&outputHeight_==height?NVSDK_NGX_PerfQuality_Value_DLAA:mode==SrMode::quality?NVSDK_NGX_PerfQuality_Value_MaxQuality:mode==SrMode::balanced?NVSDK_NGX_PerfQuality_Value_Balanced:NVSDK_NGX_PerfQuality_Value_MaxPerf;
    if(!srEnabled_)return;
    context_.initializeNgx();
    try {
        check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&initAllocator_)));check(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,initAllocator_.Get(),nullptr,IID_PPV_ARGS(&initList_)));check(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&initFence_)));
        if(srEnabled_){
            auto&m=models_[1];ngx(NVSDK_NGX_D3D12_GetCapabilityParameters(&m.parameters),"Capabilities");int available=0;ngx(m.parameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available),"Availability");if(!available)throw std::runtime_error("SR unavailable");auto*p=m.parameters;
            p->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);p->Set(NVSDK_NGX_Parameter_Width,width);p->Set(NVSDK_NGX_Parameter_Height,height);p->Set(NVSDK_NGX_Parameter_OutWidth,outputWidth_);p->Set(NVSDK_NGX_Parameter_OutHeight,outputHeight_);p->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(quality));p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int((colorInput_==ColorInput::ldr?0:NVSDK_NGX_DLSS_Feature_Flags_IsHDR)|(colorInput_==ColorInput::sceneHdr?NVSDK_NGX_DLSS_Feature_Flags_AutoExposure:0)|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted));p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,0);
            ngx(NVSDK_NGX_D3D12_CreateFeature(initList_.Get(),NVSDK_NGX_Feature_SuperSampling,p,&m.feature),"CreateSR");
        }
        check(initList_->Close());
    } catch(...){for(auto&m:models_){if(m.feature)NVSDK_NGX_D3D12_ReleaseFeature(m.feature);if(m.parameters)NVSDK_NGX_D3D12_DestroyParameters(m.parameters);}throw;}
}
void NativeModels::prepareSharpening(){
    if(sharpenPipeline_)return;
    auto*d=device_.Get();
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,4};parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[1].DescriptorTable={2,ranges};D3D12_ROOT_SIGNATURE_DESC signature{2,parameters};ComPtr<ID3DBlob>code,error;check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error));check(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&sharpenRoot_)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=sharpenRoot_.Get();pso.CS={ac7_rcas,sizeof(ac7_rcas)};check(d->CreateComputePipelineState(&pso,IID_PPV_ARGS(&sharpenPipeline_)));
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=outputWidth_;texture.Height=outputHeight_;texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;check(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&sharpenInput_)));D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=2;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(d->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&sharpenHeap_)));
}
NativeModels::~NativeModels(){for(auto&m:models_){if(m.feature)NVSDK_NGX_D3D12_ReleaseFeature(m.feature);if(m.parameters)NVSDK_NGX_D3D12_DestroyParameters(m.parameters);}}
void NativeModels::initialize(){
    if(!srEnabled_)return;
    auto*q=context_.queue();
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("NGX initialization event unavailable");const auto armed=initFence_->SetEventOnCompletion(1,event);if(FAILED(armed)){CloseHandle(event);check(armed);}ID3D12CommandList* lists[]={initList_.Get()};q->ExecuteCommandLists(1,lists);const auto signaled=q->Signal(initFence_.Get(),1);const auto result=SUCCEEDED(signaled)?WaitForSingleObject(event,INFINITE):WAIT_FAILED;CloseHandle(event);check(signaled);if(result!=WAIT_OBJECT_0)throw std::runtime_error("NGX initialization fence wait failed");check(device_->GetDeviceRemovedReason());
}
void NativeModels::recordScene(ID3D12GraphicsCommandList*l,ID3D12Resource*color,ID3D12Resource*depth,ID3D12Resource*motion,ID3D12Resource*output,uint64_t frame,float realTime,float previousRealTime,uint64_t sequence,bool cameraCut,const std::array<float,2>& jitter,float sharpness,const NrSettings&settings,unsigned slot){
    if(!std::isfinite(sharpness)||sharpness<0||sharpness>1)throw std::invalid_argument("Invalid RCAS amount");
    const bool first=!seen_||sequence!=lastSequence_;
    const bool nrEnabled=colorInput_==ColorInput::sceneHdr&&settings.enabled;
    const bool nrChanged=nrEnabled!=lastNrEnabled_||(nrEnabled&&!(settings==lastSettings_));
    const bool reset=first||cameraCut||frame!=lastFrame_+1||nrChanged;
    const float dt=(realTime-(first?previousRealTime:lastTime_))*1000.f;
    auto*srColor=color;
    if(nrEnabled){
        const auto spatialLayout=nrSpatialLayout(width_,height_,settings);const bool compressed=spatialLayout.workWidth<width_;
        if(spatialLayout.workWidth!=nrWidth_)throw std::runtime_error("NR working layout changed after admission");
        auto&o=outputs_.at(slot);
        if(!o.codec){
            o.codec=std::make_unique<HdrProxy12>(device_.Get(),width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT);
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
            D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=nrWidth_;texture.Height=height_;texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            check(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&o.nr)));
        }
        auto&m=models_[0];
        if(!m.feature){
            context_.initializeNgx();
            ngx(NVSDK_NGX_D3D12_GetCapabilityParameters(&m.parameters),"NR capabilities");int available=0;
            ngx(m.parameters->Get(NVSDK_NGX_Parameter_DLSSNR_Available,&available),"NR availability");if(!available)throw std::runtime_error("NR unavailable");
            m.parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Width,nrWidth_);m.parameters->Set(NVSDK_NGX_Parameter_DLSSNR_Height,height_);
            ngx(NVSDK_NGX_D3D12_CreateFeature(l,NVSDK_NGX_Feature_DLSSNR,m.parameters,&m.feature),"CreateSceneNR");
        }
        const ViewRect rect{0,0,width_,height_};o.codec->prepare(l,color,rect,1.f);
        transition(l,o.codec->prepared(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(l,o.nr.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(compressed){if(!o.spatial)o.spatial=std::make_unique<NrSpatial>(device_.Get(),width_,height_,nrWidth_);o.spatial->pack(l,o.codec->prepared(),depth,motion,spatialLayout,jitter);}
        configureModelEvaluation(m.parameters,{compressed?o.spatial->color():o.codec->prepared(),compressed?o.spatial->depth():depth,compressed?o.spatial->motion():motion,o.nr.Get(),nullptr},false,reset,dt,nrWidth_,height_,settings);
        m.parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,jitter[0]);m.parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitter[1]);
        ngx(NVSDK_NGX_D3D12_EvaluateFeature(l,m.feature,m.parameters,nullptr),"EvaluateSceneNR");
        transition(l,o.nr.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(l,o.codec->prepared(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        auto*neural=o.nr.Get();
        if(compressed){o.spatial->unpackEdits(l,o.nr.Get(),spatialLayout,jitter);neural=o.spatial->edits();transition(l,neural,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
        o.codec->restore(l,color,rect,1.f,neural,compressed);
        if(compressed)transition(l,neural,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        transition(l,o.nr.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        srColor=o.codec->srColor();transition(l,srColor,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if(sharpness>0)prepareSharpening();
    if(srEnabled_){
        if(sharpness>0)transition(l,sharpenInput_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        configureModelEvaluation(models_[1].parameters,{srColor,depth,motion,sharpness>0?sharpenInput_.Get():output,nullptr},true,reset,dt,width_,height_);
        models_[1].parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_X,jitter[0]);models_[1].parameters->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitter[1]);
        models_[1].parameters->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,dt);
        ngx(NVSDK_NGX_D3D12_EvaluateFeature(l,models_[1].feature,models_[1].parameters,nullptr),"EvaluateSceneSR");
        if(sharpness>0)transition(l,sharpenInput_.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }else{
        auto*destination=sharpness>0?sharpenInput_.Get():output;
        const auto previous=sharpness>0?D3D12_RESOURCE_STATE_COMMON:D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        transition(l,srColor,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(l,destination,previous,D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyResource(destination,srColor);
        transition(l,srColor,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(l,destination,D3D12_RESOURCE_STATE_COPY_DEST,sharpness>0?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:previous);
    }
    if(nrEnabled)transition(l,srColor,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    if(sharpness>0){
        // The bridge output is fixed for this layout; never rewrite GPU-visible descriptors in flight.
        if(!sharpenDescriptorsReady_){auto cpu=sharpenHeap_->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;device_->CreateShaderResourceView(sharpenInput_.Get(),&srv,cpu);cpu.ptr+=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(output,nullptr,&uav,cpu);sharpenDescriptorsReady_=true;}
        ID3D12DescriptorHeap*heaps[]={sharpenHeap_.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(sharpenRoot_.Get());l->SetPipelineState(sharpenPipeline_.Get());const struct {unsigned width,height;float amount;unsigned hdr;} constants{outputWidth_,outputHeight_,sharpness,unsigned(colorInput_==ColorInput::sceneHdr)};l->SetComputeRoot32BitConstants(0,4,&constants,0);l->SetComputeRootDescriptorTable(1,sharpenHeap_->GetGPUDescriptorHandleForHeapStart());l->Dispatch((outputWidth_+7)/8,(outputHeight_+7)/8,1);transition(l,sharpenInput_.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    }
}
}
