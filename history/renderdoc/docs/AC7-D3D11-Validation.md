# D3D11 设备丢失：1.45 对照与验证层取证

## 操作修正：1.45 界面没有 Mute Debug Output 开关

已核对 v1.45 官方 `qrenderdoc/Windows/Dialogs/CaptureDialog.ui` 和 `.cpp`：界面没有该开关，设置往返也没有传递 debugOutputMute。界面启动因此可能重建为默认 true。先前让用户取消此复选框、或仅加载带 false 的 `.cap` 的指导不适用；不是用户遗漏。

本机官方 `renderdoccmd capture --help` 明确提供 `--opt-api-validation` 和 `--opt-api-validation-unmute`。后续使用 `Launch-AC7-D3D11-Validation.cmd`（清理时已移除的实验文件）启动，不再通过 GUI 执行本轮验证。脚本使用完整配套 1.45 工具，保持工作目录和 `-windowed`。进程父级由 GUI 改为官方 CLI 是本轮已知差异；本轮目标是取得诊断消息，不是严格证明静音字段的因果作用。

当前操作：先运行 `Collect-AC7-D3D11.cmd`（清理时已移除的实验文件），保持其窗口打开，再运行上述 Launch 脚本。旧收集器需先用 Ctrl+C 结束，避免重复占用输出通道。无需在 RenderDoc 中找复选框或再点 Launch。脚本尚未执行，是否收到 AC7 输出仍待验证。


## 用户补充的控制台输出：已取得真实验证消息

用户提供 [log.log](../user-output.log)，共 53 个非空行，PID 全部为 66176。它表明即使输出文件检查时为 0 字节，控制台仍已收到 Win32 调试消息；先前“未取得任何消息”的判断应撤回。文件写入缓冲等具体原因未确认，不再要求为此重复同一轮。

关键尾部为 `D3D11: Removing Device` 和 EXECUTION WARNING #379 `DEVICE_REMOVAL_PROCESS_POSSIBLY_AT_FAULT`：驱动执行未定义操作的迹象很强，但可能源于应用先执行非法或未定义操作。这是设备移除的诊断分类，不定位具体责任组件或原始 API。

frame-capturer 注册后约 45.3ms 就报告移除，支持失败靠近早期呈现/启动 GPU 工作的阶段；注册事件不是 Present 的函数跟踪，不能据此说某次 Present 或 RenderDoc 注册本身导致故障。GPU 异步执行也使最后一条 CPU 日志不等于最早错误位置。

所贴片段没有更早的 D3D11 ERROR，但序号不连续且无法保证完整，不能宣称验证层已证明之前调用合法。SteamAPI_Init 已在该进程成功；dbghelp 导致调用栈采集禁用的日志不构成设备丢失原因，多个设备/WARP 创建仍可能是探测。此轮已有有效取证，停止重复相同测试；后续需能区分原生 GPU 工作与捕获包装交互的新证据，而非继续假定未取消静音。

## 最新 CLI 轮（PID 66176）：验证层已加载，初次文件检查未见消息

本次检查确认官方 CLI 的进程命令行带 `--opt-api-validation --opt-api-validation-unmute`，目标加载了系统 `D3D11_3SDKLayers.dll` 和便携目录 1.45 `renderdoc.dll`。收集器检查时在运行，日志仍为 0 字节。此时游戏曾 Responding=true，但未确认菜单或飞行；之后窗口枚举未得到该 PID 的可见窗口，不能用它替代用户报告。

因此当前只能确认启动参数及 SDK 层加载，不能宣称已经成功取消了运行时 InfoQueue 的所有静音/过滤，也不能由空日志推断无验证错误。用户可见结果待确认，暂不重复启动。观察记录与空日志快照在 `artifacts/diagnostics/d3d11-validation/cli-run-66176/`。原 RenderDoc 日志读取后在复制时已不存在，未声称已保存其完整副本。

## 本轮结果

用户在 1.45 上再次提供与 1.46 相同的 UE 错误类别：D3D11Util.cpp:182，设备丢失，0x887A0020 / DXGI_ERROR_DRIVER_INTERNAL_ERROR。

本地日志确认：1.45 UI PID 134392 启动目标 PID 84600，捕获库日志为 v1.45；06:15:55 到 D3D11 Presenting，06:16:02 连接重置。实际保存设置仍为明确工作目录、`-windowed`、delayForDebugger=0，捕获选项与此前基线一致。原日志及配置保存在 `artifacts/diagnostics/renderdoc-1.45-failure/`。

结论：仅 1.46 回归的解释被削弱，不证明所有 RenderDoc 版本都不兼容，也没有定位驱动、游戏中间件或包装层中具体哪一方有错。用户提供的错误栈和先前相同不代表执行基址/最初 GPU 失败点已被定位。

## 源码与资料核对

