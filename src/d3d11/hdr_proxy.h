#pragma once
#include "guide_normalizer.h"
namespace ac7 {
// Two local color passes only; no NR invocation, interop or game hooks.
class HdrProxy {
public:
    HdrProxy(ID3D11Device* device,uint32_t width,uint32_t height);
    // Serialized immediate context. Caller supplies qualified finite/nonnegative scene HDR and actual exposure.
    // Null neural uses the actual FP16 prepared image as an identity diagnostic, not NR Enabled=0.
    void record(ID3D11DeviceContext* context,ID3D11Texture2D* hdr,ViewRect rect,float exposure,ID3D11Texture2D* neural=nullptr);
    ID3D11Texture2D* prepared() const {return prepared_.Get();}
    ID3D11Texture2D* restored() const {return restored_.Get();}
private:
    uint32_t width_,height_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    std::array<Microsoft::WRL::ComPtr<ID3D11ComputeShader>,2> shaders_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> prepared_,restored_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> preparedView_;
    std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>,2> outputs_;
};
}
