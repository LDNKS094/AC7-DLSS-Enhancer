# 接入路线决策：保留 OptiScaler 与直接 NGX 两个候选

> **普通设备门槛及View协议已通过独立宿主**：[deferred复制](AC7-Neutral-ASI-Ordinary-Fix.md)与[真实View上传/guide/loader](AC7-View-Identity-ASI-Validation.md)已完成双设备矩阵，默认关闭观察包待用户实际游戏日志。实际主View/启动时序/模型仍未验，不据此扩成自研FG；旧阻塞块保留历史。

> **ASI当前门槛**：[实际hook验证](AC7-Neutral-ASI-Validation.md)在debug宿主通过原Draw/中性/停止，但普通设备未通过；当前没有游戏可部署包。先解决普通设备稳定拦截，再接真实帧/View/guide，不靠模组清理或扩大工具链规避。

> **用户最新实施偏好与宿主进展**：不把其它模组兼容作为前置，先做最直接ASI窄入口；确有冲突再提具体临时移出/恢复清单，不自动清理。[独立中性宿主](AC7-Neutral-Host-Validation.md)已通过默认关闭/无模型、区域/状态/停止控制，下一步ASI hook与原draw继续测试，真实主View/guide语义仍待连接；未部署游戏。

> **同通路对照与工程建议**：[路线对照报告](AC7-Route-Comparison-Validation.md)已完成直接NGX等价和ABBA有限计时，建议下一固定NR/SR原型优先路线2；路线1保留为FG/压缩复用候选，完整宿主和FG架构未选。CPU Evaluate差约0.070ms、GPU差小，不是FPS结论。OverlayMenu=false旧菜单在30帧后泄漏SR输出状态、日志sink全关不静默的配置误解已定位，失败证据保留；隔离默认配置绕开，无参考源码补丁。本段更新工作建议，不代替用户对最终方案的选择。

用户当前决策：现有信息不足以选择最终接入方式。接下来在路线1和路线2之间验证；路线3工作量过大，暂不考虑。不再默认推进ReShade专用中性回接。

| 路线 | 范围 | 当前决策 |
|---|---|---|
| 1：薄AC7适配层＋OptiScaler | 自行取得游戏数据并回接，复用现有Pre-SR NR/SR/周边压缩/FG等可用部分 | 保留候选；公开NR/SR已验证，FG/压缩复用未验 |
| 2：自研AC7接入＋直接NGX | 复用本项目规范化、HDR代理/恢复、共享资源、模型调用与生命周期验证，先做固定尺寸NR/SR | 下一固定尺寸NR/SR原型的工程优先建议；FG另选，不自动扩成全自研 |
| 3：完整自研NR/SR/周边压缩/FG呈现基础设施 | 连同FG交换链、UI、帧节奏与延迟全部自行适配维护 | 暂不考虑 |

## 为什么暂停ReShade专用原型

ReShade此前选作第一中性验证宿主，是利用已有真实采样入口的短期建议；它不是用户已经选定的最终路线。现暂停新增专用回接代码，现有采样插件继续保留供诊断使用，不改游戏安装。

没有同条件实测能证明ReShade、OptiScaler或自研哪条更快。应分别评估回调/资源跟踪的CPU成本与模型、格式转换、复制、跨API同步的GPU/等待成本，不以框架名称作性能结论。

现独立链为D3D11→D3D12 NR→D3D11 HDR恢复→D3D12 SR→D3D11；验证正确性有价值，但生产路线应比较是否可让NR、HDR恢复和SR在同一D3D12链中执行，减少往返。不能为了复用叠加两套互操作桥。

用户授权的本地提交已为`81021a0`，后续未再提交。[首轮接口](AC7-OptiScaler-Interface-Validation.md)后，自有共享桥＋公开D3D12入口在FL11.0输入上通过[持久八帧历史/Reset](AC7-OptiScaler-Sequence-Validation.md)：16输出对直接NGX逐位、64输入保持、双debug0/完成后清理正常；绕过了OptiScaler自身D3D11层，未修复该层的FL11.0恢复/Shutdown1限制。[D3D12颜色模块](AC7-D3D12-Color-Validation.md)之后，[公开NR与D3D12整链](AC7-OptiScaler-GPU-Chain-Validation.md)已实际执行两组八帧，80阶段逐位、64输入保持、完成后清理/debug0。组合为自有颜色＋公开原生NR转发＋OptiScaler SR，未采用内建NR compose；下一步原始D11输入输出桥和在途deadline/停止已由[共享整链验证](AC7-OptiScaler-Shared-Chain-Validation.md)完成：40正常阶段＋8返回逐位、整链迟到丢弃/双Reset/stop/D11退休通过；仍是fixture初始化、私有输出，在线guide和游戏回调未接。路线仍待同通路成本比较，尚无性能结论。

## 下一步验证顺序

1. 记录并提交当前源码、检查器、报告和本决策，排除模型、原捕获、构建和artifacts；只创建用户授权的一次本地提交，不push。
2. 核实本地OptiScaler固定源码版本、现成产物与构建条件。确认可调用导出、参数/初始化/Feature生命周期，以及全局状态/配置依赖；不将其假设为独立SDK。
3. 在独立非AC7宿主验证明确输入/输出的SR调用。必须核对真实模型后端/执行与输出，不能只看Success；游戏取数、NR和FG先不混入此最小测试。
4. 验证目标分支的Pre-SR NR能否容纳本项目SDR代理→NR→恢复HDR→SR合同；核对周边压缩的颜色/guide/MV对应，避免静默旁路。
5. 对路线1与直接NGX对照，列出接口改动、资源复制、同步点、初始化/销毁和FG复用边界，再决定接入方式。测试夹带的磁盘/读回/日志时间不能冒充帧时间。

若缺少可构建依赖或合法公共接口，记录具体阻塞与已验证范围，不改模型/驱动/私有ABI，不盲目扩大工具链。真实AC7输入取得与原warp/post/UI回接是两条路线都需要的工作。

## 保持的约束

普通屏幕；HDR→SDR代理→NR→HDR恢复→SR→原warp/后处理/tonemap→FG/UI/呈现；P1未校准但延后非阻塞；实际jitter0，不伪造。模型重建造成的差异不直接判缺陷。现有独立成果继续复用，不当作任何候选已游戏接入的证据。

不启动游戏、不部署、不改游戏/NVIDIA配置、不运行未隔离AC7派生重放、不要求用户补采。完整resize/device-loss/FG自研不再作为首次固定尺寸路线验证的前置。

相关：[工作计划](../PLAN.md)、[架构调查](AC7-Native-DLSS-Architecture-Selection.md)、[此前中性路径提案](AC7-Minimal-Game-Test-Path.md)、[固定会话验证](AC7-Fixed-Runtime-Ownership-Validation.md)。本决策优先于这些文档中旧“下一步ReShade中性回接”的内容。
