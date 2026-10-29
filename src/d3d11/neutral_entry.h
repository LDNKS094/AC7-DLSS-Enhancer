#pragma once
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstddef>
namespace ac7 {
// Host-neutral fixed-layout diagnostic. No hooks, models, background threads or game loader.
// Serialized immediate-context calls only; copies are recorded on a private deferred context.
// ExecuteCommandList(TRUE) restores the immediate pipeline. Stop/poll drained() before destruction.
class NeutralEntry {
public:
    enum class Result { disabled, unmatched, refused, pending, applied, stopped };
    explicit NeutralEntry(ID3D11Device*);
    NeutralEntry(const NeutralEntry&)=delete;
    NeutralEntry& operator=(const NeutralEntry&)=delete;
    // Only a shader created from the exact captured prewarp bytecode can be registered.
    static bool matchesBytecode(const void* bytecode,size_t size);
    bool registerShader(ID3D11PixelShader*,const void* bytecode,size_t size);
    bool isShader(ID3D11PixelShader*shader) const {return shader_.Get()==shader&&shader!=nullptr;}
    void enable(bool value){if(!stopped_)enabled_=value;}
    // Hook owner calls before the original draw; the original draw must always run.
    // Frame identity must come from the host, not draw ordinal. One application per token.
    Result beforeDraw(ID3D11DeviceContext*,uint64_t frame);
    void stop(){enabled_=false;stopped_=true;}
    bool drained();
    // Diagnostic read only; do not mutate while in flight.
    ID3D11Texture2D* capturedRegion() const {return scratch_.Get();}
private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_;
    Microsoft::WRL::ComPtr<ID3D11Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader_;
    Microsoft::WRL::ComPtr<ID3D11CommandList> pendingCommands_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> scratch_,scene_;
    uint64_t value_=0,lastFrame_=0;
    bool enabled_=false,stopped_=false,pending_=false,seen_=false;
};
}
