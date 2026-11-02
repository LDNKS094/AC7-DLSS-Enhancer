#include "motion_blur.h"
#include <d3d11.h>
#include "ac7_temporal_vertex.h"
#include "ac7_temporal_motion.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ac7 {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Temporal color HRESULT "+std::to_string(unsigned(hr)));}
struct alignas(16) Constants{
    std::array<float,4>low,high,sizes,motion,alignment;
    unsigned quality=0,pad[3]{};
};
static_assert(sizeof(Constants)==96);
std::array<float,4>rect(ViewRect r){return {float(r.x),float(r.y),float(r.width),float(r.height)};}
void valid(ID3D11Texture2D*t,ViewRect r){
    if(!t||!r.width||!r.height)throw std::invalid_argument("Missing temporal color resource or rect");
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
    if(d.SampleDesc.Count!=1||d.ArraySize!=1||d.MipLevels!=1||r.x>d.Width||r.y>d.Height||r.width>d.Width-r.x||r.height>d.Height-r.y)
        throw std::invalid_argument("Unsupported temporal color resource grid");
}
}
MotionBlur::MotionBlur(ID3D11Device*d):device_(d){
    if(!d)throw std::invalid_argument("Missing temporal color device");
    check(d->CreateDeferredContext(0,&deferred_));
    check(d->CreateVertexShader(ac7_temporal_vertex,sizeof(ac7_temporal_vertex),nullptr,&vertex_));
    check(d->CreatePixelShader(ac7_temporal_motion,sizeof(ac7_temporal_motion),nullptr,&motion_));
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(d->CreateBuffer(&cb,nullptr,&constants_));
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxAnisotropy=1;s.ComparisonFunc=D3D11_COMPARISON_NEVER;s.MaxLOD=D3D11_FLOAT32_MAX;
    check(d->CreateSamplerState(&s,&linear_));s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;check(d->CreateSamplerState(&s,&point_));
    auto raster=CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);raster.CullMode=D3D11_CULL_NONE;check(d->CreateRasterizerState(&raster,&raster_));
    auto depth=CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);depth.DepthEnable=FALSE;depth.StencilEnable=FALSE;check(d->CreateDepthStencilState(&depth,&depth_));
    auto blend=CD3D11_BLEND_DESC(D3D11_DEFAULT);check(d->CreateBlendState(&blend,&blend_));
}
ComPtr<ID3D11ShaderResourceView>MotionBlur::srv(ID3D11Texture2D*t){
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
    D3D11_SHADER_RESOURCE_VIEW_DESC s{};s.Format=d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS?DXGI_FORMAT_R16G16B16A16_FLOAT:d.Format;
    s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;s.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView>v;check(device_->CreateShaderResourceView(t,&s,&v));return v;
}
void MotionBlur::begin(ID3D11Texture2D*t,ViewRect output){
    valid(t,output);deferred_->ClearState();
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);D3D11_RENDER_TARGET_VIEW_DESC v{};
    v.Format=d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS?DXGI_FORMAT_R16G16B16A16_FLOAT:d.Format;v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView>rt;check(device_->CreateRenderTargetView(t,&v,&rt));
    ID3D11RenderTargetView*target=rt.Get();deferred_->OMSetRenderTargets(1,&target,nullptr);
    deferred_->OMSetDepthStencilState(depth_.Get(),0);deferred_->OMSetBlendState(blend_.Get(),nullptr,~0u);
    deferred_->RSSetState(raster_.Get());D3D11_VIEWPORT vp{float(output.x),float(output.y),float(output.width),float(output.height),0,1};deferred_->RSSetViewports(1,&vp);
    deferred_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);deferred_->VSSetShader(vertex_.Get(),nullptr,0);
    ID3D11SamplerState*s[]={linear_.Get(),point_.Get()};deferred_->PSSetSamplers(0,2,s);
}
ComPtr<ID3D11CommandList>MotionBlur::finish(ID3D11Texture2D*t,ID3D11Texture2D*resolved){
    deferred_->Draw(3,0);deferred_->ClearState();
    if(resolved&&resolved!=t)deferred_->CopyResource(resolved,t);
    ComPtr<ID3D11CommandList>list;check(deferred_->FinishCommandList(FALSE,&list));return list;
}
ComPtr<ID3D11CommandList>MotionBlur::motionBlur(ID3D11Texture2D*color,ID3D11Texture2D*flat,ID3D11Texture2D*tiles,ID3D11Texture2D*target,ID3D11Texture2D*resolved,ViewRect low,ViewRect high,const std::array<float,2>&jitter,const std::array<float,4>&parameters,unsigned quality){
    valid(color,high);valid(flat,low);if(!tiles||quality<1||quality>4||target==color)throw std::invalid_argument("Unsupported temporal motion input");
    D3D11_TEXTURE2D_DESC a{},g{},t{};color->GetDesc(&a);flat->GetDesc(&g);tiles->GetDesc(&t);
    if(t.Width<(low.width+15)/16||t.Height<(low.height+15)/16)throw std::invalid_argument("Motion tile allocation does not cover the View");
    for(float v:parameters)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite motion parameters");
    Constants c{};c.low=rect(low);c.high=rect(high);c.sizes={1.f/a.Width,1.f/a.Height,1.f/g.Width,1.f/g.Height};c.motion=parameters;c.alignment={jitter[0],jitter[1],1.f/t.Width,1.f/t.Height};c.quality=quality;
    auto cv=srv(color),fv=srv(flat),tv=srv(tiles);begin(target,high);
    deferred_->UpdateSubresource(constants_.Get(),0,nullptr,&c,0,0);
    ID3D11Buffer*b=constants_.Get();ID3D11ShaderResourceView*v[]={cv.Get(),fv.Get(),tv.Get()};deferred_->PSSetConstantBuffers(0,1,&b);deferred_->PSSetShaderResources(0,3,v);deferred_->PSSetShader(motion_.Get(),nullptr,0);
    return finish(target,resolved);
}
}
