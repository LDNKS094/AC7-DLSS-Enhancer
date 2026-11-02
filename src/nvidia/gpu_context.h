#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mutex>
#include <filesystem>

namespace ac7 {
struct ModelCapabilities {bool sr=false,nr=false;};
std::filesystem::path modelRuntimeDirectory();
// Serialized owner. All feature/transport users must retire before destruction, outside loader lock.
class GpuContext {
public:
    // Process-pinned per-device owner, shared by creation hooks and the scene chain.
    static GpuContext& acquire(ID3D11Device*);
    ~GpuContext();
    GpuContext(const GpuContext&)=delete;
    GpuContext&operator=(const GpuContext&)=delete;
    ID3D11Device*sourceDevice()const{return source_.Get();}
    ID3D12Device*device()const{return device_.Get();}
    ID3D12CommandQueue*queue()const{return queue_.Get();}
    ID3D12CommandQueue*presentationQueue()const{return presentationQueue_.Get();}
    void initializeDevice();
    void initializeNgx();
    ModelCapabilities modelCapabilities(bool requestSr,bool requestNr);
private:
    explicit GpuContext(ID3D11Device*);
    bool initializeNgxLocked();
    struct Capability {bool known=false,available=false;};
    std::mutex initializationLock_;
    Microsoft::WRL::ComPtr<ID3D11Device>source_;
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>presentationQueue_;
    Microsoft::WRL::ComPtr<IDXGIAdapter>adapter_;
    Capability sr_,nr_;
    bool ngxInitialized_=false,ngxUnavailable_=false,capabilityApiUnavailable_=false;
};
}
