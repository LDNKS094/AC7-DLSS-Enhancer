#include "guide_normalizer.h"
#include "ac7_guide_normalizer.h"
#include <cmath>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("Guide normalization HRESULT " + std::to_string(hr)); }
struct Constants { ViewRect rect; ClipToPrevious matrix; std::array<float,4> jitter; };
static_assert(sizeof(Constants) == 96);
}
GuideNormalizer::GuideNormalizer(ID3D11Device* device, uint32_t width, uint32_t height)
    : width_(width), height_(height), device_(device) {
    if (!device || !width || !height || width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::invalid_argument("Invalid guide extent/device");
    ComPtr<ID3D11Device1> device1; check(device->QueryInterface(IID_PPV_ARGS(&device1)));
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0, actual;
    check(device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &actual, &state_));
    ComPtr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);check(immediate.As(&context_));
    check(device->CreateComputeShader(ac7_guide_normalizer,sizeof(ac7_guide_normalizer),nullptr,&shader_));
    D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = sizeof(Constants); buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    check(device->CreateBuffer(&buffer, nullptr, &constants_));
    ComPtr<ID3D11Texture2D>* textures[] = { &depth_, &motion_ };
    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_TYPELESS };
    for (unsigned i = 0; i < 2; i++) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = width; d.Height = height; d.MipLevels = d.ArraySize = 1; d.SampleDesc.Count = 1;
        d.Format = formats[i]; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&d, nullptr, textures[i]->GetAddressOf()));
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16_UINT;view.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
        check(device->CreateUnorderedAccessView(textures[i]->Get(), i==1?&view:nullptr, &views_[i]));
    }
}
void GuideNormalizer::record(ID3D11DeviceContext* context, ID3D11Texture2D* depth,
                             ID3D11Texture2D* packedMotion, ViewRect rect, const ClipToPrevious& matrix,const std::array<float,2>& projectionJitter) {
    if (!context || !depth || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
        rect.width != width_ || rect.height != height_) throw std::invalid_argument("Invalid guide context/rect/input");
    if(context!=context_.Get())throw std::invalid_argument("Guide context mismatch");
    ComPtr<ID3D11Device> actual;
    ID3D11Texture2D* inputs[] = { depth, packedMotion };
    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R16G16_UNORM };
    for (unsigned i = 0; i < 2; i++) {
        // D3D11 null SRV reads zero: absent native velocity uses the existing real camera transform fallback.
        if(i==1&&!inputs[i]){inputs_[i].Reset();inputViews_[i].Reset();continue;}
        if(inputs_[i].Get()!=inputs[i]){inputs[i]->GetDevice(&actual);if(actual!=device_)throw std::invalid_argument("Guide resource device mismatch");}
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
    Constants constants{rect, matrix,{projectionJitter[0],projectionJitter[1],0,0}}; context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
    ComPtr<ID3DDeviceContextState> previous; context_->SwapDeviceContextState(state_.Get(), &previous);
    context->CSSetShader(shader_.Get(), nullptr, 0);
    ID3D11Buffer* cb = constants_.Get(); context->CSSetConstantBuffers(0, 1, &cb);
    ID3D11ShaderResourceView* srvs[] = { inputViews_[0].Get(), inputViews_[1].Get() }; context->CSSetShaderResources(0, 2, srvs);
    ID3D11UnorderedAccessView* uavs[] = { views_[0].Get(), views_[1].Get() }; context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    context->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    // No throwing calls between state swaps. Context state owns the private bindings.
    context_->SwapDeviceContextState(previous.Get(), nullptr);
}
}
