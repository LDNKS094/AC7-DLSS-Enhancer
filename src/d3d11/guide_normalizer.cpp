#include "guide_normalizer.h"
#include <d3dcompiler.h>
#include <cmath>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Guide normalization HRESULT " + std::to_string(hr)); }
constexpr char source[] = R"(
Texture2D<float> Depth : register(t0);
Texture2D<float2> PackedMotion : register(t1);
RWTexture2D<float> NormalizedDepth : register(u0);
#ifdef MODEL_MOTION
RWTexture2D<uint2> NormalizedMotion : register(u1);
#else
RWTexture2D<float2> NormalizedMotion : register(u1);
#endif
cbuffer Parameters : register(b0) { uint4 Rect; float4 Previous0, Previous1, Previous2, Previous3; float4 ProjectionJitter; };
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Rect.z || id.y >= Rect.w) return;
    uint2 at = id.xy + Rect.xy;
    float z = Depth.Load(int3(at,0));
    float2 encoded = PackedMotion.Load(int3(at,0));
    float2 motion = 0; uint valid = 3;
    if (isfinite(z) && z >= 0 && z <= 1) {
        if (encoded.x > 0) {
            motion = encoded * 4.008016 - 2.003978;
            valid = 2;
        } else {
            float2 current = (float2(id.xy) + .5) / float2(Rect.zw) * float2(2,-2) + float2(-1,1) - ProjectionJitter.xy;
            float4 previous = current.x * Previous0 + current.y * Previous1 + z * Previous2 + Previous3;
            if (all(isfinite(previous)) && abs(previous.w) > 1e-8) {
                motion = current - previous.xy / previous.w;
                if (all(isfinite(motion))) valid = 1;
            }
        }
    }
    NormalizedDepth[id.xy] = valid == 3 ? 0 : z;
#ifdef MODEL_MOTION
    motion = valid == 3 ? float2(0,0) : motion;
    // Convert explicitly before UINT storage; scale NDC by this View's pixel dimensions.
    NormalizedMotion[id.xy] = uint2(f32tof16(motion.x*(-.5*Rect.z)),f32tof16(motion.y*(.5*Rect.w)));
#else
    NormalizedMotion[id.xy] = valid == 3 ? float2(0,0) : motion;
#endif
}
)";
struct Constants { ViewRect rect; ClipToPrevious matrix; std::array<float,4> jitter; };
static_assert(sizeof(Constants) == 96);
}
GuideNormalizer::GuideNormalizer(ID3D11Device* device, uint32_t width, uint32_t height, bool modelMotion)
    : width_(width), height_(height), device_(device) {
    if (!device || !width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::invalid_argument("Invalid guide extent/device");
    ComPtr<ID3D11Device1> device1; check(device->QueryInterface(IID_PPV_ARGS(&device1)));
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0, actual;
    check(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &actual, &state_));
    ComPtr<ID3DBlob> code, errors;
    const D3D_SHADER_MACRO macros[]={{"MODEL_MOTION","1"},{nullptr,nullptr}};
    HRESULT hr = D3DCompile(source, sizeof(source)-1, "ac7_guide_normalizer", modelMotion?macros:nullptr, nullptr, "main", "cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &errors);
    if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Guide shader compilation failed");
    check(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader_));
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = sizeof(Constants); buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&buffer, nullptr, &constants_));
    ComPtr<ID3D11Texture2D>* textures[] = { &depth_, &motion_ };
    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R32_FLOAT, modelMotion?DXGI_FORMAT_R16G16_TYPELESS:DXGI_FORMAT_R32G32_FLOAT };
    for (unsigned i = 0; i < 2; i++) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = width; d.Height = height; d.MipLevels = d.ArraySize = 1; d.SampleDesc.Count = 1;
        d.Format = formats[i]; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&d, nullptr, textures[i]->GetAddressOf()));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16_UINT;view.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
        check(device->CreateUnorderedAccessView(textures[i]->Get(), modelMotion&&i==1?&view:nullptr, &views_[i]));
    }
}
void GuideNormalizer::record(ID3D11DeviceContext* context, ID3D11Texture2D* depth,
                             ID3D11Texture2D* packedMotion, ViewRect rect, const ClipToPrevious& matrix,const std::array<float,2>& projectionJitter) {
    if (!context || !depth || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
        rect.width != width_ || rect.height != height_) throw std::invalid_argument("Invalid guide context/rect/input");
    for (const auto& row : matrix) for (float v : row) if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite previous-clip transform");
    for(float v:projectionJitter)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite projection jitter");
    ComPtr<ID3D11Device> actual; context->GetDevice(&actual);
    if (actual.Get() != device_.Get()) throw std::invalid_argument("Guide context device mismatch");
    ID3D11Texture2D* inputs[] = { depth, packedMotion };
    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R16G16_UNORM };
    for (unsigned i = 0; i < 2; i++) {
        // D3D11 null SRV reads zero: absent native velocity uses the existing real camera transform fallback.
        if(i==1&&!inputs[i]){inputs_[i].Reset();inputViews_[i].Reset();continue;}
        actual.Reset(); inputs[i]->GetDevice(&actual);
        if (actual.Get() != device_.Get()) throw std::invalid_argument("Guide resource device mismatch");
        D3D11_TEXTURE2D_DESC d; inputs[i]->GetDesc(&d);
        if (d.Format != formats[i] || d.MipLevels != 1 || d.ArraySize != 1 || d.SampleDesc.Count != 1 || d.SampleDesc.Quality ||
            !(d.BindFlags & D3D11_BIND_SHADER_RESOURCE) || d.Width < rect.width || d.Height < rect.height ||
            rect.x > d.Width - rect.width || rect.y > d.Height - rect.height) throw std::invalid_argument("Unsupported guide input layout/region");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = i ? formats[i] : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = 1;
        if(inputs_[i].Get()!=inputs[i]){
            ComPtr<ID3D11ShaderResourceView>next;check(device_->CreateShaderResourceView(inputs[i], &srv, &next));
            inputViews_[i]=std::move(next);inputs_[i]=inputs[i];
        }
    }
    ComPtr<ID3D11DeviceContext1> context1; check(context->QueryInterface(IID_PPV_ARGS(&context1)));
    Constants constants{rect, matrix,{projectionJitter[0],projectionJitter[1],0,0}}; context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
    ComPtr<ID3DDeviceContextState> previous; context1->SwapDeviceContextState(state_.Get(), &previous);
    context->CSSetShader(shader_.Get(), nullptr, 0);
    ID3D11Buffer* cb = constants_.Get(); context->CSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* srvs[] = { inputViews_[0].Get(), inputViews_[1].Get() }; context->CSSetShaderResources(0, 2, srvs);
    ID3D11UnorderedAccessView* uavs[] = { views_[0].Get(), views_[1].Get() }; context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    context->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    // No throwing calls between state swaps. Context state owns the private bindings.
    context1->SwapDeviceContextState(previous.Get(), nullptr);
}
}
