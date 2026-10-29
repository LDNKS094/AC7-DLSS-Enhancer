#include "neutral_entry.h"
#include <bcrypt.h>
#include <array>
#include <limits>
#include <stdexcept>
namespace ac7 {
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Neutral entry D3D11 failure");}
bool layout(ID3D11Texture2D*t,DXGI_FORMAT format){D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);return d.Width==5120&&d.Height==1440&&d.Format==format&&d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1&&d.SampleDesc.Quality==0;}
}
NeutralEntry::NeutralEntry(ID3D11Device*d):device_(d){
    if(!d)throw std::invalid_argument("Missing neutral device");
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> c;d->GetImmediateContext(&c);check(c.As(&context_));
    Microsoft::WRL::ComPtr<ID3D11Device5>d5;check(d->QueryInterface(IID_PPV_ARGS(&d5)));
    check(device_->CreateDeferredContext(0,&deferred_));check(d5->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=2560;desc.Height=1440;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R11G11B10_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;check(d->CreateTexture2D(&desc,nullptr,&scratch_));
}
bool NeutralEntry::matchesBytecode(const void*code,size_t size){
    if(!code||size!=628)return false;
    std::array<unsigned char,32> hash{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,static_cast<PUCHAR>(const_cast<void*>(code)),628,hash.data(),32)<0)return false;
    constexpr unsigned char expected[]={0xaf,0x12,0x47,0xe3,0xa4,0x73,0x86,0x19,0x34,0x04,0x7c,0x3c,0xab,0xe8,0x49,0xda,0xe6,0x57,0x7d,0xc6,0x11,0x89,0xbc,0x56,0xba,0x22,0x5f,0x33,0x51,0x29,0x4e,0xc0};
    for(unsigned i=0;i<32;i++)if(hash[i]!=expected[i])return false;
    return true;
}
bool NeutralEntry::registerShader(ID3D11PixelShader*s,const void*code,size_t size){
    if(stopped_||enabled_||pending_||!s||!matchesBytecode(code,size))return false;
    Microsoft::WRL::ComPtr<ID3D11Device> owner;s->GetDevice(&owner);if(owner.Get()!=device_.Get())return false;
    if(shader_&&shader_.Get()!=s)return false; // One fixed view/shader object; host must rebuild on shader replacement.
    shader_=s;return true;
}
bool NeutralEntry::drained(){
    if(!pending_)return true;const auto completed=fence_->GetCompletedValue();if(completed==UINT64_MAX){stop();throw std::runtime_error("Neutral retirement device lost");}
    if(completed<value_)return false;pending_=false;scene_.Reset();pendingCommands_.Reset();return true;
}
NeutralEntry::Result NeutralEntry::beforeDraw(ID3D11DeviceContext*c,uint64_t frame){
    if(stopped_)return Result::stopped;if(!enabled_)return Result::disabled;
    if(!c||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return Result::refused;
    Microsoft::WRL::ComPtr<ID3D11Device> owner;c->GetDevice(&owner);if(owner.Get()!=device_.Get()||c!=static_cast<ID3D11DeviceContext*>(context_.Get()))return Result::refused;
    if(!drained())return Result::pending;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);if(!shader_||ps.Get()!=shader_.Get())return Result::unmatched;
    if(seen_&&frame<=lastFrame_)return Result::refused;
    D3D11_VIEWPORT viewport{};UINT count=1;c->RSGetViewports(&count,&viewport);if(count!=1||viewport.TopLeftX!=1280||viewport.TopLeftY!=0||viewport.Width!=2560||viewport.Height!=1440||viewport.MinDepth!=0||viewport.MaxDepth!=1)return Result::refused;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;c->PSGetShaderResources(0,1,&srv);if(!srv)return Result::refused;D3D11_SHADER_RESOURCE_VIEW_DESC view{};srv->GetDesc(&view);if(view.Format!=DXGI_FORMAT_R11G11B10_FLOAT||view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Texture2D.MostDetailedMip!=0||view.Texture2D.MipLevels!=1)return Result::refused;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);Microsoft::WRL::ComPtr<ID3D11Texture2D> scene;if(FAILED(resource.As(&scene))||!layout(scene.Get(),DXGI_FORMAT_R11G11B10_FLOAT))return Result::refused;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(0,nullptr,&dsv);if(!dsv)return Result::refused;dsv->GetResource(resource.ReleaseAndGetAddressOf());Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;if(FAILED(resource.As(&depth))||!layout(depth.Get(),DXGI_FORMAT_R32G8X24_TYPELESS))return Result::refused;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dsv->GetDesc(&dv);if(dv.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT||dv.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D||dv.Texture2D.MipSlice!=0)return Result::refused;
    Microsoft::WRL::ComPtr<ID3D11Buffer> viewBuffer;c->PSGetConstantBuffers(1,1,&viewBuffer);if(!viewBuffer)return Result::refused;D3D11_BUFFER_DESC bd{};viewBuffer->GetDesc(&bd);if(bd.ByteWidth!=4096)return Result::refused;
    if(value_==std::numeric_limits<uint64_t>::max()){stop();return Result::refused;}
    const D3D11_BOX region{1280,0,0,3840,1440,1};deferred_->CopySubresourceRegion(scratch_.Get(),0,0,0,0,scene.Get(),0,&region);deferred_->CopySubresourceRegion(scene.Get(),0,1280,0,0,scratch_.Get(),0,nullptr);check(deferred_->FinishCommandList(FALSE,&pendingCommands_));
    // Direct copies inside the ordinary-device Draw detour hung; command-list execution passed both device paths.
    c->ExecuteCommandList(pendingCommands_.Get(),TRUE);scene_=scene;pending_=true;const auto signal=context_->Signal(fence_.Get(),++value_);seen_=true;lastFrame_=frame;if(FAILED(signal)){stop();throw std::runtime_error("Deferred neutral retirement signal failed");}return Result::applied;
}
}
