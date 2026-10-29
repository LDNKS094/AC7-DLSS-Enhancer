# AC7 原生 DLSS 架构选型调查

> **ASI复制与View协议**：[私有deferred复制](AC7-Neutral-ASI-Ordinary-Fix.md)及[真实View上传/guide/loader](AC7-View-Identity-ASI-Validation.md)已过独立普通/debug矩阵。已准备默认关闭观察包，下一步用户实际游戏日志确认主View/启动时序；仍未验实际游戏复制/模型/性能，不扩成自研FG。

> **ASI门槛更新**：[debug实际hook验证](AC7-Neutral-ASI-Validation.md)通过加载/原draw8/中性2/像素状态/停止，普通设备hook仍不合格，无可部署游戏包。先解决该具体阻塞，不把debug资格等同游戏接入，也不默认回到ReShade或深改工具链。

> **中性宿主进展**：[独立D11中性入口](AC7-Neutral-Host-Validation.md)已通过全scene/crop、管线快照/UAVcounter/原CS继续、拒绝与stop-drain，hook0/model0。用户要求最直接实现优先、不以模组兼容前置；下一步ASI窄hook合成验证，再实际view/guide/token关联，遇具体冲突再提可恢复清理清单，不部署/启动游戏。

> **同通路比较完成**：[对照报告](AC7-Route-Comparison-Validation.md)记录direct40阶段＋8返回逐位、ABBA各64有效样本：NR/SR Evaluate CPU约0.388/0.318ms，D12颜色/模型GPU约4.031/4.003ms，不能转换游戏FPS。工程建议下一固定NR/SR优先直接NGX，保留OptiScaler作FG/压缩候选，完整宿主未选。Opti legacy菜单30帧后状态泄漏与logger配置勘误已保留并在隔离host绕开，无源码patch。最新Opti累计NR198/SR218，直接本阶段88/88。

> **提交后实测**：自有共享桥＋OptiScaler公开D3D12入口在FL11.0输入上通过[持久八帧历史/Reset](AC7-OptiScaler-Sequence-Validation.md)，16输出对直接NGX逐位、64输入保持、双API debug0、完成后清理正常；累计NR0/SR21。[公开NR与D3D12整链](AC7-OptiScaler-GPU-Chain-Validation.md)已通过独立NR及同queue/list真实八帧历史/SRReset，80阶段逐位/64输入保持/debug0，后续[原始HDR共享整链](AC7-OptiScaler-Shared-Chain-Validation.md)完成单桥正常40阶段＋8返回、deadline/late-drop/双Reset/stop与D11退休，Opti累计NR47/SR67；当前转向同通路路线成本比较，在线guide/游戏回调未接，路线未选。无FG/性能/游戏验收。其D3D11包装层FL11.0恢复/Shutdown1问题仍保留，详见[首轮接口](AC7-OptiScaler-Interface-Validation.md)。提交仍81021a0，之后改动未再提交。

> **用户最新决策**：仅比较路线1“薄AC7适配＋OptiScaler”和路线2“自研接入＋直接NGX先NR/SR”；路线3完整自研FG暂不考虑。暂停ReShade专用原型，不把其采样成功当最终路线。先独立验证OptiScaler导出/生命周期/真实输出及HDR链兼容，再比较复制同步和复用成本。详见[路线决策](AC7-Integration-Route-Decision.md)。下方ReShade实施建议是此前提案，已暂停。

> 当前第一原型范围已确定：复用已有ReShade公开API20回调，先做默认关闭、固定尺寸、不加载模型的中性回接与游戏smoke test。完整NR/SR/FG后端仍未选定。独立固定会话所有权与实际特性重建已通过；首次中性测试不等待动态resize完成。详见[最小游戏测试路径](AC7-Minimal-Game-Test-Path.md)与[会话验证](AC7-Fixed-Runtime-Ownership-Validation.md)。下方为历史调查，旧“相邻帧未取得/无模型桥”不再表示当前进度。

> 最新[prewarp新配对链](AC7-Prewarp-NR-SR-Validation.md)：465后/466前新GPU代理→新NR→GPU恢复HDR→新SR(P1/Reset)→原warp/后处理/UI已离线通过，旧postwarp结果未混用；FXAA可保留/有效区RGB旁路。下一门槛真实连续帧/jitter/history，不提前选宿主/桥，不把单Reset成功当高输出或时域验收。

