#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>

namespace ac7 {
// Serialized owner. All feature/transport users must retire before destruction, outside loader lock.
class ModelContext {
public:
    explicit ModelContext(ID3D11Device*);
    ~ModelContext();
    ModelContext(const ModelContext&)=delete;
    ModelContext&operator=(const ModelContext&)=delete;
    ID3D11Device*sourceDevice()const{return source_.Get();}
    ID3D12Device*device()const{return device_.Get();}
    ID3D12CommandQueue*queue()const{return queue_.Get();}
    void initializeNgx();
private:
    Microsoft::WRL::ComPtr<ID3D11Device>source_;
    Microsoft::WRL::ComPtr<ID3D12Device>device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>queue_;
    bool ngxInitialized_=false;
};
}
