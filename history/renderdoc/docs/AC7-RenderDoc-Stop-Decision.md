# RenderDoc 排查阶段收束

## 决策

暂停 RenderDoc 的 AC7 试启动。已达到事先约定的有界排查终点：基础 D3D11 捕获/重放成功，但无 mod AC7、两个相邻官方版本及最后一次尺寸参数对照均未取得有效 AC7 帧。没有新的具体失败调用证据，不继续试旧版本、修改驱动或 TDR 等全局设置。

这不是认定 AC7 永远不能使用 RenderDoc，也不是否定原生 NR/SR/FG 适配可行；仅表示当前环境下这条工具路线的继续试错收益不足。

## 最后一次结果

用户覆盖的 `docs/Presets/log.log` 同时含 UE 错误栈与 Win32 输出。副本保存至 `artifacts/diagnostics/renderdoc-final-720p/user-output.log`。

- 目标 PID 108636，官方 RenderDoc 1.45，SteamAPI_Init 成功。
- UE4 仍报告 D3D11Util.cpp:182，设备丢失 0x887A0020 / DXGI_ERROR_DRIVER_INTERNAL_ERROR。
- 06:44:11 注册设备/窗口对应的 frame capturer，约 47.249ms 后 `D3D11: Removing Device`，同一条 EXECUTION WARNING #379，未得到更早的具体 API 错误。
- 用户报告“没有任何区别”。捕获对象注册不是实际 Present 调用跟踪，且 GPU 异步执行，不能把最后一条正常日志当成根因位置。
- 用户执行的是准备的尺寸参数试验，但所贴输出没有命令行或交换链尺寸；保存的 GameUserSettings.ini 仍为 5120×1440、FullscreenMode=1。不能据此断言 1280×720 的实际资源尺寸已生效，也不能将尺寸假设记为完全排除。按事先条件，即使参数采用情况未知也不无限补测。

## 已得到的有效结论

| 证据 | 可以支持什么 | 不能支持什么 |
| --- | --- | --- |
| 无工具直接启动 A 正常 | 原版可运行，RenderDoc 介入相关值得关注 | 所有游戏 API 用法/驱动路径都正确 |
| 无额外 DLL/ASI 后仍失败 | 已隔离 mod 不是故障出现的必要条件 | 所有系统注入/驱动交互都排除 |
| 工作目录明确、延迟为零仍失败 | 不再优先怀疑这两个启动变量 | 其他所有启动差异完全排除 |
| Overlay 两层开关一直关闭 | 不再重复检查功能开关 | Steam 模块所有内部 Hook 均不存在 |
| 1.45/1.46 同类设备丢失 | 仅 1.46 回归解释削弱 | 所有版本都不兼容 |
| D3D11 调试层 #379 | 驱动/应用图形交互进入设备移除状态 | NVIDIA 驱动单方缺陷、硬件损坏或显存不足 |
| 最小 D3D11 三角形捕获+重放成功 | 基础硬件 D3D11 捕获链路可用 | 覆盖 UE4、trueSKY、Aftermath、复杂并行资源工作 |

## 当前现场与恢复

游戏未运行。额外 mod 仍完整存放在 `artifacts/backups/clean-renderdoc-baseline/`，20 个文件路径/哈希见 [恢复清单](../../../docs/AC7-Clean-Test-State.json)。游戏目录仍为官方内容加临时 `steam_appid.txt`（502500），没有放入 RenderDoc DLL。未在本轮恢复 mod 或改变用户画质，避免无说明地改变后续对照环境。

最新检查时日志收集器 PID 20748 仍运行，原脚本为 300 秒有界收集；用户可在该窗口 Ctrl+C 结束，不需要再启动它。后续普通启动不需要该收集器。没有启用系统 Silent Process Exit 配置。

后续要恢复正常 mod 使用时，退出游戏后按清单恢复，核对目标不被覆盖并验证哈希；临时 App ID 文件仅在核对内容仍为 502500 后移除。恢复后需要正常启动验证，不能只凭文件复制宣布恢复成功。

## 后续方向

优先候选的官方支持评估已完成，见 [Nsight Graphics评估](../../nsight-startup/AC7-Nsight-Graphics-Evaluation.md)：2025.5 Windows x64 的 Frame Debugger 有明确 D3D11 文档，Blackwell 与本机驱动满足文档前提。尚未下载、安装或实测；先验证最小D3D11程序再尝试一次AC7，不启用D3D debug runtime或叠加RenderDoc。不能将新版仅支持D3D12/Vulkan的Graphics Capture当成此方案。

若另一种完整捕获工具也成本过高，再评估最小运行期诊断，仅观测实际设备、交换链、资源绑定及 pass；这是新的受控适配工作，不能提前宣布已经知道深度、速度、jitter 或其他原生数据语义，也不在没有窄目标前搭建完整注入框架。

项目目标仍是原生数据和高分辨率输出回接；RenderDoc 可用不是项目验收前提。后端选型继续保持开放。
