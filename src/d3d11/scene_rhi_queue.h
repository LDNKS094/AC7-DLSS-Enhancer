#pragma once
#include <memory>
namespace ac7 {
// Owned command nodes are allocated before admission, never from the engine arena.
struct SceneWork {
    SceneWork*next=nullptr;
    void(*execute)(void*,SceneWork*)noexcept=nullptr;
    void*payload=nullptr;
    void(*run)(void*)noexcept=nullptr;
    void(*cancel)(void*)noexcept=nullptr;
};
bool initializeSceneRhiQueue();
void waitForSceneTasks(void*immediateList);
bool canQueueSceneWork(void*list)noexcept;
std::unique_ptr<SceneWork>prepareSceneWork(void*payload,void(*run)(void*)noexcept,void(*cancel)(void*)noexcept);
// Same normal producer/list verified at admission. Only pointer linking occurs here; no allocation can fail.
void enqueueSceneWork(void*list,SceneWork*)noexcept;
void discardSceneWork(void*list)noexcept;
}