> 最新[SR同尺寸离线回接](AC7-SR-Writeback-Validation.md)已完成NR→恢复HDR→SR(P1/Reset)→原空间AA/后处理/UI→最终帧，旁路12项逐位、区外0变化、UI逐位。用户确认菜单只有FXAA，无TAA选项；本轮保留FXAA，后续旁路对照，再验证候选位置与guides/连续帧。P调查延后，仍未实时接入、高输出或选定宿主/桥。下文旧“未SR回接”按历史阶段读取。

## 1. 结论与决策状态

**选型待定。此前“源码级首选 OptiScaler”的表述提前下了结论，现撤回。** Nsight实测已确认AC7使用D3D11，两份C++ Capture重放及最终图像核对通过。最新正常飞行样本确认场景/HUD分离、UNORM物体速度及深度/矩阵相机回退；该样本无补丁、有效区域中央2560×1440，见 [当前运行证据](AC7-Nsight-Normal-Flight-20261028.md)。相邻帧对应、颜色曝光和最终全幅适配仍待确认。下文保留各轮源码调查时的范围，不能把历史“尚未实测”描述当作当前状态。

最新 [SR Reset单帧诊断](AC7-SR-Reset-Diagnostic.md)：原始/NR后HDR在显式独立域PreExposure=1假设下同尺寸HDR/DLAA求值、GPU完成和读回通过，输出有限并保留>1；峰值166→102.8125及局部变化未验收，不回写SR结果。游戏真实pre-exposure和时域仍未知，原生接入门槛保持；不再把它等同“所有诊断禁止”。生产链覆盖扩大到156writer/33唯一PS统计及末两级但未证明预曝光。下一步解释重建/高光变化并定位真实曝光/view语义，再提出必要的具体采样要求，不深改工具链。

新增 [颜色曝光与后端门槛](AC7-Color-Exposure-Backend-Gates.md)：按AC7实际输入对照接口，记录OptiScaler资源桥/Pre-SR链的可复用范围，但不预定为下一项实施。曝光应用通道和HDR范围已有GPU读回证据，pre-exposure与时域仍未知。

新增参考 [Overglaze](AC7-Overglaze-Reference.md)：已核对固定提交的DX12 Post-SR NR实现，并运行183项独立WARP检查。优先参考深度单平面转换、颜色/曝光和生命周期；它不提供AC7 D3D11资源桥、Pre-SR周边压缩、SR或FG，当前不选为完整宿主。

保留 AC7 适配＋OptiScaler、Luma 内扩展、Magpie 扩展等候选。OptiScaler 已有较完整的相关处理代码，Luma 有 TAA 识别基础，但这些都不是在 AC7 上可用或总代价更低的证明。

必须先取得下列前置证据，再排序候选并确定原型：实际设备/API 与交换链、TAA/后处理/UI 阶段、原生颜色/深度/速度及元数据来源、SR 输出回接。之后再核对候选的输入契约和 FG 条件。完整路线还必须通过：

1. AC7 能可靠提供正确的场景颜色、深度、速度、视图/曝光和重置信号。
2. 适配器可经 NGX 入口驱动目标后端，并取得真实成功输出；不能只相信统一返回码。
3. 低分辨率输入、高分辨率输出可接回 AC7 的后处理与 UI 阶段。
4. FG 交换链路径与显式/经验证的 HUDless、UI 和相机数据可在 AC7 正确工作。

若前三项通过、第四项失败，仅能宣布 NR/SR 路径成立；需再判断局部修复 FG 还是更换呈现路线，不重写已成立的部分。

## 2. 本轮先更新的计划

需求与门槛已先写入 [原生需求及计划](AC7-Native-DLSS-Requirements.md) 第 7.1 节，然后开展本轮调查。

目标为普通屏幕下的原生数据接入、Pre-SR 周边压缩 NR、SR 和 FG；优先复用现有实现，只开发 AC7 适配和必要的窄接口。本轮不安装注入器、不修改游戏目录或既有配置、不启动模型求值。

调查内容：

- 查看标准导出输入和每帧资源传递，而非仅对照功能列表。
- 追踪 DX11→DX12 的处理和结果回接。
- 分开确认 NR/SR、FG 输入以及交换链/呈现三条路径。
- 比较 Luma 与 Magpie 的额外改造面。
- 执行不依赖游戏的周边压缩 CPU 映射测试。
- 明确哪些仍需游戏实机证据。

