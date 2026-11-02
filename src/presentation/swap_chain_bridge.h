#pragma once
#include <d3d11.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#include <memory>

namespace ac7 {
struct FinalCompositeInputs;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> prepareHudlessTarget(ID3D11DeviceContext*,ID3D11RenderTargetView*,std::shared_ptr<const FinalCompositeInputs>);
void discardHudlessTarget(ID3D11RenderTargetView*);
HWND createPresentationWindow();
void closePresentationWindow(HWND)noexcept;
IDXGISwapChain4* createPresentationBridge(IDXGIFactory*,ID3D11Device*,HWND,IDXGISwapChain*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC&,IDXGIOutput*,void(*failure)(const char*)noexcept);
}
