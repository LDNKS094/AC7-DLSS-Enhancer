#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <stdexcept>
namespace ac7 {
struct GuideCandidate {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth, velocity;
    Microsoft::WRL::ComPtr<ID3D11Buffer> view;
    D3D11_VIEWPORT viewport{};
};
inline bool sameViewport(const D3D11_VIEWPORT& a, const D3D11_VIEWPORT& b) {
    return a.TopLeftX == b.TopLeftX && a.TopLeftY == b.TopLeftY && a.Width == b.Width && a.Height == b.Height && a.MinDepth == b.MinDepth && a.MaxDepth == b.MaxDepth;
}
class GuideCandidates {
public:
    void clear() { values_.clear(); }
    size_t size() const { return values_.size(); }
    void add(GuideCandidate candidate) {
        for (const auto& existing : values_)
            if (existing.depth.Get() == candidate.depth.Get() && existing.velocity.Get() == candidate.velocity.Get() && existing.view.Get() == candidate.view.Get() && sameViewport(existing.viewport, candidate.viewport)) return;
        if (values_.size() == 16) throw std::runtime_error("More than 16 unique guide candidates in one Present interval");
        values_.push_back(std::move(candidate));
    }
    const GuideCandidate* find(ID3D11Resource* depth, ID3D11Buffer* view, const D3D11_VIEWPORT& viewport) const {
        const GuideCandidate* result = nullptr;
        for (const auto& candidate : values_) {
            if (candidate.depth.Get() != depth || candidate.view.Get() != view || !sameViewport(candidate.viewport, viewport)) continue;
            if (result) throw std::runtime_error("Ambiguous velocity candidates for prewarp depth/View/viewport");
            result = &candidate;
        }
        return result;
    }
private:
    std::vector<GuideCandidate> values_;
};
}