## 3. 检查的源码与本地材料

| 项目 | 本轮使用的版本/材料 |
| --- | --- |
| wilsjo2 OptiScaler 分支 | `6125a75b37f7e2a408e31934f4827e5660429203` |
| Luma | 原有 `dc471fa590c24a12a36be3e527ec45d2595466bd` 加本地 8 行求值日志 |
| Magpie Fork | 用户下载包 0.6.11 对应 `4fc74a196993c883ce2f01e96e8ed8d81f1490e6`，此前已与本地关键源码核对 |
| AC7 | 只读导入表检查；游戏当时未运行，没有运行 API 观察结论 |

仓库：

- <https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass>
- <https://github.com/Filoppi/Luma-Framework>
- <https://github.com/SAOG0721/Magpie>

本轮未完整构建 OptiScaler，不宣称其发行程序已具备 AC7 兼容性。

## 4. OptiScaler：已存在的数据入口，不必先拆内部算法

### 4.1 输入入口

`OptiScaler/inputs/NVNGX_DLSS_Dx11.cpp` 提供 NGX 初始化、参数和 Feature 创建/求值入口。`NVSDK_NGX_D3D11_EvaluateFeature` 将参数送入所选 Feature 后端。

FSR2 DX11 适配已有把其他输入转成该契约的先例：`OptiScaler/inputs/FSR2_Dx11.cpp` 会传递颜色、深度、速度、曝光、jitter、reset、尺寸，以及 `FSR.cameraNear/cameraFar/cameraFovAngleVertical` 等元数据，再调用 NGX 入口。

因此 AC7 可以作为新的数据生产者：

```text
AC7 的 TAA/重建替换点
  → 适配器规范化原生纹理与元数据
  → 现有 NGX 输入协议
  → OptiScaler 选定后端
  → 适配器提供的输出纹理
  → AC7 后续处理
```

这里复用的是既有输入协议，不是伪造截图或靠自动扫描让 OptiScaler 自己找到所有 AC7 数据。

### 4.2 加载与初始化不是零工作

`OptiScaler/dllmain.cpp` 识别 `OptiScaler.dll` 和 `.asi` 加载模式，但源码注释称相关逻辑为 testing hack。它不是已承诺稳定的独立插件 SDK。

适配器还要核实：

- 从实际 DLL 获取哪些导出，NGX 初始化/销毁由谁负责。
- 避免把请求重新转回自己造成递归。
- 后端在交换链创建前是否已加载，FG 配置何时生效。
- 不同版本参数、Feature flags、全局设备/上下文状态的兼容性。
- 功能失败是否真正传播给适配器。

特别是 `NVSDK_NGX_D3D11_EvaluateFeature` 的部分分支即使未执行或后端切换，也会返回 Success。原型必须检查实际后端状态、输出和调试证据，不能以返回码单独验收。

### 4.3 适配器输出仍要接回游戏

OptiScaler 能写出指定的 SR 输出，但不会替适配器完成 AC7 后续 pass 的视口、纹理尺寸、采样常量和原空间放大路径调整。这是任何候选都需要完成的 AC7 工作。

首个原型先使用正确的同尺寸 DLAA 验证协议及资源语义，再单独验证真正超分。DLAA 成功不是最终目标，也不代表输出回接问题已经解决。

## 5. OptiScaler：NR 和周边压缩位于实际处理链中

### 5.1 DX11→DX12 已有结果回传

`OptiScaler/upscalers/dlss/DLSSFeature_Dx11On12.cpp` 将 DX11 Feature 包装到 DX12 DLSS Feature。

`OptiScaler/upscalers/IFeature_Dx11wDx12.cpp` 的 Evaluate 路径：

1. 保存相关 DX11 绑定。
2. 准备/共享颜色、深度、运动和输出等资源。
3. 用 DX12 对应资源暂时替换参数。
4. 调用 `dx12Feature->Evaluate(...)`。
5. 恢复 DX11 参数指针，提交并同步 DX12 工作。
6. 调用 `CopyBackOutput()` 回写 DX11 输出。
7. 恢复图形状态。

因此不必为 NR 从零编写这条 GPU 互操作路径。但资源格式、视口和同步开销仍须在 AC7 验证。

