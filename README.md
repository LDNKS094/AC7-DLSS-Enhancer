# AC7 Native Adapter

当前基线为 [HUD 生命周期修正版 ASI](development/build/hud-lifetime/development/Release/AC7NativeModels.asi)，使用正常 DLSS 路径。用户在此前确认菜单 UI、启动 Logo、模型／场景及 DLSS 抗锯齿正常，并在本版实机测试后反馈未发现异常。HUD 缩小问题按本次反馈结案，不再单独修改雷达 alpha。实现于原生交接前保留同帧 UI 资源身份供图调度使用。一次性 D3D11 截帧分支已移除，捕获和历史二进制保留；早期留档为 `8301e49`。

当前候选将 HDR DLSS SR 接入原生后处理图的时序逻辑位置：低分几何、透明与 Distortion 先按原生网格完成；图调度落实颜色输入后执行 SR，再由高分颜色链继续处理。深度、stencil、GBuffer 与低分运动 tile 不再整体放大。

流程重整及 HUD 修复已获用户本轮实机反馈。该结果限于已体验范围，不表示所有关卡和配置均已覆盖。NR、RCAS 继续旁路；不调整 jitter 序列、mip 或其它画质参数，不读写游戏 AA 字段，FXAA 仍由用户手动关闭。

## 使用

本次 ASI：[AC7NativeModels.asi](development/build/hud-lifetime/development/Release/AC7NativeModels.asi)。完全退出游戏后由用户替换原加载位置的本项目 ASI，只加载一份。沿用 ASI 旁的 INI、模型目录与 DLL，保留宽屏、dinput8、UE4SS 和其它模组。

```ini
[SR]
Mode=0
Sharpness=0.4
```

| Mode | 每轴输入比例 |
|---|---|
| 0 — Quality | 2/3 |
| 1 — Balanced | 58% |
| 2 — Performance | 50% |

Sharpness 与 NR 参数保留读取，本版不执行锐化或 NR。

| 游戏前台快捷键 | 功能 |
|---|---|
| Ctrl+Shift+F10 | 整模组开关；在途帧自然完成，后续保持原生 |
| Ctrl+Shift+F11 | 重载配置；改 SR 档位才使模型历史失效 |
| Ctrl+Shift+F12 | 保存下一成功 HDR SR 的输入／输出预览与元数据 |

日志 `resolutionReady=1` 仅表示入口安装；`sr-scene stage=temporal-graph` 才表示实际 SR 提交。资源或模型错误记录为 `scene-error` 并停止后续准入，空间输出不会记作 SR。D3D 设备或 View 常量上传失效属于当前帧不可恢复错误。

[状态与限制](development/docs/STATUS.md) · [游戏操作](development/docs/GAME-RUNBOOK.md) · [数据契约](development/docs/DATA-CONTRACT.md) · [执行路线](development/docs/ROUTES.md) · [逻辑计划](development/docs/SR-LOGIC-PLAN.md) · [问题记录](development/docs/PROBLEM-RECORD.md)

## 构建

当前基线构建目录 `development/build/hud-lifetime` 已配置：

```text
cmake --build development/build/hud-lifetime --config Release --target ac7-native-models --parallel 4
```

新配置需提供 AC7_BUILD_DEVELOPMENT、AC7_MINHOOK_SOURCE_DIR、NGX_INCLUDE_DIR、NGX_LOADER_LIBRARY。MSVC/MASM 构建原生边界，Windows SDK 的 FXC 将局部颜色 shader 编译并嵌入 ASI；可用 AC7_FXC_EXECUTABLE 指定已有工具路径。

旧源码留档为 `ea213fb`；对应当时 ASI 保存在 `development/artifacts/research/sr-state-ea213fb/`，旧 current 二进制也未覆盖。原始捕获、日志、INCOMPLETE、SDK 与模型均保留。本轮只构建交付二进制，不运行测试宿主、不自动部署或启动游戏；游戏性能和新流程实机结果待用户确认。
