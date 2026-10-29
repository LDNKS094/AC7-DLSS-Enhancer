#pragma once
#include "../d3d11/frame_submission.h"
#include <limits>
namespace ac7 {
// Exclusive one-frame publication policy. Caller retains bridge/models/list/color resources until drained().
// poll advances once; the caller supplies a private output, then consumes it only after published.
class ChainPublication {
public:
    using Clock=FrameSubmission::Clock;
    using Result=SharedTextureBridge::Result;
    ChainPublication(SharedTextureBridge& bridge,ID3D11Device* device):bridge_(bridge),submission_(bridge){
        if(!device)throw std::invalid_argument("Missing publication device");
        Microsoft::WRL::ComPtr<ID3D11Device5> d5;check(device->QueryInterface(IID_PPV_ARGS(&d5)));Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);check(context.As(&context_));check(d5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)));
    }
    ChainPublication(const ChainPublication&)=delete;
    ChainPublication& operator=(const ChainPublication&)=delete;
    bool resetFor(uint64_t frame) const {return resetNext_||(seen_&&frame-lastSeen_!=1);}
    // recordedReset must be applied to BOTH models before this closed list is submitted.
    bool begin(uint64_t frame,const std::array<ID3D11Texture2D*,4>& inputs,ID3D12GraphicsCommandList* list,Clock::time_point deadline,bool recordedReset){
        if(stopping_)return false;
        if(seen_&&frame<=lastSeen_)throw std::invalid_argument("Non-monotonic chain publication");
        if(!drained()){
            seen_=true;lastSeen_=frame;resetNext_=invalid_=true;
            if(bridge_.canPoll())bridge_.abandon();
            return false;
        }
        const bool reset=resetFor(frame);if(recordedReset!=reset)throw std::invalid_argument("Both model Reset flags must match chain history");
        seen_=true;lastSeen_=frame;resetNext_=true;invalid_=false;deadline_=deadline;frame_=frame;
        if(!submission_.begin(frame,inputs,list,deadline))return false;
        return true;
    }
    Result poll(ID3D11Texture2D* privateDestination){
        if(retiring_){
            const auto completed=fence_->GetCompletedValue();if(completed==UINT64_MAX)throw std::runtime_error("D11 retirement device lost");
            if(completed<value_)return Result::pending;
            retiring_=false;destination_.Reset();const bool usable=result_==Result::published&&!invalid_&&!stopping_&&Clock::now()<deadline_;
            resetNext_=!usable;return usable?Result::published:Result::discarded;
        }
        if(!bridge_.canPoll())throw std::runtime_error("No pollable chain submission");
        result_=submission_.poll(frame_,privateDestination);if(result_==Result::pending)return result_;
        // D12 completion alone cannot release resources used by the queued D11 output copy.
        if(value_==std::numeric_limits<uint64_t>::max())throw std::runtime_error("D11 retirement fence exhausted");
        destination_=privateDestination;retiring_=true;check(context_->Signal(fence_.Get(),++value_));context_->Flush();return Result::pending;
    }
    void stop(){stopping_=true;resetNext_=true;if(bridge_.canPoll())bridge_.abandon();}
    bool drained() const {return !bridge_.busy()&&!retiring_;}
    bool stopping() const {return stopping_;}
    bool hasRetirement() const {return retiring_;}
private:
    static void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Chain publication HRESULT "+std::to_string(hr));}
    SharedTextureBridge& bridge_;
    FrameSubmission submission_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context_;
    Microsoft::WRL::ComPtr<ID3D11Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> destination_;
    Clock::time_point deadline_{};
    uint64_t value_=0,frame_=0,lastSeen_=0;
    bool seen_=false,resetNext_=true,stopping_=false,retiring_=false,invalid_=false;
    Result result_=Result::discarded;
};
}