### 5.2 Pre-SR NR 有实际调用位置

`OptiScaler/upscalers/IFeature_Dx12.cpp` 持有 NR 与 SR 共用输入，判断 Pre-SR 配置和有效性，在执行 `EvaluateInternal` 前准备 NR 增强后的颜色，之后恢复原参数。

普通 Pre-SR 条件没有直接排除 interop，因此有连接上述 DX11→DX12 路径的代码基础。不能将某个更特殊的 native-DX12-only 模式与普通 Pre-SR 混为一谈。

不过 Pre-SR 仍可能因输入不符合条件而降级/旁路。必须记录实际阶段，而不是只读取 `RunBeforeSR=true`。

### 5.3 周边压缩已有实现与测试

`OptiScaler/shaders/dlssnr/DlssNr_Spatial.h`、空间 shader 和 NR 求值代码提供周边空间布局与处理基础。其工作范围包括颜色与引导数据的对应变换，不是单纯给输出边缘加模糊。

已在本机编译并运行上游 `tests/nr_spatial_mapping_smoke.cpp`：

```text
PASS: NR peripheral spatial mapping, bounds, motion constants, bypasses
exit code: 0
```

该测试包含映射往返、单调性、边界/无效值和旁路等断言。它证明所检查 CPU 契约测试通过，不证明 NVIDIA 推理画质、GPU shader 或 AC7 时序正确。

复现入口与日志：

- [CPU 测试启动脚本](../../research/architecture-selection/run-spatial-cpu-check.cmd)
- [测试输出](../../research/architecture-selection/spatial-cpu-check.log)
- [本轮证据与未执行项](../../research/architecture-selection/selection-evidence.json)

## 6. OptiScaler：FG 已有桥接，HUD 与相机仍需显式验证

### 6.1 不只是“有一个 FG 类”

`OptiScaler/inputs/FG/Upscaler_Inputs_Dx11wDx12.cpp` 从超分输入准备/复用深度与速度的 DX12 资源缓存，并设置 jitter、reset、相机参数、帧时间和插值区域。

`OptiScaler/hooks/DxgiFactory_Hooks.cpp` 在满足 `FGInput::Upscaler` 等条件时创建：

- 游戏使用的隐藏 DX11 交换链；
- 面向原窗口的 DX12 FG 交换链；
- `Dx11wDx12SC` 包装层。

`OptiScaler/hooks/FG_Hooks.cpp` 再根据输出后端创建 DLSSG、XeFG 或 FSRFG Feature。说明已有从 DX11 游戏导向 DX12 呈现的代码，而不需要先把整个游戏改成 DX12。

### 6.2 不能省略的限制

- MSAA、格式、交换链模式和创建时机均有限制。
- FG 交换链创建失败时，代码可能改用普通 DX12 交换链继续运行；看到包装成功不代表 FG 真的工作。
- FG 相机路径会读取扩展参数，也存在配置默认值回退；AC7 适配器必须填入真实且已验证的数据。
- HUDless 路径包含 `Hudfix_Dx11`、资源跟踪与候选判断。它们有助于支持普通游戏，但不能据此证明 AC7 的 HUD 分离已达标。
- 适配器是否可通过现有协议明确提交正确 HUDless/UI 数据，或需新增一个很窄的后端入口，是首轮原型的重要决策点。

所以 A 路线最大优势是已有大部分基础设施；最大未决点是 AC7 的回接及确定性的 FG/UI 数据，而不是模型数学。

## 7. Luma：适合作为取数参考，但整条链复用较少

已检查的通用 Unreal 模块包含：

- TAA shader 特征分析、绑定资源识别；
- 颜色、深度、速度及视图常量的取得；
- 速度解码与 NGX SR 调用；
- 图形状态保存、恢复及诊断能力。

这些恰好覆盖 AC7 原版没有现成超分入口时的难点，值得保留为原型或适配器参考。

但目前通用路径仍标注 `DLAA only`，按原 TAA 纹理尺寸处理，使用部分通用曝光/相机约定。它不是已验证的 AC7 取数模块。

若把 Luma 选作整个处理宿主，还需引入 Pre-SR NR、颜色/曝光转换、周边 NR、DX12 互操作、FG 和呈现代码，或把其他项目组件搬进来。相比调用 OptiScaler 既有协议，这一方向的后端改动面更大。