- RenderDoc v1.45/v1.46 的 `renderdoc/driver/ihv/nv/nvapi_hooks.cpp` 均在未启用相应 vendor extension 时对非白名单 NvAPI 查询返回 NULL。日志中的 Returning NULL 是预期拦截路径，不是独立的驱动错误证据。暂不盲目开启 vendor extension 绕过。
- v1.45 `renderdoc/driver/d3d11/d3d11_hooks.cpp` 在 apiValidation=true 时给设备创建加 D3D11_CREATE_DEVICE_DEBUG；false 时移除该标志。这是下一项取证的代码依据。
- 搜到的 D3D12 捕获回归、Vulkan NVIDIA workaround 不属于本次 API/阶段，不作为 AC7 解决方案。
- 尚未找到证据足以认定 AC7 的 Aftermath 或 trueSKY 为根因，不能凭 DLL 名称停用官方游戏组件。

源码：https://github.com/baldurk/renderdoc/blob/v1.45/renderdoc/driver/d3d11/d3d11_hooks.cpp

## 已验证的输出收集链路

官方工具：https://learn.microsoft.com/en-us/sysinternals/downloads/debugview

下载：https://download.sysinternals.com/files/DebugView.zip

已核验微软签名 Valid，DebugViewCLI x64 v5.02，SHA-256：`7954a8bbeb1f650bb5d2b8c4c6b642fd4585319d2092433ca841c7d672a4634b`。本次使用普通用户 Win32 OutputDebugString 捕获，不启用内核/全局会话捕获，不附加异常调试器。执行过 `--accepteula`，写入的是工具自身 EULA 接受记录，非系统崩溃处理配置。

自检用独立 D3D11 程序创建 NVIDIA 硬件 DEBUG 设备成功，feature level 11.0；故意传入 ByteWidth=0 的缓冲区描述，得到预期 E_INVALIDARG，并由 CLI 捕获到 CREATEBUFFER_INVALIDDIMENSIONS 和 CREATEBUFFER_INVALIDARG_RETURN。自检未启动游戏、未经过 RenderDoc，不能证明游戏取证会得到同样详细的原因。日志位于 `artifacts/diagnostics/d3d11-validation/collector-selfcheck.txt`。

## 本次取证轮结果：输出仍静音，未获得有效消息

用户执行后，1.45 目标 PID 33664 于 06:26:48 到 Presenting，随后停止呈现/退出。收集器 PID 122216 检查时 running=true、paused=false、elevated=false；输出文件为 0 字节。

实际 `%APPDATA%/qrenderdoc/most_recent.cap` 为 apiValidation=true、**debugOutputMute=true**，未满足准备配置要求的 false。原因未确认，不归咎于用户操作。空日志不能用来证明验证层没有报错。进程在模块检查前已退出，未核实本次加载的验证层 DLL。

本轮快照保存在 `artifacts/diagnostics/d3d11-validation/muted-run-33664/`。后续已按文首修正为官方 CLI 显式启用验证和取消静音；不要继续寻找 GUI 复选框。重新开始收集前结束旧收集器，避免通道竞争；旧证据已保存。

## 下一项：完成取证条件后再复现一次

这不是只改一个行为参数的因果对照，而是有意启用一组诊断观测：`apiValidation=true`、`debugOutputMute=false`，并启动输出收集。其余条件固定为 1.45、现有无 mod 游戏、同一驱动、`-windowed`、明确工作目录及零延迟。启用验证会改变执行开销/行为；若失败类型改变，需单独记录。

1. 退出游戏，关闭其他正在占用 Win32 DebugView 通道的收集器；本工具目前没有常驻等待进程。
2. 双击 `Collect-AC7-D3D11.cmd`（清理时已移除的实验文件）。它只收集名为 Ace7Game.exe 的进程，最多 300 秒/10000 行/10 MB，输出 `artifacts/diagnostics/d3d11-validation/ac7-validation.txt`。本地日志每轮覆盖，下一轮前先保存旧证据。Ctrl+C 可提前结束。
3. 双击 `Launch-AC7-D3D11-Validation.cmd`（清理时已移除的实验文件），由官方 1.45 CLI 显式传入验证和 unmute 参数；不要改用 GUI 启动本轮测试。
4. 出错后保留弹窗文本，不反复 Launch；检查输出日志、RenderDoc 目标 PID，以及是否确实加载验证层。
5. 结束后回到 `原窗口化配置`（清理时已移除的实验文件）即可撤销验证选项，停止收集器。无需修改系统诊断注册表或替换 DLL。

预期：

- 得到具体 API 验证错误：先核对 PID、调用顺序和错误级别，判断是应用错误、工具内部调用、能力探测或设备丢失后的连带错误，不能把首条 warning 自动当根因。
- 仍只有 0x887A0020：只说明当前验证输出未提供更早原因，不能证明调用合法或显卡损坏。
- 设备无法创建/验证层缺失等不同错误：这一轮不等同于原始失败，先校验取证条件。
- 开启验证后能启动：诊断层可能改变时序或驱动路径，不直接宣布修复；保留成功条件再分析。
