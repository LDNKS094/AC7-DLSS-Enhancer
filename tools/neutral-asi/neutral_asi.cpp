// Fixed neutral ASI. DllMain does no D3D work. Process-pinned hooks, no hot unload.
#include "interface.h"
#include "neutral_entry.h"
#include "guide_candidates.h"
#include "view_identity.h"
#include <MinHook.h>
#include <bcrypt.h>
#include <memory>
#include <mutex>
#include <array>
#include <type_traits>
#include <filesystem>
#include <cstdio>
#include <string>
using Microsoft::WRL::ComPtr;
namespace {
using CreatePS=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using DrawIndexed=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using Map=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
using Unmap=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);
using Update=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
using Update1=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT,UINT);
using Copy=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
using CopyRegion=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
using CopyRegion1=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext1*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*,UINT);
using Execute=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);
std::array<CreatePS,2> originalPS{};std::array<DrawIndexed,2> originalDraw{};
std::array<Map,2> originalMap{};std::array<Unmap,2> originalUnmap{};std::array<Update,2> originalUpdate{};std::array<Update1,2> originalUpdate1{};
std::array<Copy,2> originalCopy{};std::array<CopyRegion,2> originalCopyRegion{};std::array<CopyRegion1,2> originalCopyRegion1{};std::array<Execute,2> originalExecute{};
std::mutex lock;AC7NeutralStatus state;
ac7::NeutralEntry* entry=nullptr; // Process-pinned owner; no GPU teardown under loader lock.
ComPtr<ID3D11Device> ownerDevice;ComPtr<ID3D11DeviceContext> ownerContext;ComPtr<ID3D11DeviceContext1> ownerContext1;ComPtr<ID3D11PixelShader>guideShader;
ac7::ViewUploads uploads;ac7::ViewFrames frames;ac7::GuideCandidates guides;
ac7::ViewIdentity guideIdentity;bool guideEpoch=false,guideConflict=false;
struct Mapping{ComPtr<ID3D11Buffer>buffer;const void*data;};std::vector<Mapping> mappings;
uint64_t token=0;bool hasToken=false;
thread_local unsigned psDepth=0,drawDepth=0,mapDepth=0,unmapDepth=0,updateDepth=0,copyDepth=0,executeDepth=0,neutralDepth=0;
struct Nest {unsigned& value;explicit Nest(unsigned&v):value(v){++value;}~Nest(){--value;}};
struct Coverage {uint64_t ownerDraws=0,otherDraws=0,maps=0,ownerMaps=0,updates=0,ownerUpdates=0,executions=0,mapEvictions=0;};Coverage coverage;
struct UploadDiagnostic {ComPtr<ID3D11Buffer> buffer;const char*reason="unseen";std::array<float,6> rect{};uint32_t rawCounter=0;};
std::array<UploadDiagnostic,8> recentUploads;unsigned nextUpload=0,noteCount=0;uint64_t noteMask=0;
HMODULE knownD3D11=nullptr,knownDXGI=nullptr,knownDwmapi=nullptr;
enum class Note:unsigned {ownerGuide,ownerPrewarp,foreignShape,foreignGuide,foreignPrewarp,guideRange,warpRange,guideViewport,warpViewport,guideCache,warpCache,guideConflict,guideLayout,missingEpoch,epochConflict,packetMismatch,ambiguousGuide,depthViewMismatch,frameRejected,gpuInvalidation,unknownCommands,wideObservation};
HANDLE traceFile=INVALID_HANDLE_VALUE;unsigned traceLines=0;
bool writeTrace(const char*line,int n,size_t capacity){
    if(traceFile==INVALID_HANDLE_VALUE)return false;if(traceLines==64)return true;DWORD written=0;const bool ok=n>0&&size_t(n)<capacity&&WriteFile(traceFile,line,DWORD(n),&written,nullptr)&&written==DWORD(n);if(ok)traceLines++;return ok;
}
bool trace(const char*event){
    char line[1024];const int n=std::snprintf(line,sizeof(line),"event=%s ms=%llu installed=%u enabled=%u stopped=%u drained=%u error=%u prewarp=%llu guides=%llu uploads=%llu observed=%llu counter=%u identity=%llu applied=%llu refused=%llu pending=%llu original=%llu ownerDraws=%llu otherDraws=%llu maps=%llu ownerMaps=%llu updates=%llu ownerUpdates=%llu executions=%llu cacheEntries=%zu cacheEvictions=%llu mapped=%zu mapEvictions=%llu\r\n",event,GetTickCount64(),state.installed,state.enabled,state.stopped,state.drained,state.error,state.shaderMatches,state.guideShaderMatches,state.viewUploads,state.observedFrames,state.lastViewCounter,state.lastViewToken,state.applied,state.viewRefused,state.pending,state.originalDrawCalls,coverage.ownerDraws,coverage.otherDraws,coverage.maps,coverage.ownerMaps,coverage.updates,coverage.ownerUpdates,coverage.executions,uploads.size(),uploads.evictions(),mappings.size(),coverage.mapEvictions);return writeTrace(line,n,sizeof(line));
}
void fail(unsigned code){state.error=state.error?state.error:code;state.enabled=0;state.stopped=1;if(entry)entry->stop();trace("error");}
bool startTrace(){
    HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&startTrace),&module))return false;wchar_t path[32768];const DWORD n=GetModuleFileNameW(module,path,32768);if(!n||n==32768)return false;const auto folder=std::filesystem::path(path).parent_path();const auto flags=GetFileAttributesW((folder/L"AC7NativeNeutral.enable").c_str());state.enabled=flags!=INVALID_FILE_ATTRIBUTES&&!(flags&FILE_ATTRIBUTE_DIRECTORY);
    const auto name=L"AC7NativeNeutral-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log";traceFile=CreateFileW((folder/name).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);return trace("bootstrap");
}
void note(Note key,const char*reason,ID3D11DeviceContext*c,unsigned role){
    const uint64_t bit=uint64_t(1)<<unsigned(key);if((noteMask&bit)||noteCount==32||!c)return;noteMask|=bit;noteCount++;
    ComPtr<ID3D11Device>d;c->GetDevice(&d);ComPtr<IUnknown>identity,ownerIdentity;c->QueryInterface(IID_PPV_ARGS(&identity));ownerContext.As(&ownerIdentity);
    ComPtr<ID3D11DeviceContext1>c1;ComPtr<ID3D11Buffer>b;UINT first=0,count=0;D3D11_VIEWPORT v{};UINT nv=0;const auto type=c->GetType();if(type==D3D11_DEVICE_CONTEXT_IMMEDIATE){if(SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c1))))c1->PSGetConstantBuffers1(1,1,&b,&first,&count);else c->PSGetConstantBuffers(1,1,&b);nv=1;c->RSGetViewports(&nv,&v);}
    D3D11_BUFFER_DESC desc{};if(b)b->GetDesc(&desc);
    const UploadDiagnostic*seen=nullptr;for(const auto&u:recentUploads)if(u.buffer&&u.buffer.Get()==b.Get()){seen=&u;break;}
    MEMORY_BASIC_INFORMATION memory{};const auto address=(*reinterpret_cast<void***>(c))[12];VirtualQuery(address,&memory,sizeof(memory));const char*name=memory.AllocationBase&&memory.AllocationBase==knownD3D11?"d3d11.dll":memory.AllocationBase&&memory.AllocationBase==knownDXGI?"dxgi.dll":memory.AllocationBase&&memory.AllocationBase==knownDwmapi?"dwmapi.dll":"other";
    const std::array<float,6> empty{};const auto&r=seen?seen->rect:empty;char line[1024];const int n=std::snprintf(line,sizeof(line),"event=diagnostic reason=%s ms=%llu role=%u module=%s ownerContext=%u deviceEqual=%u identityEqual=%u contextType=%u cbBytes=%u cbBind=%u cbFirst=%u cbCount=%u viewportCount=%u viewport=%.6g,%.6g,%.6g,%.6g cbSeen=%u decode=%s uploadRect=%.6g,%.6g,%.6g,%.6g,%.6g,%.6g rawCounter=%u ownerDraws=%llu otherDraws=%llu maps=%llu ownerMaps=%llu updates=%llu ownerUpdates=%llu uploads=%llu observed=%llu enabled=%u applied=%llu\r\n",reason,GetTickCount64(),role,name,unsigned(c==ownerContext.Get()),unsigned(d.Get()==ownerDevice.Get()),unsigned(identity&&identity.Get()==ownerIdentity.Get()),unsigned(c->GetType()),desc.ByteWidth,desc.BindFlags,first,count,nv,v.TopLeftX,v.TopLeftY,v.Width,v.Height,unsigned(seen!=nullptr),seen?seen->reason:"unseen",r[0],r[1],r[2],r[3],r[4],r[5],seen?seen->rawCounter:0,coverage.ownerDraws,coverage.otherDraws,coverage.maps,coverage.ownerMaps,coverage.updates,coverage.ownerUpdates,state.viewUploads,state.observedFrames,state.enabled,state.applied);if(!writeTrace(line,n,sizeof(line)))fail(12);
}
bool owned(ID3D11DeviceContext*c){return !state.stopped&&state.viewMode&&c==ownerContext.Get();}
ComPtr<ID3D11Buffer>viewBuffer(ID3D11Resource*r){ComPtr<ID3D11Buffer>b;if(r&&SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&b)))){D3D11_BUFFER_DESC d{};b->GetDesc(&d);if(d.ByteWidth==4096&&d.BindFlags==D3D11_BIND_CONSTANT_BUFFER)return b;}return {};}
void invalidate(ID3D11Resource*r){if(auto b=viewBuffer(r)){uploads.invalidate(b.Get());note(Note::gpuInvalidation,"gpu-copy-invalidates",ownerContext.Get(),0);}}
void recordUpload(ID3D11Buffer*b,const void*data){
    const char*reason=nullptr;uploads.observe(b,data,4096,&reason);state.viewUploads++;UploadDiagnostic*record=nullptr;for(auto&u:recentUploads)if(u.buffer.Get()==b){record=&u;break;}if(!record)record=&recentUploads[nextUpload++%recentUploads.size()];record->buffer=b;record->reason=reason;const auto*bytes=static_cast<const unsigned char*>(data);const unsigned offsets[]={126*16,126*16+4,127*16,127*16+4,128*16,128*16+4};for(unsigned i=0;i<6;i++)std::memcpy(&record->rect[i],bytes+offsets[i],4);std::memcpy(&record->rawCounter,bytes+137*16+4,4);
}
void observe(ID3D11Resource*r,UINT sub,const D3D11_BOX*box,const void*data){if(auto b=viewBuffer(r)){if(sub||box||!data)uploads.invalidate(b.Get());else recordUpload(b.Get(),data);}}
bool guideCode(const void*data,SIZE_T size){
    if(size!=4240||!data)return false;std::array<unsigned char,32>hash{};if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),ULONG(size),hash.data(),ULONG(hash.size()))<0)throw std::runtime_error("Guide shader hash failed");
    constexpr char expected[]="85ec4635ea1fee2851d5979da08a1013609ead56312747cc4f54ba2e80620af0";constexpr char hex[]="0123456789abcdef";for(size_t i=0;i<hash.size();i++)if(hex[hash[i]>>4]!=expected[i*2]||hex[hash[i]&15]!=expected[i*2+1])return false;return true;
}
ComPtr<ID3D11Texture2D>psTexture(ID3D11DeviceContext*c,UINT slot){ComPtr<ID3D11ShaderResourceView>v;c->PSGetShaderResources(slot,1,&v);if(!v)return {};ComPtr<ID3D11Resource>r;v->GetResource(&r);ComPtr<ID3D11Texture2D>t;r.As(&t);return t;}
bool layout(ID3D11Texture2D*t,DXGI_FORMAT format){if(!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);return d.Width==5120&&d.Height==1440&&d.Format==format&&d.MipLevels==1&&d.ArraySize==1&&d.SampleDesc.Count==1;}
const ac7::ViewIdentity*boundView(ComPtr<ID3D11Buffer>&b,D3D11_VIEWPORT&v,unsigned role){
    UINT first=0,count=0;ownerContext1->PSGetConstantBuffers1(1,1,&b,&first,&count);UINT n=1;ownerContext->RSGetViewports(&n,&v);
    const D3D11_VIEWPORT central{1280,0,2560,1440,0,1},full{0,0,5120,1440,0,1};if(first||count<256){note(role==1?Note::guideRange:Note::warpRange,"cb-range",ownerContext.Get(),role);return nullptr;}if(n!=1||(!ac7::sameViewport(v,central)&&!ac7::sameViewport(v,full))){note(role==1?Note::guideViewport:Note::warpViewport,"viewport",ownerContext.Get(),role);return nullptr;}const auto*id=uploads.get(b.Get());if(!id){note(role==1?Note::guideCache:Note::warpCache,"view-cache-miss",ownerContext.Get(),role);return nullptr;}if(v.TopLeftX!=id->rect[0]||v.TopLeftY!=id->rect[1]||v.Width!=id->rect[2]||v.Height!=id->rect[3]){note(role==1?Note::guideViewport:Note::warpViewport,"view-viewport-mismatch",ownerContext.Get(),role);return nullptr;}return id;
}
void collectGuide(){
    ComPtr<ID3D11Buffer>b;D3D11_VIEWPORT v{};const auto*id=boundView(b,v,1);if(!id){state.viewRefused++;guideConflict=true;return;}
    if(!guideEpoch||id->counter!=guideIdentity.counter){guides.clear();guideIdentity=*id;guideEpoch=true;guideConflict=false;}
    if(!(*id==guideIdentity)){note(Note::guideConflict,"guide-view-conflict",ownerContext.Get(),1);guideConflict=true;state.viewRefused++;return;}
    auto d=psTexture(ownerContext.Get(),0),m=psTexture(ownerContext.Get(),3);
    if(!layout(d.Get(),DXGI_FORMAT_R32G8X24_TYPELESS)||!layout(m.Get(),DXGI_FORMAT_R16G16_UNORM)){note(Note::guideLayout,"guide-texture-layout",ownerContext.Get(),1);guideConflict=true;state.viewRefused++;return;}
    guides.add({d,m,b,v});state.guideCandidates++;
}
void observedDraw(){
    ComPtr<ID3D11PixelShader>ps;ownerContext->PSGetShader(&ps,nullptr,nullptr);
    if(ps.Get()==guideShader.Get()&&guideShader){note(Note::ownerGuide,"owned-guide",ownerContext.Get(),1);collectGuide();return;}
    if(!entry||!entry->isShader(ps.Get()))return;note(Note::ownerPrewarp,"owned-prewarp",ownerContext.Get(),2);
    ComPtr<ID3D11Buffer>b;D3D11_VIEWPORT v{};const auto*id=boundView(b,v,2);if(!id){state.viewRefused++;return;}if(!guideEpoch){note(Note::missingEpoch,"missing-guide-epoch",ownerContext.Get(),2);state.viewRefused++;return;}if(guideConflict){note(Note::epochConflict,"guide-epoch-conflict",ownerContext.Get(),2);state.viewRefused++;return;}if(!(*id==guideIdentity)){note(Note::packetMismatch,"guide-view-bytes",ownerContext.Get(),2);state.viewRefused++;return;}
    ComPtr<ID3D11DepthStencilView>ds;ownerContext->OMGetRenderTargets(0,nullptr,&ds);ComPtr<ID3D11Resource>depth;if(ds)ds->GetResource(&depth);
    const ac7::GuideCandidate*candidate=nullptr;try{candidate=guides.find(depth.Get(),b.Get(),v);}catch(const std::runtime_error&){note(Note::ambiguousGuide,"ambiguous-guides",ownerContext.Get(),2);state.viewRefused++;return;}
    uint64_t realToken=0;if(!candidate){note(Note::depthViewMismatch,"depth-view-association",ownerContext.Get(),2);state.viewRefused++;return;}if(!frames.proposed(*id,realToken)){note(Note::frameRejected,"frame-clock-counter",ownerContext.Get(),2);state.viewRefused++;return;}
    if(id->rect[2]==5120){note(Note::wideObservation,"full-view-observe-only",ownerContext.Get(),2);if(state.enabled){fail(13);return;}}
    if(state.enabled){
        entry->enable(true);Nest commands(neutralDepth);const auto result=entry->beforeDraw(ownerContext.Get(),realToken);
        if(result==ac7::NeutralEntry::Result::applied){state.applied++;state.drained=0;}
        else if(result==ac7::NeutralEntry::Result::pending)state.pending++;
        else if(result==ac7::NeutralEntry::Result::refused){state.refused++;return;}
        else return;
    }
    frames.commit(*id,realToken);state.observedFrames++;state.lastViewCounter=id->counter;state.lastViewToken=realToken;if(!trace("view")){fail(12);return;}
    // One bounded neutral/observation trial; no persistent game toggle or fabricated Present clock.
    if(state.observedFrames==8){state.enabled=0;state.stopped=1;entry->stop();if(!trace("bounded-stop"))fail(12);}
}
template<unsigned I>HRESULT STDMETHODCALLTYPE createPS(ID3D11Device*d,const void*data,SIZE_T size,ID3D11ClassLinkage*classes,ID3D11PixelShader**result){
    Nest nested(psDepth);const HRESULT hr=originalPS[I](d,data,size,classes,result);if(psDepth!=1||FAILED(hr)||!result||!*result||(size!=628&&size!=4240))return hr;
    try{
        const bool prewarp=ac7::NeutralEntry::matchesBytecode(data,size),guide=guideCode(data,size);if(!prewarp&&!guide)return hr;
        std::lock_guard<std::mutex>guard(lock);if(state.stopped)return hr;ComPtr<ID3D11Device>device;(*result)->GetDevice(&device);
        if(ownerDevice&&ownerDevice.Get()!=device.Get()){fail(5);return hr;}
        if(!ownerDevice){ownerDevice=device;device->GetImmediateContext(&ownerContext);if(FAILED(ownerContext.As(&ownerContext1))){fail(6);return hr;}}
        if(guide){if(guideShader){fail(5);return hr;}guideShader=*result;state.guideShaderMatches++;if(!trace("guide-shader"))fail(12);return hr;}
        if(entry){fail(5);return hr;}auto candidate=std::make_unique<ac7::NeutralEntry>(device.Get());if(!candidate->registerShader(*result,data,size)){fail(6);return hr;}entry=candidate.release();state.shaderMatches++;if(!trace("prewarp-shader"))fail(12);
    }catch(...){std::lock_guard<std::mutex>guard(lock);fail(7);}return hr;
}
template<unsigned I>void STDMETHODCALLTYPE drawIndexed(ID3D11DeviceContext*c,UINT indices,UINT start,INT base){
    Nest nested(drawDepth);if(drawDepth==1)try{
        std::lock_guard<std::mutex>guard(lock);state.drawIntercepts++;if(c==ownerContext.Get())coverage.ownerDraws++;else coverage.otherDraws++;
        if(!state.stopped&&state.viewMode&&ownerContext&&c!=ownerContext.Get()&&indices==3&&start==6&&base==0){
            const uint64_t both=(uint64_t(1)<<unsigned(Note::foreignGuide))|(uint64_t(1)<<unsigned(Note::foreignPrewarp));if((noteMask&both)!=both){if(c->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED)note(Note::foreignShape,"foreign-deferred-shape",c,0);else{ComPtr<ID3D11PixelShader>ps;c->PSGetShader(&ps,nullptr,nullptr);if(guideShader&&ps.Get()==guideShader.Get())note(Note::foreignGuide,"foreign-guide",c,1);else if(entry&&entry->isShader(ps.Get()))note(Note::foreignPrewarp,"foreign-prewarp",c,2);else note(Note::foreignShape,"foreign-shape",c,0);}}
        }
        if(entry&&state.stopped&&!state.drained){state.drained=entry->drained();if(state.drained&&!trace("drained"))fail(12);}
        if(!state.stopped&&c==ownerContext.Get()&&indices==3&&start==6&&base==0){
            if(state.viewMode)observedDraw();
            else if(entry&&state.enabled&&hasToken){entry->enable(true);Nest commands(neutralDepth);const auto r=entry->beforeDraw(c,token);if(r==ac7::NeutralEntry::Result::applied)state.applied++;else if(r==ac7::NeutralEntry::Result::pending)state.pending++;else if(r==ac7::NeutralEntry::Result::refused)state.refused++;}
        }
    }catch(...){std::lock_guard<std::mutex>guard(lock);fail(8);}
    originalDraw[I](c,indices,start,base); // Unconditional, including refusal/stop/exception/nesting.
    if(drawDepth==1)try{std::lock_guard<std::mutex>guard(lock);state.originalDrawCalls++;}catch(...){}
}
template<unsigned I>HRESULT STDMETHODCALLTYPE map(ID3D11DeviceContext*c,ID3D11Resource*r,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE*out){
    Nest nested(mapDepth);const auto hr=originalMap[I](c,r,sub,type,flags,out);if(mapDepth==1&&!neutralDepth&&SUCCEEDED(hr))try{std::lock_guard<std::mutex>guard(lock);coverage.maps++;if(c==ownerContext.Get())coverage.ownerMaps++;if(owned(c))if(auto b=viewBuffer(r)){uploads.invalidate(b.Get());if(!sub&&type!=D3D11_MAP_READ&&out&&out->pData){if(mappings.size()==16){uploads.invalidate(mappings.front().buffer.Get());mappings.erase(mappings.begin());coverage.mapEvictions++;if(coverage.mapEvictions==1&&!trace("map-tracking-evicted"))fail(12);}mappings.push_back({b,out->pData});}}}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}return hr;
}
template<unsigned I>void STDMETHODCALLTYPE unmap(ID3D11DeviceContext*c,ID3D11Resource*r,UINT sub){
    Nest nested(unmapDepth);if(unmapDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);if(owned(c))if(auto b=viewBuffer(r)){uploads.invalidate(b.Get());for(auto it=mappings.begin();it!=mappings.end();++it)if(it->buffer.Get()==b.Get()){if(!sub){recordUpload(b.Get(),it->data);}mappings.erase(it);break;}}}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalUnmap[I](c,r,sub);
}
template<unsigned I>void STDMETHODCALLTYPE update(ID3D11DeviceContext*c,ID3D11Resource*r,UINT sub,const D3D11_BOX*box,const void*data,UINT row,UINT depth){Nest nested(updateDepth);if(updateDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);coverage.updates++;if(c==ownerContext.Get())coverage.ownerUpdates++;if(owned(c))observe(r,sub,box,data);}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalUpdate[I](c,r,sub,box,data,row,depth);}
template<unsigned I>void STDMETHODCALLTYPE update1(ID3D11DeviceContext1*c,ID3D11Resource*r,UINT sub,const D3D11_BOX*box,const void*data,UINT row,UINT depth,UINT flags){Nest nested(updateDepth);if(updateDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);coverage.updates++;if(c==ownerContext.Get())coverage.ownerUpdates++;if(owned(c))observe(r,sub,box,data);}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalUpdate1[I](c,r,sub,box,data,row,depth,flags);}
template<unsigned I>void STDMETHODCALLTYPE copy(ID3D11DeviceContext*c,ID3D11Resource*dst,ID3D11Resource*src){Nest nested(copyDepth);if(copyDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);if(owned(c))invalidate(dst);}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalCopy[I](c,dst,src);}
template<unsigned I>void STDMETHODCALLTYPE copyRegion(ID3D11DeviceContext*c,ID3D11Resource*dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource*src,UINT source,const D3D11_BOX*box){Nest nested(copyDepth);if(copyDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);if(owned(c))invalidate(dst);}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalCopyRegion[I](c,dst,sub,x,y,z,src,source,box);}
template<unsigned I>void STDMETHODCALLTYPE copyRegion1(ID3D11DeviceContext1*c,ID3D11Resource*dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource*src,UINT source,const D3D11_BOX*box,UINT flags){Nest nested(copyDepth);if(copyDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);if(owned(c))invalidate(dst);}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalCopyRegion1[I](c,dst,sub,x,y,z,src,source,box,flags);}
template<unsigned I>void STDMETHODCALLTYPE execute(ID3D11DeviceContext*c,ID3D11CommandList*commands,BOOL restore){Nest nested(executeDepth);if(executeDepth==1&&!neutralDepth)try{std::lock_guard<std::mutex>guard(lock);if(owned(c)){coverage.executions++;note(Note::unknownCommands,"unknown-command-list",c,0);uploads.clear();guideEpoch=false;}}catch(...){std::lock_guard<std::mutex>guard(lock);fail(10);}originalExecute[I](c,commands,restore);}
DWORD WINAPI bootstrap(void*){
    {std::lock_guard<std::mutex>guard(lock);if(state.stopped)return 0;if(!startTrace()){fail(12);return 0;}}
    std::array<ComPtr<ID3D11Device>,2>devices;std::array<ComPtr<ID3D11DeviceContext>,2>contexts;std::array<ComPtr<ID3D11DeviceContext1>,2>contexts1;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    for(unsigned i=0;i<2;i++){const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,i?D3D11_CREATE_DEVICE_DEBUG:0,&level,1,D3D11_SDK_VERSION,&devices[i],nullptr,&contexts[i]);if(FAILED(hr)&&!i){std::lock_guard<std::mutex>guard(lock);fail(1);return 0;}if(contexts[i]&&FAILED(contexts[i].As(&contexts1[i]))){std::lock_guard<std::mutex>guard(lock);fail(1);return 0;}}
    knownD3D11=GetModuleHandleW(L"d3d11.dll");knownDXGI=GetModuleHandleW(L"dxgi.dll");knownDwmapi=GetModuleHandleW(L"dwmapi.dll");
    std::lock_guard<std::mutex>guard(lock);if(state.stopped)return 0;const auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){fail(2);return 0;}
    std::array<void*,20>targets{};unsigned count=0;
    const auto hook=[&](void*target,void*wrapper,void**original){for(unsigned j=0;j<count;j++)if(targets[j]==target)return true;if(MH_CreateHook(target,wrapper,original)!=MH_OK)return false;targets[count++]=target;return true;};
