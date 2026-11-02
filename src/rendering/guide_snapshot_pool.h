#pragma once
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace ac7 {
class GuideSnapshotPool;
class GuideSnapshot {
public:
    ID3D12Resource*depth()const{return shared12_[0].Get();}
    ID3D12Resource*motion()const{return shared12_[1].Get();}
    void consumedBy(ID3D12Fence*,UINT64 value);
private:
    friend class GuideSnapshotPool;
    struct Reader {Microsoft::WRL::ComPtr<ID3D12Fence>fence;UINT64 value;};
    std::mutex lock_;
    bool retired();
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2>shared11_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,2>shared12_;
    std::vector<Reader>readers_;
    unsigned width_=0,height_=0;
};
// Borrowers keep a strong frame lease through submission, then register every GPU reader.
// copy uses the serialized immediate RHI context; no weak lease re-acquisition.
class GuideSnapshotPool {
public:
    GuideSnapshotPool(ID3D11Device*,ID3D12Device*);
    std::shared_ptr<GuideSnapshot>copy(ID3D11DeviceContext*,ID3D11Texture2D*depth,ID3D11Texture2D*motion);
private:
    Microsoft::WRL::ComPtr<ID3D11Device>device11_;
    Microsoft::WRL::ComPtr<ID3D12Device>device12_;
    std::vector<std::shared_ptr<GuideSnapshot>>snapshots_;
};
}
