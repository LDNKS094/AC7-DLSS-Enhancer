#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <mutex>
#include <filesystem>

namespace ac7 {
std::filesystem::path modelRuntimeDirectory();
// Serialized owner. All feature/transport users must retire before destruction, outside loader lock.
class ModelContext {
public:
    // Process-pinned per-device owner, shared by creation hooks and the scene chain.
    static ModelContext& acquire(ID3D11Device*);
    ~ModelContext();
    ModelContext(const ModelContext&)=delete;
    ModelContext&operator=(const ModelContext&)=delete;
    ID3D11Device*sourceDevice()const{return source_.Get();}
    ID3D12Device*device()const{return device_.Get();}
    ID3D12CommandQueue*queue()const{return queue_.Get();}
    ID3D12CommandQueue*presentationQueue()const{return presentationQueue_.Get();}
    void initializeDevice();
    void initializeNgx();
private:
    explicit ModelContext(ID3D11Device*);
    std::mutex initializationLock_;
    Microsoft::WRL::ComPtr<ID3D11Device>source_;
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>presentationQueue_;
    bool ngxInitialized_=false;
};
}
