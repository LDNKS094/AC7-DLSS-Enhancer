# Nsight 捕获停在 30%：只读检查

## 最新反馈：Next Frame崩溃与长捕获能力边界

用户确认Nsight界面存在Next Frame，但操作会导致crash；当前未确认崩溃主体、异常码及时间，不将早期UE4SS访问冲突当成本次原因。已撤回“导出A→Next Frame一次→导出B”要求，不复现、不盲重试。

用户重申此前F11会采非常多帧且无法控制数量，最终阻碍采集/导致卡死。结合下方Frame3062–4424/6,753,807事件记录，应承认**本机实际采入过多帧事件**；缺口是如何受限启动/停止、完整导出所需资源及校验真实相邻性，而不是是否存在跨帧采集行为。异常长范围不等于已有可用的2–16帧采集接口；开始/结束边界异常等仍只是假设。

当前暂停下文fresh会话/F11重试等历史建议。先查已有证据和公开控制入口；有明确帧数上限/停止机制后再制定运行步骤。无需用户重现崩溃或重新抓巨大捕获，也不为此导出675万事件包。

## 用户报告

在一次成功捕获并导出 C++ Capture 后，之后捕获进度反复停在30%；重启一次未解决。尝试在 UE4SS 内禁用 mod 后仍无法捕获。实际 UI 操作阶段/进度窗口文本和精确尝试时间尚未确认。

## 只读证据

- 固态磁盘剩余空间：C:约240 GiB，D:约2.2 TiB，E:约2.4 TiB，F:约515 GiB。磁盘空间不足不是当前首要假设。
- Windows Application Hang 1002 记载 `ngfx-ui.exe 2025.5.0.0` 在本机时间 **2026-10-27 14:01:53** 停止响应，HangType=`Top level window is idle`。同一报告存在Windows Error Reporting AppHangB1记录；它说明Nsight UI发生挂起，但目前没有证据确认百分比进度正是此Hang实例的最后界面。
- 过去24小时另见AC7 Application Error 1000：`UE4SS.dll`，访问冲突 `0xc0000005`，故障偏移 `0x2f5957`，数次同类记录（13:11、13:23、13:56）。这是需要单独考虑的间歇性游戏进程故障，不足以断定为Nsight卡30%的原因。没有读取/复制dump。
- 现场枚举时AC7由当前 Nsight UI PID38316启动；AC7 PID58148报告Responding=true、503线程，工作集约11.38 GiB。模块中含UE4SS.dll、游戏目录dinput8/dwmapi和Nsight Nomad/Interception DLL。用户在UE4SS里禁用mod不等于从进程卸载UE4SS.dll；但加载模块本身不证明此次卡顿由它造成。
- 用户此前已在补丁启用的环境成功捕获；故超宽屏补丁或常驻mod不是必然阻止捕获的静态条件。当前失败具有间歇/状态变化特征。

## 当前判断

最强直接证据是 Nsight UI曾经挂起；尚未定位30%位于帧采集、文件写入、Frame Debugger解析或C++ Capture导出阶段。磁盘容量不是明显根因。UE4SS同偏移多次崩溃提示间歇性进程问题，但不能据此把Nsight hang归咎给补丁或mod。RenderDoc无关，当前路线仍是Nsight。

不要重复重启/捕获来碰运气，不要现在清空缓存、改驱动、停钩子或移除补丁。现有C++ Capture目录已完整留存。若AC7/Nsight当前仍卡顿，不要终止进程或做破坏性清理，先记录准确UI阶段与时间。

## 新一次菜单重试：截图确认多帧

用户报告菜单重试进度等待很久后似乎完成、结果异常，截图随后确认此次确为多帧范围，不只是单帧事件很多：

