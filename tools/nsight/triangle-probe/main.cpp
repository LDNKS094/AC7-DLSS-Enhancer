#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT kWidth = 1280;
constexpr UINT kHeight = 720;
constexpr UINT kSelfTestFrames = 120;

struct Constants { float angle; float padding[3]; };
struct App {
    HWND window = nullptr;
    bool running = true;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> constants;
};
App app;

void Check(HRESULT hr, const char* action) {
    if (FAILED(hr)) throw std::runtime_error(action);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE || message == WM_DESTROY) {
        app.running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CreateWindowForProbe() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AC7NsightTriangleProbe";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT rect{0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight)};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    app.window = CreateWindowW(wc.lpszClassName, L"AC7 Nsight D3D11 Triangle Probe",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.window) throw std::runtime_error("CreateWindowW failed");
    ShowWindow(app.window, SW_SHOW);
}

void CreateDeviceAndPipeline() {
    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = kWidth;
    swapDesc.BufferDesc.Height = kHeight;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 1;
    swapDesc.OutputWindow = app.window;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    Check(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &swapDesc, &app.swapChain,
        &app.device, &featureLevel, &app.context), "D3D11CreateDeviceAndSwapChain failed");

    ComPtr<ID3D11Texture2D> backBuffer;
    Check(app.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer failed");
    Check(app.device->CreateRenderTargetView(backBuffer.Get(), nullptr, &app.renderTarget), "CreateRenderTargetView failed");

    static constexpr char shader[] = R"(
cbuffer Constants : register(b0) { float angle; float3 padding; };
struct VSOut { float4 position : SV_Position; float3 color : COLOR0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 points[3] = { float2(0.0, 0.65), float2(0.65, -0.5), float2(-0.65, -0.5) };
    float3 colors[3] = { float3(1,0.15,0.1), float3(0.1,1,0.25), float3(0.15,0.35,1) };
    float s = sin(angle), c = cos(angle);
    float2 p = points[id];
    VSOut o; o.position = float4(c*p.x-s*p.y, s*p.x+c*p.y, 0.5, 1); o.color = colors[id]; return o;
}
float4 PSMain(VSOut i) : SV_Target { return float4(i.color, 1); }
)";
    ComPtr<ID3DBlob> vs, ps, errors;
    HRESULT hr = D3DCompile(shader, sizeof(shader) - 1, "triangle-probe.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &errors);
    if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "VS compile failed");
    errors.Reset();
    hr = D3DCompile(shader, sizeof(shader) - 1, "triangle-probe.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &errors);
    if (FAILED(hr)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "PS compile failed");
    Check(app.device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &app.vertexShader), "CreateVertexShader failed");
    Check(app.device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &app.pixelShader), "CreatePixelShader failed");

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Check(app.device->CreateBuffer(&cb, nullptr, &app.constants), "CreateBuffer failed");

    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(kWidth), static_cast<float>(kHeight), 0, 1};
    app.context->RSSetViewports(1, &viewport);
    ID3D11RenderTargetView* target = app.renderTarget.Get();
    app.context->OMSetRenderTargets(1, &target, nullptr);
    app.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    app.context->VSSetShader(app.vertexShader.Get(), nullptr, 0);
    app.context->PSSetShader(app.pixelShader.Get(), nullptr, 0);
    ID3D11Buffer* constantBuffer = app.constants.Get();
    app.context->VSSetConstantBuffers(0, 1, &constantBuffer);
}

bool PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return app.running;
}

void Draw(UINT frame) {
    const float clear[] = {0.025f, 0.035f, 0.06f, 1.0f};
    app.context->ClearRenderTargetView(app.renderTarget.Get(), clear);
    Constants data{static_cast<float>(frame) * 0.025f, {0, 0, 0}};
    app.context->UpdateSubresource(app.constants.Get(), 0, nullptr, &data, 0, 0);
    app.context->Draw(3, 0);
    Check(app.swapChain->Present(0, 0), "Present failed");
}
}

int main(int argc, char** argv) {
    const bool selfTest = argc > 1 && std::strcmp(argv[1], "--self-test") == 0;
    try {
        CreateWindowForProbe();
        CreateDeviceAndPipeline();
        UINT frames = 0;
        while (PumpMessages() && (!selfTest || frames < kSelfTestFrames)) {
            Draw(frames++);
            Sleep(16);
        }
        std::printf("PASS: rendered %u D3D11 triangle frames; Present returned S_OK.\n", frames);
        app.context->ClearState();
        return selfTest && frames != kSelfTestFrames ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s (HRESULT 0x%08lx)\n", e.what(), static_cast<unsigned long>(GetLastError()));
        return 1;
    }
}