不能直接照搬所有 Luma shader 判断到独立适配器并宣称准确；需要固定 AC7 版本/实际 shader 与资源证据。也不必为复用一段取数逻辑就加载完整 Luma 和另一套后端；具体加载组合留待原型验证。

代码复用边界还包括许可证：所检查 Luma 使用 Custom MIT，附有署名与商用需作者许可的条款；OptiScaler 为 GPLv3。不能把前者当作标准 MIT 而默认可直接合并发布。本轮只做源码调查和既有测试，未将 Luma 源码并入 OptiScaler；适配器可以先使用采样证据确定 AC7 专用契约，再决定实际复用范围。

## 8. Magpie：可以扩展，但“新增捕获源”不足以满足原生输入

已检查本地 Fork：

| 边界 | 现状 | 原生接入需要增加 |
| --- | --- | --- |
| CaptureMethod / FrameSourceBase | 窗口/桌面/GDI/DWM 画面入口 | 原生共享帧输入和返回通道 |
| NativeEffectDrawContext | 图像、HDR、帧身份及 FrameGuidance | 原生 jitter、曝光、相机及 UI 的完整契约 |
| FrameGuidanceService | 零深度及光流等 provider，消费者尺寸适配 | 原生深度/速度 provider，严格帧关联和无错误旁路 |
| DLSS SR 后端 | 捕获图像语义，当前求值 jitter 为零 | 使用原生 jitter、flags、有效区域和曝光 |
| NR 颜色处理 | 为捕获图像/效果链建立的处理语义 | 引擎线性/预曝光场景的正确转换与反变换 |
| 图像输出 | 外部窗口呈现 | SR 返回游戏继续后处理；FG 的后续输入再送回或重新分配职责 |
| FG | 既有外部呈现路径 | 游戏后处理结果、无 HUD/UI 的同步和对应关系 |

因此外部化可以复用部分后端和 UI，但增加了两次渲染边界交接，还需要改动多个内部契约。共享纹理避免 CPU 读回不代表没有 GPU 复制、fence 等待和资源重建。

若把 Magpie 后端搬到游戏进程内，虽然不再跨进程，但也不再是“原版 Magpie＋单个游戏接口”，需要维护拆分/集成工作。这些是待比较的新增工作，不足以在 AC7 前置证据缺失时给出最终排序。

## 9. 候选对比

以下是根据已确认模块缺口的定性比较，不是估算代码行数或开发工时。

| 项目 | A：AC7 适配＋OptiScaler | B：Luma 内扩展 | C：扩展 Magpie 外部处理 |
| --- | --- | --- | --- |
| AC7 干净取数与数据校验 | 必须新适配；可参考 Luma | 通用基础较多，仍需适配 | 必须新适配 |
| 现成原生参数输入 | NGX/FSR/XeSS 协议 | 内部 Feature/DrawData | 捕获语义，需扩展 |
| Pre-SR NR＋周边压缩 | 已有具体实现 | 需接入/移植 | NR 已有，原生数据与周边路径仍需核对/接入 |
| DX11→DX12＋输出回传 | 已有桥接 | 需引入 | 有互操作基础，跨进程原生回传需新增 |
| FG＋交换链 | 有具体 DX11 包装路径，AC7/UI 未验证 | 需引入完整后端/呈现 | 外部 FG 可复用，原生回接流程需重构 |
| 自己维护的主要新增部分 | AC7 适配和可能的窄协议扩展 | AC7 适配＋多项后端集成 | AC7 适配＋IPC/资源/元数据＋呈现衔接 |
| 当前定位 | 候选：协议与处理链较完整 | 候选：TAA 取数基础较多 | 候选：需扩展原生契约及回传 |

Cheeky 同样需要已有 NGX/SR 数据来源，其额外的 SR 分区与多种渲染路径并非本次必须项。暂未发现它能减少 AC7 必需适配或比 A 路线更直接提供全部目标功能的证据。

从零实现新的完整后端未显示出比 A 更小的必要改动量，不列为首轮路线。

## 10. AC7 只读观察与未决项

对本机 `Ace7Game.exe` 进行了导入表检查。表中同时有 DXGI、D3D9、D3D11、D3D12 名称；**导入表不是实际运行 API 的证明**。检查时游戏未运行，因此不把默认 DX11 假设升级为当前设备观察结果，也没有尝试启动参数切换。

