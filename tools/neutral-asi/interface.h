#pragma once
#include <windows.h>
#include <cstdint>
// Explicit controls for the isolated host; game mode derives identity from actual View uploads.
struct AC7NeutralStatus {
    uint32_t size=sizeof(AC7NeutralStatus),version=2;
    uint32_t bootstrap=0,installed=0,enabled=0,stopped=0,drained=1,error=0;
    uint32_t diagnosticStage=5;
    uint64_t shaderMatches=0,drawIntercepts=0,originalDrawCalls=0,applied=0,refused=0,pending=0;
    uint32_t viewMode=1,lastViewCounter=0;
    uint64_t guideShaderMatches=0,viewUploads=0,guideCandidates=0,viewRefused=0,observedFrames=0,lastViewToken=0;
};
using AC7NeutralInitialize=void(WINAPI*)();
using AC7NeutralControl=BOOL(WINAPI*)(BOOL enabled,uint64_t frame);
using AC7NeutralViewControl=BOOL(WINAPI*)(BOOL enabled);
using AC7NeutralStopFn=BOOL(WINAPI*)();
using AC7NeutralQuery=BOOL(WINAPI*)(AC7NeutralStatus*);
