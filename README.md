# AC7 Native Adapter

当前接线：**真实低分辨率3D → 游戏低分后处理/tonemap → 末端LDR DLSS SR → 原目标尺寸输出**。NR暂时完全旁路。模组不读取或改写AA模式，请在游戏设置中手动关闭FXAA。HUD/UI模糊留待下一波处理，目前不承诺UI保持原生清晰度。

已接入真实Halton投影jitter：在引擎可见性/历史保存前改投影，调用原生逆矩阵与派生矩阵更新；View携带实际偏移，camera MV回退去除jitter，NGX接收低分像素单位偏移。SR后加入AMD RCAS锐化，F12包含锐化后的结果。SR暂保留当前插入位置，待SR本身稳定后前移到更早的颜色/guide一致阶段。

按SR比例的纹理mip偏置尚未接入，后置颜色与原3D guide的对应仍未合格确认。先前Quality日志在1524次提交后停止进入SR，当前颜色流补齐候选尚待实机确认；专项诊断日志仅在此问题未解决期间保留，解决后立即删除。

## SR档位

在ASI旁的`AC7NativeModels.ini`追加，已有NR参数保留：

```ini
[SR]
Mode=0
Sharpness=0.4
```

| Mode | 每轴渲染比例 | NGX模式 |
|---|---|---|
| 0 — Quality（默认） | 约66.7%（2/3） | MaxQuality |
| 1 — Balanced | 58% | Balanced |
| 2 — Performance | 50% | MaxPerf |

`Sharpness`为RCAS直接强度0..1，默认0.4，0完全绕过锐化；F11重载，缺失/非法保留当前值。只改锐化不重置DLSS历史，也不改变分辨率。

Ctrl+Shift+F11显式重载。缺失/非法值在启动时用Quality，重载时保留当前模式。切档时新View按新比例渲染，旧GPU工作退休后重建尺寸资源并Reset SR历史；不预设输出分辨率。NGX模式依据实际处理帧的输入/输出网格选取，排队中的旧View不套用新模式。

| 游戏前台快捷键 | 功能 |
|---|---|
| Ctrl+Shift+F10 | 整个模组效果开关，默认开启；关闭后恢复后续View的游戏原分辨率 |
| Ctrl+Shift+F11 | 重载SR档位及保留的NR参数；本版NR不执行 |
| Ctrl+Shift+F12 | 保存下一成功SR帧的低分LDR输入与高分SR输出PNG |

入口按实际资源和View关联，不使用关卡或shader哈希白名单。分辨率hook保留入口定位，FeatureLevel只读；不再匹配AA指令。快捷键独立采样，`resolutionReady`显示接线状态，`sr-upscale`记录实际SR提交与模式。共享桥保留producer Flush、跨API fence和三个allocator槽，尺寸重建复用device/queue。旧HDR/NR实现保留供后续接回。

[当前状态](development/docs/STATUS.md) · [游戏操作](development/docs/GAME-RUNBOOK.md) · [数据契约](development/docs/DATA-CONTRACT.md) · [路线](development/docs/ROUTES.md) · [DLSS SR 接入检查清单](development/docs/DLSS-SR-CHECKLIST.md)

## 构建ASI

本地SDK、模型与MinHook路径已配置时：

```text
cmake -S . -B development/build/current -DAC7_BUILD_DEVELOPMENT=ON
cmake --build development/build/current --config Release --target ac7-native-models --parallel 4
```

新机器需配置`AC7_MINHOOK_SOURCE_DIR`、`NGX_INCLUDE_DIR`与`NGX_LOADER_LIBRARY`。输出为`development/build/current/development/Release/AC7NativeModels.asi`；沿用INI、模型目录与原加载方式。游戏部署、设置与启动由用户操作，本轮只构建实际ASI。

核心库在`src/`，ASI实现位于`development/src/`和`development/tools/neutral-asi/`。原始数据、旧候选和模型/SDK依赖保留。