游戏根目录已有 `dinput8.dll`、`dwmapi.dll` 及两个超宽屏 ASI。将来原型必须明确加载链和 hook 共存，不能直接覆盖这些文件。

以下内容仍需实机/独立原型：

- 实际 API、交换链描述、创建时机和真实设备。
- TAA、原空间放大、色调映射和 UI 的真实顺序。
- 颜色/深度/速度/视图常量语义及帧对应。
- 高分辨率 SR 输出的后续纹理和采样常量回接。
- 通过标准入口调用时的全局状态与设备生命周期。
- FG 的确定性 HUDless/UI 与相机输入，实际生成帧及节奏。
- 与现有超宽屏及 UE4SS/输入相关组件的共存。

## 11. 前置调查、后续原型及停止条件

### R0：先取得 AC7 运行事实，再做选型

记录真实运行的设备/API、设备适配器、交换链描述、实际设置与启动参数。DLL 导入或模块加载只作为辅助线索，不能单独证明正在使用的渲染后端。

在代表性飞行场景中定位 TAA、放大、色调映射和 UI，导出颜色/深度/速度及对应常量、资源区域和调用位置。随后再比较各后端可接受的输入与可回接的位置，并排定原型顺序。

补查时 AC7 未运行；游戏用户目录的 Logs 文件夹为空，现有 UE4SS 及超宽屏日志未提供可用设备/API 或交换链记录。因此 R0 尚未完成，当前不做最佳选型声明。

以下 P0–P4 是完成 R0 并选定候选后可采用的验证层级，不表示已经选择 OptiScaler 或开始实施。

### P0：离线验证输入边界

在与游戏相同 API/设备要求的最小宿主中验证 OptiScaler 导出、初始化、Feature 生命周期、明确的输入/输出资源，以及真实失败/输出状态。先不加入 AC7、NR 和 FG，避免把初始化问题与游戏 hook 混合。

若直接使用协议需要修改多个后端全局假设，重新评估 A/B，而不是无限扩展通用桥接。

### P1：AC7 取数与同尺寸回接

固定真实帧采样，验证颜色/深度/速度/jitter/曝光。先同尺寸旁路或 DLAA，保持原游戏可恢复。未知输入不伪造默认值作为完成依据。

### P2：真正 SR

建立低输入/高输出，处理原 TAA/放大替换、后处理尺寸和 UI。此阶段通过才进入 NR。

### P3：Pre-SR NR 与超宽屏压缩

复用选定分支已有链，先整幅单次，再横向周边压缩。验证有效处理阶段、颜色/引导变换及历史，分别测模型和完整帧成本。

### P4：FG

先确认交换链与明确输入路径，再启用 2×。若只能依赖尚未验证的 HUD 候选或相机默认值，该阶段不通过；优先补窄接口，而非降低数据要求。

本轮完成的是候选源码调查；运行前置调查未完成，不能据此确认最佳选型或首选原型。

## 12. 固定源码依据

OptiScaler 分支提交 `6125a75b37f7e2a408e31934f4827e5660429203`：

- [NGX DX11 输入](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/inputs/NVNGX_DLSS_Dx11.cpp)
- [FSR2 DX11 输入转换](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/inputs/FSR2_Dx11.cpp)
- [DX11→DX12 Feature 包装](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/upscalers/IFeature_Dx11wDx12.cpp)
- [Pre-SR NR 调度](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/upscalers/IFeature_Dx12.cpp)
- [FG 输入桥接](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/inputs/FG/Upscaler_Inputs_Dx11wDx12.cpp)
- [DXGI 交换链包装](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/hooks/DxgiFactory_Hooks.cpp)
- [FG 后端创建](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/hooks/FG_Hooks.cpp)
- [周边布局实现](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/6125a75b37f7e2a408e31934f4827e5660429203/OptiScaler/shaders/dlssnr/DlssNr_Spatial.h)

其他：

- [Luma Unreal 模块](https://github.com/Filoppi/Luma-Framework/blob/dc471fa590c24a12a36be3e527ec45d2595466bd/Source/Games/Unreal%20Engine/main.cpp)
- [Magpie 捕获接口](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/FrameSourceBase.h)
- [Magpie 效果上下文](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/NativeEffectBackend.h)
- [Magpie SR 输入语义](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/DLSSSRUpscaler.cpp)
