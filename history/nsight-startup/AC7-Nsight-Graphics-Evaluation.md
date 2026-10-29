# Nsight Graphics 对 AC7 的适用性评估

> 历史记录：Nsight启动资格与首帧导出阶段，非当前操作计划。后续两份C++ Capture、GPU旁路及正式NR查询已完成；当前状态以 [项目README](../../README.md) 和 [API核对](../../docs/AC7-Graphics-API-Support.md) 为准。下文进程ID、等待导出和恢复步骤均为当时现场。

## 最新本机状态

用户已安装并打开 2025.5.0，卸载注册信息 DisplayVersion=25.5.25335.2111，UI PID64592、路径 `D:\Application\Nsight Graphics 2025.5.0\host\windows-desktop-nomad-x64\ngfx-ui.exe` 已核对。先前“未安装”是评估时快照，现已更新；实际基础捕获和AC7捕获仍待验证。下一步见 [最小首测步骤](AC7-Nsight-First-Test.md)。

## 结论

**有官方支持依据，值得进行一次有边界的实测；推荐候选为 Nsight Graphics 2025.5 Windows x64 的 Frame Debugger。** 尚未安装、启动或捕获 AC7，不能宣称其可以避开 RenderDoc 的设备丢失。

选择 2025.5 的依据是该版本文档明确包含 D3D11 API Inspector、满足 Blackwell 支持起点及当前驱动最低门槛。不是宣称它是最后一个支持 D3D11 的版本，也不是已知 AC7 修复版。

本轮仅做官方资料检查和本机只读检查，未下载安装包、修改驱动、注册表、游戏或 Steam 设置。

## 已找到官方安装包直链

此前历史页主要展示发布说明，不能当成直接下载入口。现已从 NVIDIA 官方下载目录 `https://developer.nvidia.com/tools-downloads.json` 精确筛选 Nsight Graphics 2025.5.0 的 Windows 文件，地址为：

https://developer.nvidia.com/downloads/assets/tools/secure/nsight-graphics/2025_5_0/windows/NVIDIA_Nsight_Graphics_2025.5.0.25335.msi

官方目录入口可用：https://developer.nvidia.com/tools-downloads#?search=Nsight%20Graphics

HTTP HEAD 已验证：跳转到 NVIDIA 官方下载 CDN 后 200，application/octet-stream，Content-Length 445820928（约425.2 MiB）。最终CDN跳转带临时签名，不保存或分享临时URL；以上稳定developer入口供用户使用。本轮未下载完整MSI，因此未验证文件数字签名和内部版本，安装前仍需核对。

## 本机前提

- GPU：NVIDIA GeForce RTX 5080。
- `nvidia-smi --query-gpu=name,driver_version --format=csv,noheader` 实测驱动为 **617.42**，与此前文件版本 32.0.16.1742 一致。
- 目录和卸载注册表中未找到 Nsight Graphics。已有 Nsight Compute 2025.2.0、Nsight Systems 2025.1.3、Nsight Visual Studio Edition 2025.2；这些不能替代独立图形帧调试器。
- AC7 已观察到实际 D3D11 设备/呈现，主 EXE x64。原生颜色/深度/运动向量/常量调查需要逐 draw、资源绑定和中间目标查看能力，不只是 GPU 使用率曲线。
- 最小 D3D11 三角形程序已存在，可用作安装后的第一道实际资格检查。

## 官方证据

### 1. RTX 5080 / Blackwell

GPU 清单明确列出 GeForce RTX 5080：

https://developer.nvidia.com/nsight-graphics-gpus-full-list

2025.1 发布说明明确“fully supports Blackwell GPUs”，并列出 Capture/Replay 等能力：

https://developer.nvidia.com/nsight-graphics-2025_1

这支持 2025.5 的硬件候选资格，不代表每个 API 模式或 AC7 均已验证。

### 2. 版本和活动必须选对

2025.2 官方说明：Graphics Capture 面向 Vulkan/D3D12，Frame Debugger 继续支持 OpenGL 和 D3D11：

https://developer.nvidia.com/nsight-graphics-2025_2

2025.5 的 Frame Debugger 手册明确包含 **D3D11 API Inspector**，可检查纹理、渲染目标、着色器及相关常量；API Inspector 还能导出结构化 JSON：

https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/frame-debugger-ui.html

其 Frame Debugger 工作流支持逐事件、管线状态、资源值/依赖、Current Target 和 C++ Capture：

https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/frame-debugger-overview.html

当前 2026.3 的 **Graphics Capture** 文档则明确仅支持 D3D12 和 Vulkan；另一个活动已命名为 OpenGL Frame Debugger。不能仅凭品牌首页泛称支持 D3D11，就给 AC7 选择最新 Graphics Capture：

https://docs.nvidia.com/nsight-graphics/UserGuide/graphics-capture-overview.html

https://docs.nvidia.com/nsight-graphics/UserGuide/frame-debugger-overview.html

