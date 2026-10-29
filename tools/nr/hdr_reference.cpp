/*
 * HDR transfer equations adapted from NVIDIA RTX Remix (MIT):
 * Copyright (c) 2022-2026, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
// Offline CPU reference, not a GPU conversion pass or an AC7 color-space calibration.
#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace DirectX;
using namespace DirectX::PackedVector;
namespace fs=std::filesystem;
namespace {
constexpr unsigned W=2560,H=1440;
using Rgb=std::array<float,3>;
struct Image {std::array<unsigned,4> header;std::vector<unsigned char> data;};
Image read(const fs::path& path,std::array<unsigned,4> expected) {
    Image image{expected,{}};std::ifstream file(path,std::ios::binary|std::ios::ate);
    const size_t size=16+size_t(expected[0])*expected[1]*expected[3];
    if(!file||file.tellg()!=static_cast<std::streamoff>(size))throw std::runtime_error("Unexpected raw length: "+path.string());
    std::array<unsigned,4> header{};file.seekg(0);file.read(reinterpret_cast<char*>(header.data()),16);
    if(header!=expected)throw std::runtime_error("Unexpected raw format: "+path.string());
    image.data.resize(size-16);file.read(reinterpret_cast<char*>(image.data.data()),image.data.size());if(!file)throw std::runtime_error("Read failed");return image;
}
void write(const fs::path& path,const Image& image) {
    if(fs::exists(path))throw std::runtime_error("Refusing to overwrite prior evidence: "+path.string());
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(image.header.data()),16);file.write(reinterpret_cast<const char*>(image.data.data()),image.data.size());file.close();if(!file)throw std::runtime_error("Write failed");
}
Rgb unpack(unsigned word) {XMFLOAT3PK p(word);XMFLOAT3 f;XMStoreFloat3(&f,XMLoadFloat3PK(&p));return {f.x,f.y,f.z};}
unsigned pack(Rgb rgb) {XMFLOAT3PK p;XMStoreFloat3PK(&p,XMVectorSet(rgb[0],rgb[1],rgb[2],0));return p.v;}
float aces(float x) {return std::clamp((x*(2.51f*x+.03f))/(x*(2.43f*x+.59f)+.14f),0.f,1.f);}
float smooth(float low,float high,float x) {const float t=std::clamp((x-low)/(high-low),0.f,1.f);return t*t*(3-2*t);}
float exposure(const Image& image) {float value;std::memcpy(&value,image.data.data(),4);if(!std::isfinite(value)||value<=0||value>65504||value<1.f/65504)throw std::runtime_error("Unqualified exposure; no default substituted");return value;}
void validate(Rgb rgb) {for(float v:rgb)if(!std::isfinite(v)||v<0||v>65504)throw std::runtime_error("Invalid HDR input; refusing silent sanitization");}
Rgb prepare(Rgb rgb,float e) {validate(rgb);for(float& v:rgb)v=std::pow(aces(v*e),1.f/2.2f);return rgb;}
Rgb halfRound(Rgb rgb) {for(float& v:rgb)v=XMConvertHalfToFloat(XMConvertFloatToHalf(v));return rgb;}
float highlight(Rgb original,float e) {
    const float peak=std::max({original[0],original[1],original[2]})*e;
    const float shoulder=std::pow(aces(peak),1.f/2.2f);
    return smooth(1,8,peak)*smooth(.75f,.99f,shoulder);
}
Rgb restore(Rgb original,Rgb neural,float e,bool recovery,bool neutralCorrection=false) {
    const float mask=recovery?highlight(original,e):0;
    const Rgb proxy=neutralCorrection?halfRound(prepare(original,e)):Rgb{};
    for(unsigned c=0;c<3;++c) {
        if(!std::isfinite(neural[c])||neural[c]<0||neural[c]>1)throw std::runtime_error("Invalid SDR model output");
        const float tone=aces(original[c]*e),scalar=original[c]*e/std::max(tone,1e-6f);
        // AC7 adaptation: transfer only the edit relative to the actual quantized NR input.
        const float result=neutralCorrection
            ? original[c]+(std::pow(neural[c],2.2f)-std::pow(proxy[c],2.2f))*scalar/e
            : std::pow(neural[c],2.2f)*scalar/e;
        original[c]=std::clamp(result+(original[c]-result)*mask,0.f,65504.f);
    }
    return original;
}
Rgb halfAt(const Image& image,size_t i) {std::array<HALF,4> h{};std::memcpy(h.data(),image.data.data()+i*8,8);return {XMConvertHalfToFloat(h[0]),XMConvertHalfToFloat(h[1]),XMConvertHalfToFloat(h[2])};}
void putHalf(Image& image,size_t i,Rgb rgb) {std::array<HALF,4> h={XMConvertFloatToHalf(rgb[0]),XMConvertFloatToHalf(rgb[1]),XMConvertFloatToHalf(rgb[2]),XMConvertFloatToHalf(1)};std::memcpy(image.data.data()+i*8,h.data(),8);}
unsigned wordAt(const Image& image,size_t i) {unsigned word;std::memcpy(&word,image.data.data()+i*4,4);return word;}
void putWord(Image& image,size_t i,unsigned word) {std::memcpy(image.data.data()+i*4,&word,4);}
struct Stats {
    size_t changedPacked=0,changedFloat=0,bright=0,protectedPixels=0,fullProtected=0,fullProtectedChanged=0;
    double errorSum=0,maxAbsolute=0,maxRelative=0,maskSum=0,maxRgb=0;
    void add(Rgb original,Rgb restored,float mask) {
        changedPacked+=pack(original)!=pack(restored);bool changed=false;
        for(unsigned c=0;c<3;++c) {if(!std::isfinite(restored[c]))throw std::runtime_error("Nonfinite restored HDR");const double err=std::abs(double(restored[c])-original[c]);errorSum+=err;maxAbsolute=std::max(maxAbsolute,err);if(original[c]>0)maxRelative=std::max(maxRelative,err/original[c]);maxRgb=std::max(maxRgb,double(restored[c]));changed|=original[c]!=restored[c];}
        changedFloat+=changed;bright+=std::max({original[0],original[1],original[2]})>1;protectedPixels+=mask>0;fullProtected+=mask==1;fullProtectedChanged+=mask==1&&changed;maskSum+=mask;
    }
    void json(std::ostream& out) const {const double n=double(W)*H;out<<std::setprecision(17)<<"{\n  \"pixels\": "<<size_t(W)*H<<",\n  \"changedPackedPixels\": "<<changedPacked<<",\n  \"changedFloatPixels\": "<<changedFloat<<",\n  \"meanAbsoluteHdrDifference\": "<<errorSum/(n*3)<<",\n  \"maxAbsoluteHdrDifference\": "<<maxAbsolute<<",\n  \"maxRelativeChannelDifference\": "<<maxRelative<<",\n  \"maxRestoredRgb\": "<<maxRgb<<",\n  \"sourcePixelsAboveOne\": "<<bright<<",\n  \"highlightProtectedPixels\": "<<protectedPixels<<",\n  \"fullyProtectedPixels\": "<<fullProtected<<",\n  \"fullyProtectedChangedPixels\": "<<fullProtectedChanged<<",\n  \"meanHighlightMask\": "<<maskSum/n<<"\n}";}
};
void selfTest() {
    if(aces(0)!=0||std::abs(aces(1)-.8037974f)>1e-6f||pack(unpack(pack({.25f,8,166})))!=pack({.25f,8,166}))throw std::runtime_error("Curve/packing self-test failed");
    for(float e:{.01f,1.f,16.f})for(float x:{0.f,1e-8f,.00235f,.1f,1.f,8.f,166.f}) {
        const auto rgb=unpack(pack({x,x*.5f,x*.125f})),proxy=halfRound(prepare(rgb,e)),back=restore(rgb,proxy,e,true,true);
        if(back!=rgb)throw std::runtime_error("Neutral-corrected roundtrip must be bit-exact");
    }
    const Rgb dark={.0003f,.0003f,.0003f};
    if(restore(dark,halfRound(prepare(dark,.01f)),.01f,true)[0]>=dark[0]*.9f)throw std::runtime_error("Official epsilon-floor attenuation control not reproduced");
    const Rgb bright={166,8,3};if(restore(bright,{0,0,0},1,true)!=bright)throw std::runtime_error("Highlight preservation self-test failed");
    bool refused=false;try {prepare({-1,0,0},1);}catch(const std::runtime_error&){refused=true;}if(!refused)throw std::runtime_error("Invalid input accepted");
    std::cout<<"PASS exact neutral roundtrip, official dark-loss control, packing, highlight and invalid-input checks\n";
}
}
int wmain(int argc,wchar_t** argv) try {
    if(argc==2&&std::wstring(argv[1])==L"--self-test") {selfTest();return 0;}
    if(argc==5&&std::wstring(argv[1])==L"prepare") {
        const fs::path capture=argv[2],guides=argv[3],out=argv[4];if(fs::exists(out))throw std::runtime_error("Prepared directory must be new");
        const auto hdr=read(capture/L"color/scene-hdr.raw",{5120,H,26,4});
        const auto exp=read(capture/L"color/tonemap-exposure.raw",{1,1,16,8});const float e=exposure(exp);
        const auto depth=read(guides/L"depth.raw",{W,H,41,4}),motion=read(guides/L"motion.raw",{W,H,34,4});
        Image original{{W,H,26,4},std::vector<unsigned char>(size_t(W)*H*4)},proxy{{W,H,10,8},std::vector<unsigned char>(size_t(W)*H*8)},roundtrip=original;
        Stats stats,official;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
            const size_t i=size_t(y)*W+x;const unsigned word=wordAt(hdr,size_t(y)*5120+x+1280);const auto rgb=unpack(word);validate(rgb);putWord(original,i,word);
            if(pack(rgb)!=word)throw std::runtime_error("Packed HDR decoder roundtrip failed");
            const auto sdr=halfRound(prepare(rgb,e));putHalf(proxy,i,sdr);
            const auto back=restore(rgb,sdr,e,true,true);putWord(roundtrip,i,pack(back));stats.add(rgb,back,highlight(rgb,e));official.add(rgb,restore(rgb,sdr,e,true),highlight(rgb,e));
        }
        fs::create_directories(out);write(out/L"original-hdr.raw",original);write(out/L"color.raw",proxy);write(out/L"depth.raw",depth);write(out/L"motion.raw",motion);write(out/L"exposure.raw",exp);write(out/L"hdr-roundtrip.raw",roundtrip);
        std::ofstream report(out/L"roundtrip-checks.json");report<<"{\n\"official\": ";official.json(report);report<<",\n\"neutralCorrected\": ";stats.json(report);report<<"\n}\n";report.close();if(!report)throw std::runtime_error("Report write failed");official.json(std::cout);std::cout<<"\nNeutral-corrected:\n";stats.json(std::cout);std::cout<<"\nExposure="<<std::setprecision(17)<<e<<"\n";
        if(stats.changedPacked)throw std::runtime_error("Off roundtrip is not bit-exact in original packed format");
        std::cout<<"PASS NR-off prepared FP16 roundtrip is bit-exact after R11G11B10 packing\n";return 0;
    }
    if(argc==4&&std::wstring(argv[1])==L"restore") {
        const fs::path frame=argv[2],eval=argv[3];if(fs::exists(eval/L"hdr-restored.raw"))throw std::runtime_error("Refusing to overwrite HDR result");
        const auto original=read(frame/L"original-hdr.raw",{W,H,26,4}),neural=read(eval/L"nr-output.raw",{W,H,10,8});const float e=exposure(read(frame/L"exposure.raw",{1,1,16,8}));
        Image result{{W,H,10,8},std::vector<unsigned char>(size_t(W)*H*8)};Stats recovered,official,withoutRecovery;
        for(size_t i=0;i<size_t(W)*H;++i) {const auto rgb=unpack(wordAt(original,i)),n=halfAt(neural,i),back=restore(rgb,n,e,true,true);putHalf(result,i,back);recovered.add(rgb,halfRound(back),highlight(rgb,e));official.add(rgb,halfRound(restore(rgb,n,e,true)),highlight(rgb,e));withoutRecovery.add(rgb,halfRound(restore(rgb,n,e,false,true)),0);}
        if(recovered.fullProtectedChanged)throw std::runtime_error("Fully protected highlights changed");
        write(eval/L"hdr-restored.raw",result);std::ofstream report(eval/L"hdr-checks.json");report<<"{\n\"neutralCorrectedWithRecovery\": ";recovered.json(report);report<<",\n\"officialWithRecovery\": ";official.json(report);report<<",\n\"neutralCorrectedWithoutRecovery\": ";withoutRecovery.json(report);report<<"\n}\n";report.close();if(!report)throw std::runtime_error("Report write failed");recovered.json(std::cout);std::cout<<"\nPASS finite HDR restoration; quality and temporal behavior unqualified\n";return 0;
    }
    throw std::runtime_error("Usage: hdr-reference --self-test | prepare CAPTURE_DIR EXISTING_GUIDES_DIR NEW_FRAME_DIR | restore FRAME_DIR EVALUATION_DIR");
} catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