#define HOOK(method,index,original,wrapper) hook((*reinterpret_cast<void***>(method))[index],reinterpret_cast<void*>(&wrapper<i>),reinterpret_cast<void**>(&original[i]))
    // Public SDK COM method slots, discovered separately for ordinary/debug wrappers.
    for(unsigned k=0;k<2;k++)if(devices[k]){
        const auto install=[&](auto index){constexpr unsigned i=decltype(index)::value;return HOOK(devices[i].Get(),15,originalPS,createPS)&&HOOK(contexts[i].Get(),12,originalDraw,drawIndexed)&&HOOK(contexts[i].Get(),14,originalMap,map)&&HOOK(contexts[i].Get(),15,originalUnmap,unmap)&&HOOK(contexts[i].Get(),48,originalUpdate,update)&&HOOK(contexts[i].Get(),47,originalCopy,copy)&&HOOK(contexts[i].Get(),46,originalCopyRegion,copyRegion)&&HOOK(contexts[i].Get(),58,originalExecute,execute)&&HOOK(contexts1[i].Get(),115,originalCopyRegion1,copyRegion1)&&HOOK(contexts1[i].Get(),116,originalUpdate1,update1);};
        if(!(k?install(std::integral_constant<unsigned,1>{}):install(std::integral_constant<unsigned,0>{}))){for(unsigned j=0;j<count;j++)MH_RemoveHook(targets[j]);fail(3);return 0;}
    }
