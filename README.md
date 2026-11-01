# AC7 Native Adapter

目标是 AC7 原生 HDR NR／SR、超宽屏 NR 周边减负和 NVIDIA 官方 DLSSG 接入。NR／SR／周边压缩已有用户实机反馈；FG 尚未接通。

最新 [ASI](development/artifacts/releases/gpu-context-simple/AC7NativeModels.asi) 已从整理后的稳定依赖重新构建，未实机。它包含统一设备／队列与设备期 NGX 会话、运行链优化及意外错误 FailFast；没有固定 FG 倍率或独立 RCAS 专用分支。本次留档包含上下文实现与目录整理，前序运行链优化基线为 `961ec1a`。

## 目录与文档

| 路径 | 职责 |
|---|---|
| `src/` | 自有可复用核心 |
| `development/src/`、`development/tools/neutral-asi/` | ASI 与游戏接线 |
| `third_party/` | 正式第三方源码、SDK、loader 和模型，见其 [说明](third_party/README.md) |
| `development/build/` | 可整目录删除的生成输出，不保存唯一源码或依赖 |
| `development/artifacts/releases/`、`build-logs/` | 本机交付候选与对应构建日志 |
| `development/history/` | 经过筛选的专题记录、原始捕获、实现证据及参考快照，见 [索引](development/history/README.md) |
| `trash/20261102-reorganization/` | 本轮隔离的停用实验／派生数据，未永久删除 |

当前事实以 [STATUS](development/docs/STATUS.md) 为准，下一步看 [ROUTES](development/docs/ROUTES.md)，输入及生命周期看 [DATA-CONTRACT](development/docs/DATA-CONTRACT.md)，使用看 [GAME-RUNBOOK](development/docs/GAME-RUNBOOK.md)。[SR 阶段职责](development/docs/SR-LOGIC-PLAN.md)与[问题记录](development/docs/PROBLEM-RECORD.md)是支持资料；历史报告不作为现行实施要求。个人笔记由用户维护。

## 构建与使用

Windows、Visual Studio 2022 C++ 工具链、Windows SDK／FXC、CMake。正式依赖默认从仓库 `third_party` 读取，不再依赖相邻项目或历史实验目录：

```powershell
cmake -S . -B development/build/current -G "Visual Studio 17 2022" -DAC7_BUILD_DEVELOPMENT=ON
cmake --build development/build/current --config Release --target ac7-native-models --parallel 4
```

生成 `development/build/current/development/Release/AC7NativeModels.asi`。需要保留交付时另存至 `development/artifacts/releases`；清理 build 不会删除现有交付候选或 SDK。外部依赖覆盖参数仍为 `AC7_MINHOOK_SOURCE_DIR`、`NGX_INCLUDE_DIR`、`NGX_LOADER_LIBRARY`。

由用户退出游戏后替换本项目 ASI，只加载一份；保留其它模组及游戏设置。ASI 旁使用 `AC7NativeModels.ini` 和 `AC7NativeModels/` 模型目录，模型原件在 `third_party/models/`。助手不部署或启动游戏。

```ini
[SR]
Enabled=1
Mode=0
Sharpness=0

[NR]
Enabled=0
Intensity=1
Tone=0.3
Structure=0.7
Style=0
PeripheralCompression=0
CenterAspect=1.3333333
EdgeScale=0.5
TransitionWidth=0.05
```

Mode 0／1／2 对应 Quality（2/3）、Balanced（58%）、Performance（50%）。Sharpness 为0–1，0旁路 RCAS。SR 关闭可继续原生尺寸 NR；独立 RCAS 不是目标场景，允许通用链自然工作，不专门适配。SkinStructure 已移除。

游戏前台 Ctrl+Shift+F10 切换整模组，F11 重载配置，F12 保存下一成功效果帧的 HDR 输入／最终输出预览。F12 包含实际开启的 NR／SR／RCAS 效果，没有单独保存 NR 中间结果。

原始 View 身份、真实 jitter、原生 RHI 顺序、producer Flush、跨 API fence 及资源退休均保留。意外错误尽力记录 `fatal` 原因后终止，不静默停用；无 GPU 像素读回检查、测试宿主或自动部署。当前没有帧时间收益实测。
