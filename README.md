# AC7 Native Adapter

AC7原生NR/SR实现。处理链为原生HDR/depth/MV/View → D11→D12 → SDR代理 → NR → HDR恢复 → 同尺寸SR → D11返回 → 游戏原warp/post/tonemap。

模型按真实资源和View关联接入，不使用关卡、shader哈希或分辨率白名单。处理尺寸跟随游戏实际纹理allocation与有效View，运行中尺寸变化时退休旧工作、重建尺寸资源并重置模型历史，device/queue继续复用。当前支持R11 HDR与既有depth/MV协议，jitter为0；P=1未校准、SR为同尺寸DLAA，FG与周边压缩尚未接入。

| 游戏前台快捷键 | 功能 |
|---|---|
| Ctrl+Shift+F10 | 开关NR，SR继续 |
| Ctrl+Shift+F11 | 手动重载ASI旁的INI |
| Ctrl+Shift+F12 | 保存同帧原HDR与NR＋SR最终输出的共同映射BMP预览 |

[当前状态](development/docs/STATUS.md) · [游戏操作](development/docs/GAME-RUNBOOK.md) · [数据契约](development/docs/DATA-CONTRACT.md) · [路线](development/docs/ROUTES.md)

## 构建ASI

SDK、模型与MinHook由本地提供。当前构建目录已配置这些路径时，按需执行：

```text
cmake -S . -B development/build/current -DAC7_BUILD_DEVELOPMENT=ON
cmake --build development/build/current --config Release --target ac7-native-models --parallel 4
```

新机器还需配置`AC7_MINHOOK_SOURCE_DIR`、`NGX_INCLUDE_DIR`与`NGX_LOADER_LIBRARY`。输出为`development/build/current/development/Release/AC7NativeModels.asi`。核心库在`src/`，ASI实现位于`development/src/`和`development/tools/neutral-asi/`。

仓库仅提供核心库与实际ASI构建目标。原始数据、当前候选和模型/SDK依赖保留。
