#include "guide_normalizer.h"
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <limits>
using Microsoft::WRL::ComPtr;
using Bytes = std::vector<uint8_t>;
static void need(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
static void checked(HRESULT hr, int line) { if (FAILED(hr)) throw std::runtime_error("D3D11 validation HRESULT " + std::to_string(hr) + " at line " + std::to_string(line)); }
#define check(expression) checked((expression), __LINE__)
struct Raw {
    uint32_t width, height, format, bpp;
    Bytes bytes;
};
static Raw load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); need(bool(stream), "Cannot open raw guide file");
    uint32_t h[4]; stream.read(reinterpret_cast<char*>(h), sizeof(h)); need(bool(stream), "Short raw header");
    need(h[0] && h[1] && h[0] <= 8192 && h[1] <= 8192 && (h[3] == 4 || h[3] == 8), "Unsupported raw extent/stride");
    Raw r{h[0], h[1], h[2], h[3], Bytes(size_t(h[0]) * h[1] * h[3])};
    stream.read(reinterpret_cast<char*>(r.bytes.data()), r.bytes.size()); need(bool(stream) && stream.peek() == EOF, "Raw payload size mismatch"); return r;
}
static ComPtr<ID3D11Texture2D> upload(ID3D11Device* device, const Raw& raw, UINT flags) {
    D3D11_TEXTURE2D_DESC d{}; d.Width = raw.width; d.Height = raw.height; d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT(raw.format); d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = flags;
    D3D11_SUBRESOURCE_DATA initial{raw.bytes.data(), raw.width * raw.bpp, 0};
    ComPtr<ID3D11Texture2D> result; check(device->CreateTexture2D(&d, &initial, &result)); return result;
}
static Bytes download(ID3D11DeviceContext* context, ID3D11Texture2D* texture, unsigned bpp) {
    D3D11_TEXTURE2D_DESC d; texture->GetDesc(&d); auto w = d.Width, h = d.Height;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = d.MiscFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Device> device; context->GetDevice(&device);
    ComPtr<ID3D11Texture2D> staging; check(device->CreateTexture2D(&d, nullptr, &staging));
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped; check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    Bytes result(size_t(w) * h * bpp);
    for (unsigned y = 0; y < h; y++) std::memcpy(result.data() + size_t(y) * w * bpp, static_cast<uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, w * bpp);
    context->Unmap(staging.Get(), 0); check(device->GetDeviceRemovedReason()); return result;
}
static float number(const Bytes& b, size_t offset) { float v; std::memcpy(&v, b.data() + offset, 4); return v; }
static void store(const std::filesystem::path& path, uint32_t w, uint32_t h, uint32_t format, uint32_t bpp, const Bytes& b) {
    std::ofstream s(path, std::ios::binary); uint32_t header[]{w,h,format,bpp};
    s.write(reinterpret_cast<char*>(header), sizeof(header)); s.write(reinterpret_cast<const char*>(b.data()), b.size()); need(bool(s), "Output write failed");
}
static void synthetic(ID3D11Device* device, ID3D11DeviceContext* context) {
    Raw d{4,2,19,8,Bytes(4*2*8)}, m{4,2,35,4,Bytes(4*2*4)};
    for (unsigned i = 0; i < 8; i++) { float z = .5f; std::memcpy(d.bytes.data()+i*8, &z, 4); }
    uint16_t packed[2]{32767,32767}; std::memcpy(m.bytes.data(), packed, 4);
    uint16_t moving[2]{uint16_t(std::lround((.25f*.2495f+.499992f)*65535)),uint16_t(std::lround((-.125f*.2495f+.499992f)*65535))};
    std::memcpy(m.bytes.data()+4,moving,4);
    const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(d.bytes.data()+7*8,&nan,4);
    auto depth = upload(device,d,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE), motion = upload(device,m,D3D11_BIND_SHADER_RESOURCE);
    ac7::GuideNormalizer normalizer(device,4,2);
    ac7::ClipToPrevious identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    normalizer.record(context,depth.Get(),motion.Get(),{0,0,4,2},identity);
    const auto out = download(context,normalizer.motion(),8), mask = download(context,normalizer.validity(),4);
    for (unsigned i=0;i<8;i++) { uint32_t v; std::memcpy(&v,mask.data()+i*4,4); need(v==(i==7?3u:i<2?2u:1u),"Packed validity/sentinel mismatch");
        const float expectedX=i==1?.25f:0,expectedY=i==1?-.125f:0;
        need(std::abs(number(out,i*8)-expectedX)<4e-5f && std::abs(number(out,i*8+4)-expectedY)<4e-5f,"Identity/packed motion mismatch"); }
    auto translated=identity;translated[3][0]=.125f;normalizer.record(context,depth.Get(),motion.Get(),{0,0,4,2},translated);
    const auto camera=download(context,normalizer.motion(),8);need(std::abs(number(camera,2*8)+.125f)<1e-6f,"Camera motion direction mismatch");
    ac7::ClipToPrevious singular{}; normalizer.record(context,depth.Get(),motion.Get(),{0,0,4,2},singular);
    const auto invalid = download(context,normalizer.validity(),4);
    uint32_t first,second;std::memcpy(&first,invalid.data(),4);std::memcpy(&second,invalid.data()+8,4);
    need(first==2 && second==3,"Invalid camera reprojection must be explicitly marked, not admitted as zero motion");
    bool refused=false; try { normalizer.record(context,depth.Get(),motion.Get(),{1,0,4,2},identity); } catch(const std::invalid_argument&) {refused=true;}
    need(refused,"Out-of-bounds view was accepted");
}
int main(int argc, char** argv) try {
    need(argc==3,"Usage: validate-guides CHECKPOINT_DIRECTORY PARAMETERS_BIN");
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL actual, level=D3D_FEATURE_LEVEL_11_0;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,&actual,&context));
    synthetic(device.Get(),context.Get());
    std::filesystem::path directory=argv[1];
    const auto d=load(directory/"depth.raw"), m=load(directory/"velocity.raw"), reference=load(directory/"reference-motion-ndc.raw");
    need(d.format==19 && d.bpp==8 && m.format==35 && m.bpp==4 && d.width==m.width && d.height==m.height,"Capture formats disagree");
    struct Parameters {ac7::ViewRect rect; ac7::ClipToPrevious transform;} params;
    static_assert(sizeof(params)==80);
    std::ifstream constants(argv[2],std::ios::binary);constants.read(reinterpret_cast<char*>(&params),sizeof(params));need(bool(constants)&&constants.peek()==EOF,"Invalid parameter file");
    need(reference.format==16 && reference.bpp==8 && reference.width==d.width && reference.height==d.height,"Reference grid mismatch");
    auto depth=upload(device.Get(),d,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE), motion=upload(device.Get(),m,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    ac7::GuideNormalizer normalizer(device.Get(),params.rect.width,params.rect.height);
    // Keep both sources bound as graphics outputs to exercise hazard isolation and restoration.
    ComPtr<ID3D11RenderTargetView> target;check(device->CreateRenderTargetView(motion.Get(),nullptr,&target));
    D3D11_DEPTH_STENCIL_VIEW_DESC depthView{};depthView.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;depthView.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv;check(device->CreateDepthStencilView(depth.Get(),&depthView,&dsv));
    ID3D11RenderTargetView* rtv=target.Get();context->OMSetRenderTargets(1,&rtv,dsv.Get());
    D3D11_VIEWPORT viewport{1280,0,2560,1440,0,1};context->RSSetViewports(1,&viewport);
    // A separate CS buffer proves the compute binding bank survives insertion too.
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> sentinel;check(device->CreateBuffer(&bd,nullptr,&sentinel));ID3D11Buffer* cb=sentinel.Get();context->CSSetConstantBuffers(0,1,&cb);
    normalizer.record(context.Get(),depth.Get(),motion.Get(),params.rect,params.transform);
    ComPtr<ID3D11RenderTargetView> afterTarget;ComPtr<ID3D11DepthStencilView> afterDepth;context->OMGetRenderTargets(1,&afterTarget,&afterDepth);
    ComPtr<ID3D11Buffer> afterCb;context->CSGetConstantBuffers(0,1,&afterCb);
    D3D11_VIEWPORT afterViewport{};UINT count=1;context->RSGetViewports(&count,&afterViewport);
    need(afterTarget.Get()==target.Get() && afterDepth.Get()==dsv.Get() && afterCb.Get()==sentinel.Get() &&
        count==1 && std::memcmp(&viewport,&afterViewport,sizeof(viewport))==0,"Graphics/compute state restoration failed");
    auto values=download(context.Get(),normalizer.motion(),8), depths=download(context.Get(),normalizer.depth(),4), mask=download(context.Get(),normalizer.validity(),4);
    double maxError=0;uint64_t packed=0,camera=0,invalid=0;
    for(unsigned y=0;y<params.rect.height;y++)for(unsigned x=0;x<params.rect.width;x++) {
        size_t dst=size_t(y)*params.rect.width+x,src=size_t(y+params.rect.y)*d.width+x+params.rect.x;
        uint32_t validity;std::memcpy(&validity,mask.data()+dst*4,4);packed+=validity==2;camera+=validity==1;invalid+=validity==3;
        need(validity>=1&&validity<=3,"Unexpected guide mask");
        need(std::memcmp(depths.data()+dst*4,d.bytes.data()+src*8,4)==0,"Depth plane extraction changed a value");
        for(unsigned k=0;k<2;k++){const float v=number(values,dst*8+k*4),expected=number(reference.bytes,src*8+k*4);need(std::isfinite(v),"Nonfinite normalized motion");maxError=std::max(maxError,double(std::abs(v-expected)));}
    }
    need(invalid==0,"Captured guide contains invalid reprojection");need(maxError<=2e-6,"GPU motion differs from CPU shader-derived reference");
    store(directory/"gpu-motion-cropped.raw",params.rect.width,params.rect.height,16,8,values);
    store(directory/"gpu-depth-cropped.raw",params.rect.width,params.rect.height,41,4,depths);
    std::cout<<"PASS D3D11 hardware normalization; state restored; depth bit-exact; cropped="<<params.rect.width<<"x"<<params.rect.height<<" packed="<<packed<<" camera="<<camera<<" invalid="<<invalid<<" maxNdcError="<<maxError<<" synthetic=passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