#undef HOOK
    HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&bootstrap),&pinned)){for(unsigned j=0;j<count;j++)MH_RemoveHook(targets[j]);fail(4);return 0;}
    for(unsigned j=0;j<count;j++)if(MH_QueueEnableHook(targets[j])!=MH_OK){fail(4);return 0;}if(MH_ApplyQueued()!=MH_OK){fail(4);return 0;}state.installed=1;if(!trace("installed"))fail(12);return 0;
}
}
extern "C" __declspec(dllexport) void WINAPI InitializeASI(){std::lock_guard<std::mutex>guard(lock);if(state.bootstrap||state.stopped)return;state.bootstrap=1;HANDLE thread=CreateThread(nullptr,0,bootstrap,nullptr,0,nullptr);if(thread)CloseHandle(thread);else fail(1);}
extern "C" __declspec(dllexport) BOOL WINAPI AC7NeutralSetFrame(BOOL enabled,uint64_t frame){try{std::lock_guard<std::mutex>guard(lock);if(!state.installed||state.stopped||state.error||!entry||state.observedFrames)return FALSE;if(hasToken&&frame<token)return FALSE;state.viewMode=0;hasToken=true;token=frame;state.enabled=enabled?1:0;entry->enable(enabled!=FALSE);return TRUE;}catch(...){return FALSE;}}
extern "C" __declspec(dllexport) BOOL WINAPI AC7NeutralUseView(BOOL enabled){try{std::lock_guard<std::mutex>guard(lock);if(!state.installed||state.stopped||state.error||!entry||hasToken)return FALSE;state.viewMode=1;state.enabled=enabled?1:0;entry->enable(enabled!=FALSE);return TRUE;}catch(...){return FALSE;}}
extern "C" __declspec(dllexport) BOOL WINAPI AC7NeutralStop(){try{std::lock_guard<std::mutex>guard(lock);state.enabled=0;state.stopped=1;if(entry)entry->stop();return TRUE;}catch(...){return FALSE;}}
extern "C" __declspec(dllexport) BOOL WINAPI AC7NeutralGetStatus(AC7NeutralStatus*out){if(!out||out->size!=sizeof(AC7NeutralStatus)||out->version!=2)return FALSE;try{std::lock_guard<std::mutex>guard(lock);const auto wasDrained=state.drained;state.drained=entry?entry->drained():1;if(state.stopped&&!wasDrained&&state.drained&&!trace("drained"))fail(12);*out=state;return TRUE;}catch(...){std::lock_guard<std::mutex>guard(lock);fail(9);state.drained=0;*out=state;return TRUE;}}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