- 新截图Events窗口显示`Event Count: 2,813,894 / 6,753,807`，最末`Present`为Event6753806。树中至少可见Frame3062起、所选Frame4424；Scrubber约6.75M events。大约跨越1363个连续Frame。
- 之前成功截图只有一个Frame10245根节点、Event Count 6612 / 15085，单帧约15085个事件。
- 因而“新重试比单帧更重，导致长时间进度、高内存，甚至先前UI卡住”是强假设，但还没把6.75M捕获和14:01的旧UI AppHang精确对应，也未证明帧数就是挂死的唯一根因。
- 当前Nsight项目文件仍显示Frame Debugger`activity/fd/capturemode=Manual`、C++`capturemode=Manual`。`activity/pylon/general/framecount=1`属于Pylon，和当前Frame Debugger无关。配置中没找到Frame Debugger frame-count字段。官方[2025.5 Frame Debugger说明](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/after-launch.html)将F11说明为捕获hotkey；[活动选项说明](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/ui-launch-application.html)中的Capture Mode只有Manual或By Frame Index，二者描述触发方式，不是长范围帧数计数。当前截屏顶部仍是Frame Debugger工具栏。
- 仅有此前的C++导出目录，没有这次新捕获的导出文件；不应为调查而导出6.75M events的C++捕获。
- 当前`ngfx-ui.exe`工作集约16.9 GB，AC7约5.5 GB。此高内存与长捕获量相符，但不证明设置变更。无需强制结束当前进程或清理。

## 独立三角probe测试

用户要求用之前的最小D3D11三角probe测试Nsight独立行为。按前次清理范围，原`tools/d3d11-probe/main.cpp`和二进制已经删除；仅保留了RenderDoc capture/XML，不能还原原始C++源码。为避免假称用了原二进制，在`tools/nsight/triangle-probe/`重建了功能等价的无第三方依赖D3D11旋转三角窗体，支持120帧有限自测。普通模式已PASS：120次Present均S_OK。

原始`ngfx.exe` CLI实验：使用`--activity "Frame Debugger" --platform Windows --wait-frames 60`启动行为等价probe，进程PID39680。CLI输出仅证明进程启动成功；用户报告这次CLI启动的probe卡住，之后由用户关闭。后续只读进程核对确认PID39680已退出。该次**没有可用捕获结果**，不能声称CLI自动捕获单帧成功；日志`artifacts/replay/probe-nsight-launch.log`只保存了CLI启动输出。

随后用户在关闭CLI运行后独立启动probe，提供的截图目标为`ac7-nsight-triangle-probe.exe [26464]`、Nsight Frame `#108`、Event Count `5 / 5`。列表内容是Start of Capture、ClearRenderTargetView、UpdateSubresource、Draw(3顶点)和Present(S_OK)。这张独立截图确实是单帧、5事件的有效证据；它与CLI失败是两次不同运行，不可混为一谈。用户确认独立probe也是通过F11捕获，因此同一Nsight界面+F11在probe上能得到单帧、AC7上却得到约1363帧。

进程核对：当前Nsight UI PID38316，用户独立启动的probe PID26464为其子进程；CLI PID39680不在运行。另见Ace7Game PID41180，非CLI启动；未操作/结束任何进程。

结论：独立probe和AC7使用同一F11后结果不同，故hotkey本身不是普遍“多帧”开关；当前AC7 675万事件现象应是目标进程与Nsight会话/钩子交互的间歇异常，具体根因未知。CLI启动方案曾卡住，不重复使用。

## F11为什么会受目标程序影响

