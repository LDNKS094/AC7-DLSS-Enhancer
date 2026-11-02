#include "scene_rhi_queue.h"
#include <windows.h>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace ac7 {
namespace {
using Wait=void(__fastcall*)(void*,bool);Wait wait=nullptr;
template<class T>T&field(void*p,size_t offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
static_assert(sizeof(SceneWork)==40&&offsetof(SceneWork,execute)==8&&offsetof(SceneWork,payload)==16);
void execute(void*,SceneWork*command)noexcept{std::unique_ptr<SceneWork>owned(command);owned->run(owned->payload);}
}
bool initializeSceneRhiQueue(){
    auto*base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));if(!base)return false;
    const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.SizeOfImage<0x3c79064)return false;
    constexpr unsigned char signature[]={0x40,0x53,0x48,0x83,0xec,0x20,0x8b,0x1d,0x94,0x0c,0xa6,0x02};
    if(std::memcmp(base+0x12183c0,signature,sizeof(signature)))return false;
    wait=reinterpret_cast<Wait>(base+0x12183c0);return true;
}
bool canQueueSceneWork(void*list)noexcept{return list&&!field<unsigned char>(list,0x10)&&field<SceneWork**>(list,8)&&field<unsigned>(list,0x14)!=std::numeric_limits<unsigned>::max();}
void waitForSceneTasks(void*list){
    if(!wait||!canQueueSceneWork(list))throw std::runtime_error("Scene task join requires the normal producer list");
    wait(list,false);
}
std::unique_ptr<SceneWork>prepareSceneWork(void*payload,void(*run)(void*)noexcept,void(*cancel)(void*)noexcept){
    if(!payload||!run||!cancel)throw std::invalid_argument("Missing owned scene command");
    return std::make_unique<SceneWork>(SceneWork{nullptr,&execute,payload,run,cancel});
}
void enqueueSceneWork(void*list,SceneWork*command)noexcept{
    // The native executor saves next before calling us. Our callback frees only our own heap node.
    *field<SceneWork**>(list,8)=command;field<SceneWork**>(list,8)=&command->next;++field<unsigned>(list,0x14);
}
void discardSceneWork(void*list)noexcept{
    // Normal execution already consumed owned nodes; head/tail may point at freed nodes until native reset.
    if(!list||field<unsigned char>(list,0x10))return;
    auto**link=&field<SceneWork*>(list,0);
    while(auto*node=*link){auto*next=node->next;if(node->execute==&execute){*link=next;if(!next)field<SceneWork**>(list,8)=link;--field<unsigned>(list,0x14);std::unique_ptr<SceneWork>owned(node);owned->cancel(owned->payload);}else link=&node->next;}
}
}
