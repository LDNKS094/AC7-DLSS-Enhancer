# AC7 Native Adapter

目标是 AC7 原生 HDR NR／SR、超宽屏 NR 周边减负和 NVIDIA 官方 DLSSG 接入。NR／SR／周边压缩已有用户实机反馈；FG已有SDK额外呈现日志及关卡无异常反馈，前版Auto已有SDK额外呈现记录，新候选固定2倍已有运行记录，自动启停尚未覆盖。

当前留档包含官方呈现、输入退休、独立原生取数及可配置FG；前序基线为 `5bbb6d4`，不push，个人笔记不纳入。前版 [fg-queue-retirement ASI](development/artifacts/releases/fg-queue-retirement/AC7NativeModels.asi) 已有用户关卡反馈“没有问题”，日志确认SDK报告额外呈现帧、SR／NR持续运行，无fatal。首次零消费点误判已修正，失败及成功日志均保留。当前已接入原生状态自动启停，Enabled=1后无需按F9，固定2倍运行未见fatal，自动启停覆盖待补。未改SR／NR画质参数，未测性能收益。

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

由用户退出游戏后替换本项目 ASI，只加载一份；保留其它模组及游戏设置。ASI 旁使用 `AC7NativeModels.ini` 和 `AC7NativeModels/` 模型目录，模型原件在 `third_party/models/`。presentation-bridge另需将交付目录内 `AC7NativeModels/` 的6个官方运行库合入游戏同名目录，保留原SR／NR模型。缺库或签名无效会明确报错终止。助手不部署或启动游戏。

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

[FG]
Enabled=1
Multiplier=0
```

Mode 0／1／2 对应 Quality（2/3）、Balanced（58%）、Performance（50%）。Sharpness 为0–1，0旁路 RCAS。SR 关闭可继续原生尺寸 NR；独立 RCAS 不是目标场景，允许通用链自然工作，不专门适配。SkinStructure 已移除。

最新候选为 [automatic-fg ASI](development/artifacts/releases/automatic-fg/AC7NativeModels.asi)，本次固定2倍运行无fatal；配套 [FG配置注释](development/artifacts/releases/automatic-fg/FG-settings.example.ini) 合入原有INI并设置Enabled=1，6个官方运行库位于同目录AC7NativeModels中，保留原有模型。未配置Enabled时仍保持关闭。

automatic-fg已接入原生状态自动启停，并取得本次固定2倍运行记录；原生状态自动切换尚未覆盖。Enabled保存用户选择；Enabled=1后不需进关卡再按F9。主线程在Sleep前及SimulationEnd重新取当前GWorld，以游戏自己的GetPlayerPlane确认玩家飞机存在，排除原生IsInHangar及UWorld::IsPaused。无世界／无玩家、机库或暂停时暂时Off，恢复后按原选择继续；Present同时要求该帧和最新游戏状态允许，并保留原有同帧输入／SDK能力门槛。失去匹配场景输入的加载帧Off，Resize／退出继续先停用并等待退休。临时Off不修改Enabled，不释放仍被消费者使用的资源。仅有三维View或飞机模型不作为启用依据，不按关卡名或shader名单判断。

Enabled为总开关；Multiplier=0表示向目标帧率自适应的Auto，1表示不插帧，2、3、4…为固定总倍率。Auto每个真实主帧读取原生t.MaxFPS，作为插帧后目标；无上限时由SDK跟随显示器刷新率。VSync开启时SDK忽略自定义目标、优先跟随刷新率。固定倍率保留原生限帧，不除以倍率，不主动降低真实帧率。完全不支持FG时恢复运行时禁用，不改INI；固定倍率超限采用最高支持倍率，不支持动态Auto则回退最高固定倍率并按固定模式限帧。原因只写既有日志，不新增设置页或提示。旧fg-queue-retirement的0仍为旧SDK默认请求，不与新候选混用。Auto仅配置官方动态目标及Reflex限帧，不改游戏设置或绕过原生delta-time流程；两种限帧并存的实际节奏尚未实机确认。

游戏前台 Ctrl+Shift+F9 临时切换FG请求，不写INI；F10只切换模型效果，F11重载全部配置，F12保存下一成功效果帧的HDR输入／最终输出预览。F12包含NR／SR／RCAS效果，不包含插帧，也没有单独保存NR中间结果。

原始 View 身份、真实 jitter、原生 RHI 顺序、producer Flush、跨 API fence 及资源退休均保留。意外错误尽力记录 `fatal` 原因后终止，不静默停用；无 GPU 像素读回检查、测试宿主或自动部署。当前没有帧时间收益实测。