F11是Nsight的触发命令，但**不是host端对当前画面做一次截图**。Nsight会把捕获/分析接口带进目标程序，并在目标内拦截D3D11/DXGI调用；Nsight host UI负责发起请求、接收数据和分析。目标进程产生的Draw、Update、Present及其顺序/线程/交换链状态，就是可被采集的内容。Nsight Frame Debugger的HUD与host操作也保持同步，见[官方HUD说明](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/frame-debugger-ui.html#application-hud)及[F11触发说明](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/after-launch.html#target-application-capture-hotkey)。

因此应用可以影响：Nsight何时观察到热键/触发请求、触发落在哪个Present边界、当时有多少线程/交换链工作在进行，以及采集内容规模。Capture Mode=Manual只说明“用户手动触发”，不是捕获帧数配置；单帧Frame Debugger预期读取一个帧事件序列。应用不会因为“自己想要多帧”就有权限随意改写Nsight设定；除非程序主动调用捕获SDK（当前没有证据AC7这样做）。

用户已用F11证明同一probe为1 Frame/5事件；AC7此前也有一次1 Frame捕获。因此AC7并非恒定地要求多帧，且高帧率本身不是足够解释。1363个连续Frame更像某次捕获请求的**目标侧开始/结束边界或host-target会话状态没有按预期收束**，或存在重复/延迟请求；这是基于现象的候选机制，不是已证明的根因。也可能是Nsight本身的间歇错误。

下一次有鉴别力的对照不是改一个frame-count（Frame Debugger无该选项），而是等用户安全结束当前分析后，以**全新Nsight UI会话**启动AC7，确认仍为Frame Debugger/Manual，只按一次F11，并看它是否只收一个Present帧。这样可测试是否是之前一直复用的Nsight会话状态；如果新会话仍捕获上千帧，则加强AC7与注入路径交互异常的假设。测试前不改补丁/UE4SS/其他选项。

## 公开类似案例检索（2026-10-27）

### 与“捕获进度挂起”最接近的公开案例

NVIDIA官方开发者论坛有一组相关报告：

- [Nsight Graphics capture progress stuck at 55%](https://forums.developer.nvidia.com/t/nsight-graphics-capturing-frame-please-wait-the-progress-bar-gets-stuck-at-55/313179)：2024-11-13用户报告 UE5 + Vulkan + Ray Tracing 下捕获进度卡55%。
- 该贴链接的[Frame Debugger stuck at “Processing object mutations”](https://forums.developer.nvidia.com/t/frame-debugger-stuck-at-processing-object-mutations/319908)：UE5.5.1 DebugEditor、Third-Person模板、Vulkan SM6 PathTracing、RTX3060、Nsight Graphics 2024.3、driver566.36；编辑器/Play模式的PathTracing都卡55%，切到Lit view mode可以抓帧；开关Driver Instrumentation、Force Single-Threaded Capture、Automatic Connect均未改变失败。NVIDIA版主2025-01-20表示可在标准UE5第三人称模板复现，并建立内部case。
- 公开页面检索到的后续没有确认修复版本/根因。因此这是“相似的Frame Debugger捕获进度挂起/特定渲染路径”的强先例，但**不是**AC7的相同故障：API/引擎、Nsight版本、渲染路径、进度点均不同；未见该case讨论帧数被收成约千帧。

### 官方说明和多帧选项边界

- [Nsight Graphics Troubleshooting](https://docs.nvidia.com/nsight-graphics/UserGuide/troubleshooting.html)列出的live-analysis挂起候选包括lazy presentation、multithreading、fullscreen；通用建议为Force Repaint、Replay Thread Pause Strategy改RenderOnly，必要时采集hang dump。这些是通用排查清单，不是AC7特定修复；本机这次为windowed，且独立probe已捕获成功，不能直接照单全改。
- [2025.5 activity options](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/ui-launch-application.html)的Frame Debugger Capture Mode是Manual或By Frame Index（触发方式），当前项目为Manual；无独立Frame Debugger“多帧数量”设置。
- 该版支持显式多帧的是不同活动：Graphics Capture有`--frame-count`；GPU Trace有自己的Start/Limit Frames。2025.5 Graphics Capture主要服务D3D12/Vulkan；AC7的D3D11是在Frame Debugger中分析，不能把另一活动的多帧参数误当成其设置。
- [Nsight Graphics 2025.5 release notes](https://developer.nvidia.com/nsight-graphics/getting-started/release-note-v2025.5)中未检索到通用Frame Debugger多帧或30%挂起修复/已知问题；release note未提到不等于无此类bug。

### 和本机结果合并

本机用户独立启动的D3D11 probe截图确实是单Frame/5事件，AC7出现约1363个连续Frame/6.75M事件则不同。因此“Nsight对任何目标/默认设置一律收多帧”不成立。现有证据更支持AC7进程/调用流或会话触发路径有差异；CLI启动probe的尝试也卡住过，但与用户独立probe截图是两次不同运行，不能把CLI结果混为成功样本。公开资料未找到AC7 D3D11、Nsight2025.5、30%进度、同类多帧事件范围的精确匹配报告。

**结论：** 有公开、NVIDIA版主已复现的“Frame Debugger卡在特定捕获阶段”先例，但未见和本机完全相同的AC7/D3D11/多帧症状或已发布修复。最有鉴别力的本机事实仍是独立probe单帧成功而AC7多帧；这把问题范围压到AC7路径/触发状态，暂时不能归咎于超宽屏patch、UE4SS或某个全局frame-count设置。

## 保全

本次只读调查未结束/关闭进程，未清缓存、修改Nsight配置、游戏文件、驱动、注册表或系统设置。当前进程是否仍挂住，尚未确认。