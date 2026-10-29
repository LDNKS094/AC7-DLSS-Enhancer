#include "hdr_proxy.h"
#include <d3dcompiler.h>
#include <cmath>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace ac7 {
namespace {
void check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("HDR proxy HRESULT "+std::to_string(static_cast<unsigned>(hr)));}
struct Constants {ViewRect rect;float exposure;float padding[3]{};};
}
HdrProxy::HdrProxy(ID3D11Device* device,uint32_t width,uint32_t height):width_(width),height_(height),device_(device) {
    if(!device||!width||!height||width>8192||height>8192)throw std::invalid_argument("Invalid HDR device/extent");
    ComPtr<ID3D11Device1> device1;check(device->QueryInterface(IID_PPV_ARGS(&device1)));
    D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,chosen{};
    check(device1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&state_));
    for(unsigned i=0;i<2;++i) {
        ComPtr<ID3DBlob> code,error;
        HRESULT hr=D3DCompileFromFile(AC7_HDR_SHADER_PATH,nullptr,nullptr,i?"Restore":"Prepare","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS,0,&code,&error);
        if(FAILED(hr))throw std::runtime_error(error?std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):"HDR shader compile failed");
        check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shaders_[i]));
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=i?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        UINT support=0;check(device->CheckFormatSupport(d.Format,&support));if(!(support&D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))throw std::runtime_error("HDR typed UAV store unavailable");
        auto& target=i?restored_:prepared_;check(device->CreateTexture2D(&d,nullptr,target.ReleaseAndGetAddressOf()));
        check(device->CreateUnorderedAccessView(target.Get(),nullptr,&outputs_[i]));
    }
    check(device->CreateShaderResourceView(prepared_.Get(),nullptr,&preparedView_));
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(device->CreateBuffer(&cb,nullptr,&constants_));
}
void HdrProxy::record(ID3D11DeviceContext* context,ID3D11Texture2D* hdr,ViewRect rect,float exposure,ID3D11Texture2D* neural) {
    if(!context||!hdr||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||rect.width!=width_||rect.height!=height_||!std::isfinite(exposure)||exposure<1.f/65504||exposure>65504)throw std::invalid_argument("Invalid HDR record arguments");
    ComPtr<ID3D11Device> actual;context->GetDevice(&actual);if(actual.Get()!=device_.Get())throw std::invalid_argument("HDR context device mismatch");
    std::array<ComPtr<ID3D11ShaderResourceView>,2> inputs;
    ID3D11Texture2D* textures[]={hdr,neural};
    for(unsigned i=0;i<2;++i) {
        if(!textures[i])continue;
        textures[i]->GetDevice(actual.ReleaseAndGetAddressOf());if(actual.Get()!=device_.Get())throw std::invalid_argument("HDR texture device mismatch");
        D3D11_TEXTURE2D_DESC d{};textures[i]->GetDesc(&d);
        if(d.Format!=(i?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R11G11B10_FLOAT)||d.MipLevels!=1||d.ArraySize!=1||d.SampleDesc.Count!=1||d.SampleDesc.Quality||!(d.BindFlags&D3D11_BIND_SHADER_RESOURCE))throw std::invalid_argument("Unsupported HDR texture layout");
        if(i?(d.Width!=width_||d.Height!=height_):(d.Width<width_||d.Height<height_||rect.x>d.Width-width_||rect.y>d.Height-height_))throw std::invalid_argument("Invalid HDR region");
        if(textures[i]==prepared_.Get()||textures[i]==restored_.Get())throw std::invalid_argument("HDR input/output alias");
        check(device_->CreateShaderResourceView(textures[i],nullptr,&inputs[i]));
    }
    ComPtr<ID3D11DeviceContext1> context1;check(context->QueryInterface(IID_PPV_ARGS(&context1)));
    Constants c{rect,exposure};context->UpdateSubresource(constants_.Get(),0,nullptr,&c,0,0);
    ComPtr<ID3DDeviceContextState> previous;context1->SwapDeviceContextState(state_.Get(),&previous);
    ID3D11Buffer* cb=constants_.Get();context->CSSetConstantBuffers(0,1,&cb);
    ID3D11ShaderResourceView* nullSrvs[3]{};ID3D11UnorderedAccessView* nullUav=nullptr;
    context->CSSetShaderResources(0,3,nullSrvs);context->CSSetShader(shaders_[0].Get(),nullptr,0);
    ID3D11ShaderResourceView* hdrView=inputs[0].Get();context->CSSetShaderResources(0,1,&hdrView);
    ID3D11UnorderedAccessView* output=outputs_[0].Get();context->CSSetUnorderedAccessViews(0,1,&output,nullptr);context->Dispatch((width_+7)/8,(height_+7)/8,1);
    context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    ID3D11ShaderResourceView* srvs[]={inputs[0].Get(),preparedView_.Get(),neural?inputs[1].Get():preparedView_.Get()};context->CSSetShaderResources(0,3,srvs);
    output=outputs_[1].Get();context->CSSetUnorderedAccessViews(0,1,&output,nullptr);context->CSSetShader(shaders_[1].Get(),nullptr,0);context->Dispatch((width_+7)/8,(height_+7)/8,1);
    // No throwing calls between swaps; private bindings never alter the saved game context state.
    context1->SwapDeviceContextState(previous.Get(),nullptr);
}
}
