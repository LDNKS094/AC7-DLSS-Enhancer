#include "dxgi_hooks.h"
#include "swap_chain_bridge.h"
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ac7 {
namespace {
using Microsoft::WRL::ComPtr;
using Factory=HRESULT(WINAPI*)(REFIID,void**);
using Swap=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using SwapHwnd=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
struct State {
    std::mutex lock;
    Factory factory0=nullptr,factory1=nullptr;
    std::atomic<Swap>swap{nullptr};
    std::atomic<SwapHwnd>swapHwnd{nullptr};
    void*swapTarget=nullptr;void*hwndTarget=nullptr;
    std::vector<ComPtr<IUnknown>>factories;
    void(*failure)(const char*)noexcept=nullptr;
};
State&state=*new State;
[[noreturn]] void fatal(const char*reason)noexcept{
    if(state.failure)state.failure(reason);
    OutputDebugStringA(reason);RaiseFailFastException(nullptr,nullptr,0);std::terminate();
}
void check(HRESULT result,const char*stage){if(FAILED(result))throw std::runtime_error(std::string(stage)+" HRESULT "+std::to_string(unsigned(result)));}
void hookCheck(MH_STATUS result,const char*stage){if(result!=MH_OK)throw std::runtime_error(std::string(stage)+": "+MH_StatusToString(result));}
void*method(IUnknown*object,unsigned slot){return (*reinterpret_cast<void***>(object))[slot];}
bool gameFactory(IUnknown*object){
    ComPtr<IUnknown>identity;check(object->QueryInterface(IID_PPV_ARGS(&identity)),"DXGI factory identity");
    std::lock_guard<std::mutex>guard(state.lock);
    return std::any_of(state.factories.begin(),state.factories.end(),[&](const auto&f){return f.Get()==identity.Get();});
}
ComPtr<ID3D11Device>sourceDevice(IUnknown*factory,IUnknown*source,HWND window){
    if(!source||!window||!gameFactory(factory))return {};
    DWORD process=0;
    if(!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId())return {};
    ComPtr<ID3D11Device>device;
    const auto result=source->QueryInterface(IID_PPV_ARGS(&device));
    if(result==E_NOINTERFACE)return {};
    check(result,"DXGI swap-chain D3D11 device");
    return device;
}
struct SourceWindow {
    HWND value=createPresentationWindow();
    ~SourceWindow(){closePresentationWindow(value);}
};
void extent(HWND window,UINT&width,UINT&height){
    if(width&&height)return;
    RECT client{};if(!GetClientRect(window,&client))throw std::runtime_error("Read source window extent failed");
    if(!width)width=UINT(std::max<LONG>(1,client.right-client.left));
    if(!height)height=UINT(std::max<LONG>(1,client.bottom-client.top));
}
HRESULT STDMETHODCALLTYPE swap(IDXGIFactory*self,IUnknown*device,DXGI_SWAP_CHAIN_DESC*desc,IDXGISwapChain**out)noexcept{
    try{
        const auto call=state.swap.load(std::memory_order_acquire);
        if(!call)throw std::runtime_error("DXGI CreateSwapChain original was not published");
        if(!desc||!out)return call(self,device,desc,out);
        const auto source=sourceDevice(self,device,desc->OutputWindow);
        if(!source)return call(self,device,desc,out);
        *out=nullptr;SourceWindow hidden;
        auto local=*desc;extent(desc->OutputWindow,local.BufferDesc.Width,local.BufferDesc.Height);
        local.OutputWindow=hidden.value;local.Windowed=TRUE;
        ComPtr<IDXGISwapChain>native;const auto result=call(self,device,&local,&native);
        if(FAILED(result))return result;
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen{};fullscreen.RefreshRate=desc->BufferDesc.RefreshRate;
        fullscreen.ScanlineOrdering=desc->BufferDesc.ScanlineOrdering;fullscreen.Scaling=desc->BufferDesc.Scaling;fullscreen.Windowed=desc->Windowed;
        *out=createPresentationBridge(self,source.Get(),desc->OutputWindow,native.Get(),fullscreen,nullptr,state.failure);
        hidden.value=nullptr;
        return result;
    }catch(const std::exception&e){fatal(e.what());}catch(...){fatal("DXGI CreateSwapChain entry failed");}
}
HRESULT STDMETHODCALLTYPE swapHwnd(IDXGIFactory2*self,IUnknown*device,HWND window,const DXGI_SWAP_CHAIN_DESC1*desc,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*fullscreen,IDXGIOutput*restrictOutput,IDXGISwapChain1**out)noexcept{
    try{
        const auto call=state.swapHwnd.load(std::memory_order_acquire);
        if(!call)throw std::runtime_error("DXGI CreateSwapChainForHwnd original was not published");
        if(!desc||!out)return call(self,device,window,desc,fullscreen,restrictOutput,out);
        const auto source=sourceDevice(self,device,window);
        if(!source)return call(self,device,window,desc,fullscreen,restrictOutput,out);
        *out=nullptr;SourceWindow hidden;
        auto local=*desc;extent(window,local.Width,local.Height);
        DXGI_SWAP_CHAIN_FULLSCREEN_DESC windowed{};windowed.Windowed=TRUE;
        ComPtr<IDXGISwapChain1>native;const auto result=call(self,device,hidden.value,&local,&windowed,nullptr,&native);
        if(FAILED(result))return result;
        *out=createPresentationBridge(self,source.Get(),window,native.Get(),fullscreen?*fullscreen:windowed,restrictOutput,state.failure);
        hidden.value=nullptr;
        return result;
    }catch(const std::exception&e){fatal(e.what());}catch(...){fatal("DXGI CreateSwapChainForHwnd entry failed");}
}
template<class F>void installTarget(void*entry,void*detour,void*&installed,std::atomic<F>&original){
    // shortcut: one native implementation per method; add explicit provider binding if AC7 requires another.
    if(installed){if(installed!=entry)throw std::runtime_error("Multiple DXGI factory creation implementations are not supported by this entry");return;}
    void*trampoline=nullptr;
    hookCheck(MH_CreateHook(entry,detour,&trampoline),"Create DXGI factory method hook");
    original.store(reinterpret_cast<F>(trampoline),std::memory_order_release);
    installed=entry;
    hookCheck(MH_EnableHook(entry),"Enable DXGI factory method hook");
}
void remember(IUnknown*object){
    ComPtr<IDXGIFactory>factory;check(object->QueryInterface(IID_PPV_ARGS(&factory)),"DXGI factory interface");
    ComPtr<IUnknown>identity;check(factory.As(&identity),"DXGI factory canonical identity");
    ComPtr<IDXGIFactory2>factory2;const auto modern=factory.As(&factory2);
    if(modern!=E_NOINTERFACE)check(modern,"DXGI factory HWND interface");
    std::lock_guard<std::mutex>guard(state.lock);
    if(std::any_of(state.factories.begin(),state.factories.end(),[&](const auto&f){return f.Get()==identity.Get();}))return;
    state.factories.push_back(std::move(identity));
    installTarget(method(factory.Get(),10),reinterpret_cast<void*>(&swap),state.swapTarget,state.swap);
    if(factory2)installTarget(method(factory2.Get(),15),reinterpret_cast<void*>(&swapHwnd),state.hwndTarget,state.swapHwnd);
}
HRESULT factory(Factory call,REFIID id,void**out){
    const auto result=call(id,out);
    if(SUCCEEDED(result)){
        if(!out||!*out)throw std::runtime_error("DXGI factory creation returned no interface");
        remember(static_cast<IUnknown*>(*out));
    }
    return result;
}
HRESULT WINAPI factory0(REFIID id,void**out)noexcept{
    try{return factory(state.factory0,id,out);}catch(const std::exception&e){fatal(e.what());}catch(...){fatal("CreateDXGIFactory entry failed");}
}
HRESULT WINAPI factory1(REFIID id,void**out)noexcept{
    try{return factory(state.factory1,id,out);}catch(const std::exception&e){fatal(e.what());}catch(...){fatal("CreateDXGIFactory1 entry failed");}
}
void replaceImport(void**slot,Factory&saved,Factory replacement){
    if(!*slot)throw std::runtime_error("Missing resolved DXGI factory import");
    saved=reinterpret_cast<Factory>(*slot);
    DWORD protection=0;
    check(VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection)?S_OK:HRESULT_FROM_WIN32(GetLastError()),"Write DXGI import");
    InterlockedExchangePointer(reinterpret_cast<void*volatile*>(slot),reinterpret_cast<void*>(replacement));
    DWORD unused=0;
    check(VirtualProtect(slot,sizeof(*slot),protection,&unused)?S_OK:HRESULT_FROM_WIN32(GetLastError()),"Restore DXGI import protection");
}
}
void installPresentationEntry(void(*failure)(const char*)noexcept){
    state.failure=failure;
    auto*base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!base)throw std::runtime_error("Missing game image for presentation entry");
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>0x100000)throw std::runtime_error("Unsupported presentation entry image");
    const auto*pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)throw std::runtime_error("Unsupported presentation entry PE");
    const auto directory=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress||directory.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR)||directory.VirtualAddress>pe->OptionalHeader.SizeOfImage||directory.Size>pe->OptionalHeader.SizeOfImage-directory.VirtualAddress)throw std::runtime_error("Missing presentation import directory");
    const auto*imports=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
    unsigned installed=0;
    for(unsigned i=0;i<directory.Size/sizeof(*imports)&&imports[i].Name;i++){
        const auto&entry=imports[i];
        if(_stricmp(reinterpret_cast<const char*>(base+entry.Name),"dxgi.dll")||!entry.OriginalFirstThunk)continue;
        const auto*names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+entry.OriginalFirstThunk);
        auto*slots=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+entry.FirstThunk);
        for(unsigned n=0;names[n].u1.AddressOfData;n++){
            if(IMAGE_SNAP_BY_ORDINAL64(names[n].u1.Ordinal))continue;
            const auto*name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names[n].u1.AddressOfData);
            auto**slot=reinterpret_cast<void**>(&slots[n].u1.Function);
            if(!std::strcmp(reinterpret_cast<const char*>(name->Name),"CreateDXGIFactory")){replaceImport(slot,state.factory0,&factory0);installed++;}
            else if(!std::strcmp(reinterpret_cast<const char*>(name->Name),"CreateDXGIFactory1")){replaceImport(slot,state.factory1,&factory1);installed++;}
        }
    }
    if(!installed)throw std::runtime_error("Game has no supported DXGI factory creation imports");
}
}