本轮没有取得足以精确断言“D3D11 在某个 2026 版本彻底移除”的证据，因此不作这种结论；使用有明确 D3D11 文档的 2025.5 是更稳妥的评估候选。**GPU Trace 不用于替代 D3D11 帧资源检查。**

### 3. 驱动条件

2025.5 官方发布说明：Windows 最低 **581.80**，本机 **617.42** 高于这个门槛，无需为了满足该最低要求换驱动：

https://archive.docs.nvidia.com/nsight-graphics/2025.5/ReleaseNotes/index.html

“高于最低门槛”不等于已经证明旧工具与未来驱动的每项功能兼容，需要基础样例和实际目标验证。当前 2026.3 要求 Release 615 或更新，但这不解决其活动/API选择问题。

### 4. 启动、Steam 与 Attach

2025.5 官方支持 x64 应用和启动 x64 应用的 x86 启动器（举例 Steam），可沿被启动的进程树识别图形进程：

https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/launch-application-overview.html

Attach 页面要求目标此前经 Launch 页面启动，列出该应用及子进程。不能把它解释为向任意已初始化的 AC7 晚注入：

https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/ui-launch-application.html

AC7 已有 App ID 文件/直接启动正常的对照，因此首测优先由 Nsight 直接 Launch AC7、明确工作目录、保留已验证的 Steam 登录状态和 App ID 502500。不先改为向已运行的 Steam 注入。若再次发生启动转交，应核对实际 PID/模块，不假定已运行的 Steam 服务启动的进程一定被跟踪。

### 5. 必须避免叠加调试层和包装器

2025.5 Known Issues 明确：

> Applications that intercept DirectX devices or objects by use of a shim object are not supported.

> Nsight Graphics is incompatible with the debug runtime in all versions of Direct3D.

因此首测保持 mod 隔离，不加载 RenderDoc DLL、ReShade 或其他 D3D 包装器；**不使用先前 Launch-AC7-D3D11-Validation.cmd**，不启用 D3D11 debug runtime，不运行 ProcDump。启动 Nsight UI 本身和它注入的配套组件不是“同时注入 RenderDoc”。

Frame Debugger 基础资源查看不要求为性能计数器先开放管理员访问；如后续确需 Shader Profiler/计数器，再针对实际报错检查权限，不能提前要求修改系统性能计数器权限或以管理员运行 Steam。

## 对原生输入调查的价值和限制

若能实际捕获 AC7，D3D11 管线与资源检查足以支持第一阶段：查 pass 顺序、无 UI 的场景目标、深度/速度候选、常量绑定及资源格式/尺寸。这些是后端选型的必要证据。

但工具不会自动把某张纹理认定为真实运动向量，也不会自动证明曝光、jitter、相机或历史重置信号的语义。仍需相邻帧/相机运动等对照。C++ Capture 或资源导出格式也不能假定能直接当 RenderDoc .rdc 使用；首轮可在工具内保留帧，导出关键描述和资源证据，不先搭建自动化接口。

## 下一阶段的最小实测门槛

安装来源应为 NVIDIA 官方的 **2025.5 Windows x64 历史发行渠道**；官方历史入口：

https://developer.nvidia.com/nsight-graphics-history

发布页：https://developer.nvidia.com/nsight-graphics-2025_5

本轮只核实文档/发行记录，未取得并验证该版实际 MSI，也未核实下载是否需 NVIDIA 账号；不能把指向当前新版的下载按钮当成已核对的 2025.5 安装包。安装前核对文件版本、数字签名和安装组件；无需为此安装 CUDA Toolkit 或完整 VS 插件。

测试分两道门：

1. **Frame Debugger 捕获现有最小 D3D11 程序。** 普通运行、不加 `--self-test`，让窗口持续绘制；该程序只在检测到 RenderDoc 注入且 self-test 时触发其 API，因此普通运行不会向 Nsight 发 RenderDoc 捕获调用。确认看到 Draw、目标纹理、shader 和常量；失败则停止 AC7 测试。
2. **一次无 mod AC7 首测。** 用 Nsight 2025.5 的 Frame Debugger 直接 Launch，EXE/工作目录沿用已验证路径，参数先固定为现有 `-windowed`，不继续叠加720p/改画质；记录该基线，不将它与720p试验混作相同条件。不启用 D3D debug runtime。先到菜单并确认连接，再进实际飞行捕获一帧，检查内容是否可读。

若遇与 RenderDoc 同类设备丢失或明确 API 不支持，保存一份日志和对应 PID 后停止，不立即换驱动、系统设置或另一批版本。只有实际捕获和资源检查通过，才把 Nsight 标为本项目可用工具。

## 当前现场

额外 mod 仍在项目备份目录，临时 `steam_appid.txt` 保留；所有恢复路径见 [RenderDoc 阶段收束](../renderdoc/docs/AC7-RenderDoc-Stop-Decision.md)。本轮不改变该状态。后端和原生接入架构仍未选定。
