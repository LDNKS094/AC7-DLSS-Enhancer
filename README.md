# AC7 Native Adapter

当前候选已改为**统一View接管、早期HDR DLSS SR**：确认该View的GPU资源与回退准备完成后，才缩小渲染rect、加入真实jitter；在不透明场景结束、透明/Distortion之前重建，后续使用高分、去jitter的View。未接管的View不受本模组缩放/jitter影响。旧末端LDR Draw匹配与颜色流hook已移除。

这次针对“jitter已放行、SR入口却不放行”的控制缺口落地实现，**尚未由用户实机确认菜单、机库抖动是否消失**。NR与RCAS暂时完全旁路，配置保留。模组不读写游戏AA字段，FXAA仍由用户手动关闭；jitter序列和其它画质参数未调。

## 使用

沿用ASI旁的`AC7NativeModels.ini`与`AC7NativeModels/`模型目录，仅替换ASI。只加载一个本项目ASI，保留宽屏、dinput8、UE4SS及其它模组。部署、游戏启动与设置由用户操作。

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

`Sharpness`当前不生效，值保留供之后恢复；NR参数同样保留。实际输入/输出尺寸决定NGX模式。

| 游戏前台快捷键 | 功能 |
|---|---|
| Ctrl+Shift+F10 | 整模组开关；在途帧自然完成，后续View恢复原生 |
| Ctrl+Shift+F11 | 重载SR档位与保留的NR/锐化配置 |
| Ctrl+Shift+F12 | 保存下一成功HDR SR的前后PNG与元数据 |

日志`resolutionReady=1`表示统一入口hook已安装；`sr-scene stage=before-translucency`表示实际早期SR提交。`scene-error`表示具体失败并停止后续准入，回退不会伪记为SR。正常运行不逐帧读回。

[状态与限制](development/docs/STATUS.md) · [游戏操作](development/docs/GAME-RUNBOOK.md) · [数据契约](development/docs/DATA-CONTRACT.md) · [执行路线](development/docs/ROUTES.md)

## 构建

本机SDK、模型与MinHook路径已配置时：

```text
cmake -S . -B development/build/current -DAC7_BUILD_DEVELOPMENT=ON
cmake --build development/build/current --config Release --target ac7-native-models --parallel 4
```

新机器配置`AC7_MINHOOK_SOURCE_DIR`、`NGX_INCLUDE_DIR`与`NGX_LOADER_LIBRARY`；MSVC的MASM编译实际场景边界入口。输出`development/build/current/development/Release/AC7NativeModels.asi`。

当前原生场景纹理保留完整allocation，比旧低分allocation使用更多显存；本轮未测游戏性能。GPU桥保留producer Flush、跨API fence、三槽allocator保护与尺寸退休；不支持运行中卸载。核心库在`src/`，ASI在`development/src/`和`development/tools/neutral-asi/`。旧候选、原始捕获与日志、SDK/模型依赖保留。
