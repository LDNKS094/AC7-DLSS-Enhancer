#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <future>
#include <string>
#include <utility>
#include "../settings/nr_settings.h"
#include "../d3d11/guide_normalizer.h"
namespace ac7 {
// One requested pair; GPU readback is fenced, CPU mapping/preview/file work runs off the render callback.
class NrCapture {
public:
    struct Result {enum State {none,saved,failed};State state=none;std::filesystem::path folder;std::string error;uint64_t frame=0,rawDifferentPixels=0,previewDifferentPixels=0;};
    explicit NrCapture(std::filesystem::path directory):directory_(std::move(directory)){}
    bool busy() const{return phase_!=idle;}
    // before enters/leaves NON_PIXEL_SHADER_RESOURCE; final packed NR+SR output enters/leaves COMMON.
    void record(ID3D12GraphicsCommandList*,ID3D12Resource* before,ID3D12Resource* after,ViewRect,uint64_t frame,bool nrEnabled,const NrSettings&,uint64_t viewSequence,uint32_t viewCounter,bool hdr=true,bool cameraCut=false,const std::array<float,2>&jitter={},float sharpness=0,bool srEnabled=true,unsigned nrWorkWidth=0);
    void submitted(ID3D12CommandQueue*);
    Result poll(); // Never waits for GPU or file work.
    const std::filesystem::path&folder() const{return folder_;}
private:
    struct Info {ViewRect rect;uint64_t frame;bool nrEnabled;NrSettings settings;uint64_t viewSequence;uint32_t viewCounter;uint32_t allocationWidth=0,allocationHeight=0,outputWidth=0,outputHeight=0;bool hdr=true,packedHdr=false,cameraCut=false;std::array<float,2>jitter{};float sharpness=0;bool srEnabled=true;unsigned nrWorkWidth=0;};
    static Result save(std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2>,std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2>,Info,std::filesystem::path);
    enum Phase {idle,recorded,gpu,worker,error,untracked};Phase phase_=idle;bool poisoned_=false;
    std::filesystem::path directory_,folder_;
    std::string error_;
    Info info_{};
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2>footprint_{};
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2>readbacks_;
    Microsoft::WRL::ComPtr<ID3D12Fence>fence_;
    std::future<Result>work_;
};
}
