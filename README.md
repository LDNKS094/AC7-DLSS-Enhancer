# AC7 Native Adapter

《皇牌空战 7》普通屏幕原生渲染适配项目。

目标是在游戏内部取得干净场景及正确的深度、运动矢量、jitter、曝光与时序信息，连接可复用的 **Pre-SR NR → SR → FG** 后端，并为超宽屏提供 NR 中心保护和周边减负。

## 当前状态

**当前进展**：[第四轮实际全宽观察通过](docs/AC7-Live-View-Observation-Validation.md)：8个合格真实View身份、绑定关联、零复制、error0并正常停止，16条缓存承受1024次淘汰。合格counter有两帧间隙，不等于连续时间模型验收。当前观察版固定为通过基线，下一阶段再验证全宽中性复制，尚未启用场景修改；NR0/SR0，无自动部署/ZIP/提交，仍81021a0。

**当前路线决策**：信息仍不足以选型，只保留路线1“薄AC7适配＋OptiScaler”和路线2“自研接入＋直接NGX”；完整自研FG的路线3暂不考虑。暂停ReShade专用回接，现有采样插件仅保留诊断用途。一次提交与公开SR单帧/序列验证已完成，接下来核NR颜色合同与两路线成本后再决定。见[路线决策](docs/AC7-Integration-Route-Decision.md)与[工作计划](PLAN.md)。下方旧ReShade实施建议为历史。

最新：[固定会话所有权](docs/AC7-Fixed-Runtime-Ownership-Validation.md)已通过4次实际模型/pass重建与安全关闭、部分初始化拒绝、六份恢复参考和正常56阶段回归。下一步按[最小游戏测试路径](docs/AC7-Minimal-Game-Test-Path.md)，用已有ReShade公开回调构建默认关闭、固定尺寸、不加载NGX的中性回接。动态resize不再作为首次中性测试前置；同帧曝光与模型完成时序仍需后续解决，当前未部署或测试游戏。以下为前阶段记录。

最新补充：[处理错误后的停用与退休](docs/AC7-Chain-Error-Stop-Validation.md)已通过NR在途、空闲、SR在途三处调用方错误注入；第一原因保持、停止接收、输出隐藏及健康GPU退休通过，正常56阶段回归保持一致。绝对提交入口已移除直接ExitProcess，部分提交/API失效分支和统一会话所有权仍待验证。以下单步驱动成果继续有效。

最新进展：[单步整链驱动与退休](docs/AC7-Nonblocking-Chain-Validation.md)通过固定尺寸独立验证：一次advance只推进/轮询一个阶段，源快照支持延迟HDR恢复；NR/SR各16次受阻pending、停止拒新帧、D3D11与模型共同退休、三次驱动重建双Reset与六份参考输出逐位通过。正常56阶段回归不变。下一步整理资源所有权/错误停用并验证实际尺寸和设备会话重建；当前只验证错尺寸拒绝，未接游戏或验收性能。此前[整链调度策略](docs/AC7-Chain-Schedule-Validation.md)与[源覆盖](docs/AC7-Frame-Submission-Validation.md)证据继续有效。下方早期状态按历史理解，当前无需补采。

- 已通过 Nsight Graphics 2025.5 捕获AC7并确认D3D11；任务失败和正常飞行两份C++ Capture均已重放且最终图像核对通过，尚无游戏适配器。
- 已新增D3D11输入规范化模块，并通过硬件GPU测试和AC7独立重放插入验证：深度逐位一致、速度对CPU参考最大NDC误差约1.37e-7，同尺寸旁路后的场景/HUD/最终图像均与基线逐位相同。尚无实时游戏hook或NR/SR/FG接入。
- 正常样本未开超宽屏补丁，有效场景中央2560×1440。相邻帧、jitter、颜色曝光、低输入高输出及最终全幅适配仍待验证。**后端与实现路径未选定**。
- 优先复用现有后端，仅开发必要的 AC7 适配与窄接口；不预设从头重写整条链。
- “独立项目”指独立组织代码、文档和验证材料，不预设独立进程处理。
- 已有 Magpie 预设和 Luma 实验包只作外部对照/诊断，不能视为满足原生接入目标。

## 工作入口

| 文档 | 用途 |
| --- | --- |
| [首轮实际日志/宽屏诊断](docs/AC7-Game-Entry-Diagnostics.md) | 实际启动已验，双布局只观察与一次性拒绝诊断待下一份游戏日志 |
| [View身份/guide/loader](docs/AC7-View-Identity-ASI-Validation.md) | 独立八帧/拒绝/丢帧恢复通过，默认关闭包待用户游戏日志 |
| [普通设备ASI修复](docs/AC7-Neutral-ASI-Ordinary-Fix.md) | deferred复制路径双设备完整矩阵通过；真实帧/View与loader待接 |
| [ASI早期失败记录](docs/AC7-Neutral-ASI-Validation.md) | 原始普通设备失败与替换诊断，当前修复见上条 |
| [中性宿主验证](docs/AC7-Neutral-Host-Validation.md) | 独立D11默认关闭/精确匹配/区域状态与停止通过；ASI窄hook下一步 |
| [同通路路线对照](docs/AC7-Route-Comparison-Validation.md) | 直接NGX等价、有限ABBA计时/菜单配置勘误；建议固定NR/SR优先路线2 |
| [原始HDR共享整链](docs/AC7-OptiScaler-Shared-Chain-Validation.md) | 单桥/整链发布/迟到丢弃/双Reset/停止退休通过，私有输出限制 |
| [OptiScaler公开整链](docs/AC7-OptiScaler-GPU-Chain-Validation.md) | 独立NR及同D3D12颜色→NR→HDR→SR，80阶段逐位；桥/在途停止待验 |
| [D3D12颜色验证](docs/AC7-D3D12-Color-Validation.md) | 48阶段双参考逐位、FP16代理/R11恢复/FP16颜色；分段接模型待验 |
| [OptiScaler八帧验证](docs/AC7-OptiScaler-Sequence-Validation.md) | 持久SR历史/Reset16输出逐位、64输入保持、双debug0；NR颜色合同待验证 |
| [OptiScaler接口验证](docs/AC7-OptiScaler-Interface-Validation.md) | 公开D3D12入口＋自有桥在FL11.0正常执行/清理；NR/FG与性能待验 |
| [接入路线决策](docs/AC7-Integration-Route-Decision.md) | 路线1/2候选、路线3暂缓；OptiScaler独立接口验证优先 |
| [固定会话所有权](docs/AC7-Fixed-Runtime-Ownership-Validation.md) | 实际模型/pass重建、安全关闭与部分构造拒绝验证 |
| [最小游戏测试路径](docs/AC7-Minimal-Game-Test-Path.md) | 默认关闭的ReShade中性回接先行；模型同帧时序后接 |
| [错误停用与退休](docs/AC7-Chain-Error-Stop-Validation.md) | 三处调用方处理错误注入通过；真实API/设备失效及统一所有权待验 |
| [单步驱动与退休](docs/AC7-Nonblocking-Chain-Validation.md) | 固定尺寸逐次推进、源场景快照、停止与双API退休；实际resize/device-loss待做 |
| [整链调度策略](docs/AC7-Chain-Schedule-Validation.md) | 单个绝对deadline贯穿两模型与转换，丢帧/双Reset验证；仍是独立诊断驱动 |
| [帧提交与源覆盖](docs/AC7-Frame-Submission-Validation.md) | 单次轮询接口、16份覆盖前快照逐位、绝对截止时间；整帧调度待完成 |
| [等待预算与迟到退休](docs/AC7-Deadline-Retirement-Validation.md) | 真实NR/SR超时原图回退、同token迟到丢弃、双Reset恢复逐位；仅提交后预算，非整帧实时 |
| [完整GPU NR→HDR→SR链](docs/AC7-GPU-Chain-Validation.md) | 同进程真实八帧、56阶段逐位；中间无CPU/文件图像反馈，尚无游戏回接/实时预算验收 |
| [真实模型共享纹理验证](docs/AC7-Shared-Model-Validation.md) | NR/SR各自真实八帧共享求值并回11，24返回与文件基线逐位；完整无文件模型链待接 |
| [共享资源桥与定点验证](docs/AC7-Shared-Bridge-Validation.md) | 真实八帧共享GPU往返/过期回退逐位；太阳运动对应实测；新桥尚未接模型/游戏 |
| [真实八帧NR→HDR→SR链](docs/AC7-Live-Eight-NR-SR-Validation.md) | 698→705动态输入，NR8/SR16持久feature/历史与Reset对照、输入逐位/debug0；仍非游戏内实时处理 |
| [固定8帧采样验证](docs/AC7-Eight-Frame-Sampler-Validation.md) | v4两槽/256MiB逻辑内存/1GiB磁盘，8帧硬件与重放通过；可进行一次真实飞行补采样 |
| [真实两帧NR→HDR→SR链](docs/AC7-Live-Pair-NR-SR-Validation.md) | GPU代理/guide→NR1/0→GPU恢复HDR→SR1/0离线通过；仍无实时桥，下一步有界8–16帧 |
| [真实两帧SR历史执行](docs/AC7-SR-Live-Pair-History-Validation.md) | 同feature/纹理Reset1→0实测，与1→1对照首帧逐位同、次帧历史敏感；P1/jitter0，非画质验收 |
| [动态真实两帧验证](docs/AC7-Dynamic-Live-Pair-Validation.md) | 1123→1124，game/real推进与六矩阵链通过；相机回退运动非零，后续利用现有样本 |
| [首次真实两帧验证](docs/AC7-First-Live-Pair-Validation.md) | v3实机传输成功，393→394、real与矩阵链对应；模拟时间保持，动态验收未通过 |
| [有界两帧采样验证](docs/AC7-Bounded-Sampler-Validation.md) | 固定2帧核心硬件检查、独立ReShade插件重放传输/原15项逐位；诊断包未部署 |
| [真实连续帧采样方案](docs/AC7-Temporal-Capture-Plan.md) | Next Frame实测crash，暂停双捕获；已有长捕获证明跨帧采入，待解决帧数/停止控制 |
| [画质评估决策](docs/AC7-Image-Quality-Evaluation.md) | 区分接入错误、NR预期变化与可见缺陷；数值差异不当质量分数或单独阻塞 |
| [工作计划](PLAN.md) | 当前阶段、下一步调查及选型门槛 |
| [首帧历史归档](history/nsight-startup/README.md) | 已结束的工具选择、启动资格及首次Live Analysis调查 |
| [图形API与正式NR](docs/AC7-Graphics-API-Support.md) | NR310.9.16.0：D3D11特性拒绝，D3D12真实AC7单帧SDR求值通过 |
| [NR初始化/创建记录](docs/AC7-NR-Runtime-Preflight.md) | 逐阶段结果、scratch非必需证据及NGX缓存副作用 |
| [NR真实单帧求值](docs/AC7-NR-Single-Frame-Evaluation.md) | 正式SDR合同、首次GPU求值与预览 |
| [HDR往返参考验证](docs/AC7-HDR-NR-Roundtrip.md) | CPU参考、直接HDR拒绝证据及候选处理顺序 |
| [GPU HDR往返与离线回接](docs/AC7-HDR-GPU-Bypass-Validation.md) | 早期中性往返、CPU对照与状态检查 |
| [同一GPU代理的NR回写](docs/AC7-NR-GPU-Writeback-Validation.md) | 真实NR输入逐位核验、GPU恢复、原后处理/曝光/UI及最终帧 |
| [SR HDR合同与本机前置验证](docs/AC7-SR-HDR-Contract-Preflight.md) | 正式接口、能力/创建证据与原生输入门槛 |
| [SR Reset单帧诊断](docs/AC7-SR-Reset-Diagnostic.md) | 原始/NR后HDR在显式假设下求值成功；真实pre-exposure和画质尚未验收 |
| [SR高光空间响应](docs/AC7-SR-Spatial-Response.md) | 差异集中在海面稀疏高光；一次合成控制区分空间响应与固定HDR限幅 |
| [SR同尺寸离线回接](docs/AC7-SR-Writeback-Validation.md) | NR→SR→原FXAA/后处理/UI已回接，旁路12项逐位、UI不变；非实时/时域验收 |
| [扭曲前候选边界](docs/AC7-Prewarp-Boundary-Validation.md) | 465后/466前中性15项逐位；颜色warp与depth/MV未同变，下一模型输入前移 |
| [扭曲前NR→SR新配对链](docs/AC7-Prewarp-NR-SR-Validation.md) | 新输入实际NR/SR各一次通过并回接，原warp/UI保留，旧输入错配拒绝 |
| [View曝光字段布局核对](docs/AC7-View-Exposure-Layout.md) | AC7局部字段匹配旧ExposureScale序列，不能套新版PreExposure偏移或将1.3333当作P |
| [颜色曝光与后端门槛](docs/AC7-Color-Exposure-Backend-Gates.md) | 曝光实际应用通道、HDR范围与候选接口缺口；资源桥优先次序已由API核对修正 |
| [D3D11模块与旁路验证](docs/AC7-D3D11-Guide-Bypass-Validation.md) | 可构建输入模块、GPU数值检查、状态恢复及重放内逐位一致验证 |
| [正常飞行证据](docs/AC7-Nsight-Normal-Flight-20261028.md) | 新无补丁捕获、真实速度写入/相机回退、HUD分离和有效区域 |
| [任务失败捕获分析](docs/AC7-Nsight-Patched-Capture-Analysis.md) | 较早带补丁样本的重放与场景/UI边界；当帧速度全零 |
| [RenderDoc 结论](docs/AC7-RenderDoc-Status.md) | 当前环境不可用的简短说明与归档入口 |
| [需求与验收](docs/AC7-Native-DLSS-Requirements.md) | 原生数据契约、功能目标和验收标准 |
| [候选架构调查](docs/AC7-Native-DLSS-Architecture-Selection.md) | 已有源码证据、限制和未决项，尚不排序 |
| [Overglaze 参考分析](docs/AC7-Overglaze-Reference.md) | DX12 Post-SR NR的可复用部分、AC7缺口及183项独立检查 |
| [综合调查记录](docs/AC7-DLSS-NR-SR-Investigation.md) | SR/NR/FG、工具与中心区域优化的调查材料 |
| [Magpie 外部对照](docs/AC7-PreSR-NR-SR-FG-Setup.md) | 不满足原生输入要求的对照方案及预设 |

## 项目结构

```text
AC7-Native-Adapter/
├─ README.md
├─ PLAN.md
├─ AGENTS.md
├─ .gitignore
├─ CMakeLists.txt                     # D3D11模块与独立验证程序
├─ src/d3d11/                         # 深度/速度规范化模块
├─ tests/d3d11/                       # 合成/捕获GPU测试与重放插入检查
├─ docs/                              # 当前需求、证据与调查
├─ tools/nsight/                      # 捕获索引、反汇编和独立重放读回检查
├─ tools/nr/                          # 官方loader与运行时需求查询工具
├─ history/                           # 已结束的RenderDoc与Nsight启动调查
└─ artifacts/
   ├─ captures/                      # Nsight 导出与外部捕获索引
   └─ backups/                       # 补丁恢复备份及配置备份
```

D3D11处理模块可独立构建；目前测试限定为已保存捕获与合成数据，不注入游戏，也未引入NR后端依赖。构建/测试入口见 [验证文档](docs/AC7-D3D11-Guide-Bypass-Validation.md)。

## 本地参考材料

参考仓库和已有产物保持原位置，没有作为本项目依赖或源码合并：

- `../research/overglaze/`（NR输入规范化与生命周期参考，未接入）
- `../research/Luma-Framework/`
- `../research/OptiScaler-DLSSNR-PreSR-Multipass/`
- `../research/CheekyFoveatedDLSS/`
- `../research/AC7VRDLSS-Downloads/`
- [历史离线测试与证据](../research/architecture-selection/selection-evidence.json)
- [Luma 诊断实验包说明](../research/AC7-Flat-Probe/README_zh.md)
- Magpie 参考源码：`E:\CodingProject\Magpie`

这些目录不属于本项目 Git 仓库。将来需要复用具体代码时，再记录版本、许可、引入范围及构建方式。

## 下一步

**等待预算与历史恢复已在独立整链通过**：真实NR/SR分别注入2ms post-submit超时，4份原图回退逐位、2迟到结果同token也不回写，busy不复用资源，完成fence才释放四输入；两次双Reset恢复及一次继续history与独立参考共6输出逐位。无故障8帧56stage保持原基线，debug0/cleanup，实际本轮NR26/SR24含参考与复验。见[验证报告](docs/AC7-Deadline-Retirement-Validation.md)。这还不是整帧预算或非阻塞游戏runtime：固定测试调度保留诊断/退休等待，fixture不模拟引擎纹理内容复用。下一步抽离无诊断读回/文件的调度入口，整帧deadline及session/resize/device-loss/安全卸载，再游戏host和原scene/post回接。无需补采或换插件；本轮无游戏/AC7重放/配置写/部署/提交。下方旧下一步为历史。

**同进程完整GPU数据链已通过：** native fixtures→GPU guide/代理→共享NR→GPU恢复HDR与格式转换→共享SR→D3D11返回，真实698–705共56阶段raw全部逐位匹配原文件链，两特性/共享纹理持续、Reset/time/debug0/cleanup通过，中间图像不经CPU/文件反馈。仍有诊断validity/stage读回、参考比对、保存和CPU轮询，不是无CPU同步/性能验收。见[完整GPU链验证](docs/AC7-GPU-Chain-Validation.md)。GPU motion局部补明确half-even位编码（含±0/subnormal），40分量独立GPU自检及8帧全部motion逐位，失败两轮在Eval前拒绝并保留；公共模型参数提取后legacy NR2/SR2与shared SR reset8回归逐位。本轮总NR26/SR34，最终链NR8/SR8，无模型/SDK改造。下一步去诊断开销、deadline/drop-reset与同帧回退，再host/proxy身份/原scene和后处理接回；不需补采/部署，主会话未启动AC7/重放或写配置；同期真实游戏退出更新INI时间戳，但1150字节与保全副本逐位一致，未发现自动优化覆盖。以下旧“未接整链”为历史。

**真实NR/SR分别接共享桥已通过，24份最终输出与原文件链整个raw逐位一致。** 沿用原模型参数/feature，增加固定8共享模式5/6：D3D11fixture→共享输入→D3D12模型→COMMON→D3D11返回，每组feature/resource identity固定，NR24/SR每支32输入读回、24返回、两APIdebug0与清理/有限值通过。首轮后收紧录制成功才upload/submit与明确published判断，最终完整复验仍一致；本轮实际总NR16/SR32（最终集NR8/SR16）。见[共享模型验证](docs/AC7-Shared-Model-Validation.md)。**尚未把NR共享返回→GPU恢复HDR→SR共享输入连成无文件链**，两特性仍独立fixture；下一步独立GPU整链核原基线，不部署、不需补采。未验实时预算/模型drop-reset/device失效/游戏host和原后处理回接。本轮无AC7/重放/配置改动，以下旧状态为历史。

**共享资源桥独立原型已通过：** D3D11创建NT共享→D3D12打开，公开shared fence交替访问；真实8帧32输入/8输出逐位、固定5资源、两边debug0，刻意阻塞12队列时pending不发布，晚到结果discard保原图，busy/重复/缺输入/错格式/异adapter拒绝，D3D11绑定保留。先前12创建→11打开的本描述符组合E_INVALIDARG已保留，未做私有绕过。另用太阳实际质心独立验向后motion，7段误差.013–.085px、反向对照24–153px，所设高光/旧足迹阈值下候选0；飞机/海面细节图已查看，不等于全场景零拖影/质量验收。见[共享桥与定点验证](docs/AC7-Shared-Bridge-Validation.md)。**新桥尚无模型调用**，既有八帧NR/SR仍文件链；下一步让真实NR/SR使用共享纹理并验证回11，再考虑宿主/原后处理回接。无需补采/换插件，本轮无模型/AC7重放/配置改动。以下旧状态为历史。

**真实八帧与完整离线NR→HDR→SR历史链已通过执行资格，无需再次补采。** 58000-130125640计数698→705，7段game/real及6×7矩阵链精确，场景/depth/velocity实际变化，allocation恢复5120×1440；NVIDIA自动优化已由用户关闭且只读确认0，本轮未启动AC7/派生重放。GPU8帧代理/guide/中性恢复通过→同feature NR8次→配对GPU恢复→SR history8及逐帧Reset对照8，共NR8/SR16，所有输入读回/fence/debug0/有限值通过、首帧两支逐位。大位移FP16边界经原公式复算后改为半量化bin+.001px的float检查，错误/失败证据保留，原数据/模型值不改。原生高光本来就达12288，恢复后逐帧保留，非新增异常推断。见[动态8帧](docs/AC7-Dynamic-Eight-Frame-Validation.md)、[8帧完整链](docs/AC7-Live-Eight-NR-SR-Validation.md)及[预览](artifacts/research/live-nr-sr-eight/comparison.png)。实际jitter0/P1暂用、约0.286s不代表完整时域画质；下一步核具体稳定性/遮挡与最窄实时资源桥/同步/回接/失败回退，尚不能开启游戏内模型。以下旧采样/失败建议为历史。

**八帧首次实机失败与显示重置已查到直接原因：** NVIDIA App自动优化把新路径诊断重放当AC7，共享配置从无边框/100变全屏/200，真实纹理10240×2880/像素四倍，预算检查按设计拒绝。已保全日志/前后状态并停止所有未隔离AC7重放；不提高采样预算、不再换addon、不自动改NVIDIA全局偏好。当前退出保存值已100/无边框；下一次正常启动先确认比例100后再按旧八帧命令采样。见[事件报告](docs/AC7-Eight-Frame-Display-Reset-Incident.md)。此前“无游戏文件改动”仅指直接写入，不能涵盖已证实外部程序间接修改。

**固定8帧采样升级已通过离线资格，可补一次真实飞行序列。** v4保持2槽而非囤8帧，逻辑在途≤256MiB/总磁盘≤1GiB、忙则失败不跳帧；达到8帧停止，旧两帧默认入口保留。合成40资源/断档/缺帧/写盘失败/磁盘预算与旧回归debug0通过；AC7重放8份40资源对应、原15raw逐位，逻辑峰112.5MiB、总量约900MiB，995重复正确标非连续。包`artifacts/packages/ac7-eight-frame-diagnostic-v4/`备齐未部署。现在需要用户退出后仅换addon并复制Request-Eight-Frames.cmd，飞行后运行命令立刻回游戏（延迟5秒），提供AC7-eight-frame目录；详见[采样验证/步骤](docs/AC7-Eight-Frame-Sampler-Validation.md)。这是资源补采样，尚非游戏内NR/SR，模型链/实时桥状态保持。

**真实两帧GPU代理→NR→HDR恢复→连续SR离线链完成。** 两帧中性scene逐位、GPU depth逐位、guide invalid0；NR同feature/texture Reset1→0两次求值/6输入读回/debug0，重新核GPU proxy为实际NR输入后恢复HDR、区外0，再SR同feature Reset1→0及1→1对照4次求值/输入读回/debug0。SR首帧对照逐位、次帧2989365像素差仅诊断；预览无缩略全局异常，不验收动态画质。见[完整离线链](docs/AC7-Live-Pair-NR-SR-Validation.md)。只读Luna完成NR公共Reset/入口核对，父会话实现/GPU/验收。仍是文件staging非实时互操作；**尚不能游戏内开启NR/SR**。下一补测为8–16真实连续帧，先准备有界短序列工具（不能突破现有预算直接囤16帧），届时告知用户；现在不需重复两帧或操作游戏。P1/jitter0保持，2新NR/4新SR，无游戏改动/新提交。

**真实动态两帧SR历史执行已通过。** 原始prewarp HDR/各自新guides输入，同NGX feature/texture连续Reset1→0，两帧Evaluate/GPU完成/四输入逐位/debug0，RGB全有限保HDR。1→1对照首帧逐位相同，次帧1849323像素不同，支持history设置影响输出，不当质量评分；旧单帧frame1与Reset对照逐位，缺输入/覆盖Init前拒绝。见[SR历史执行验证](docs/AC7-SR-Live-Pair-History-Validation.md)。此次隔离SR，未叠加NR，P1/jitter0仍显式；下一步用真实两帧GPU规范化/代理→NR→恢复HDR，再复用本次SR持久路径。无需用户重采当前两帧；8–16长期质量/真实jitter/实时回接后验。新增5次SR求值、0NR、0游戏，无提交。

**动态两帧输入资格已通过。** 用户70932目录计数1123→1124、game/real约16.98ms推进且两条链残差0、6矩阵链逐位、相机矩阵改变；颜色1411575/depth-stencil1598651有效区像素变化、区外0。原objectMV纹理虽相同，实际depth/View相机回退约137万像素>0.1px、最大2.98px，不能只看raw object纹理判全局motion。jitter仍0，完整动态质量标志false保留。见[动态输入资格](docs/AC7-Dynamic-Live-Pair-Validation.md)。当前不再要求用户重复两帧；优先利用这份真实连续数据准备规范化/持久feature的Reset→保留history执行检查，再扩8–16帧；模型时域/长时质量未由采样资格自动通过。

**首次真实两帧采样成功，但模拟时间保持。** 用户77324目录2×5资源完整，计数393→394、实时时间串接、6组当前/前帧矩阵逐位对应，支持相邻渲染区间；真实jitter0。两帧游戏时间相同、depth/MV相同，颜色有效区75,109像素变化，适合传输/静止历史证据，未验收动态时域。`candidateTimeChain=false`来自game链：旧UE4参考用CurrentWorldTime−DeltaWorldTime构造PrevGameTime，暂停时不能强制等于前一采样；原manifest保留。采样索引7→0并非393/394的mod8，旧单帧巧合假设已修正。详见[首次实机两帧](docs/AC7-First-Live-Pair-Validation.md)。下一次无需换DLL，只换`artifacts/packages/ac7-two-frame-flight-trigger/Request-Two-Frames.cmd`（等待5秒），进入飞行后启动命令立即回游戏，再核验动态两帧；当前未改游戏文件/运行模型。

**第二次实机失败为辅助consumer重复，且采样早于新请求。** 日志显示启动约4.3秒即arm并失败，新遗留请求创建于约84秒后；支持启动时消费旧请求，不足以判断正常飞行采样结果。v3已加入进程创建时间请求门禁（旧请求归档不arm）及最多16个guide组合，按prewarp实际depth/View/viewport唯一配对，重复相同绑定去重，速度歧义仍拒绝；最多64个事件trace。硬件配对/歧义/请求门禁及原检查debug0，重放两帧/原15raw逐位通过。包`artifacts/packages/ac7-two-frame-diagnostic-v3/`备齐未部署；退出后只换addon、移走遗留`AC7-Capture.request`，进入飞行再发一次新请求。详见[采样记录](docs/AC7-Bounded-Sampler-Validation.md)。

**首次实机采样未完成，v2已备好。** v1成功加载并响应请求，但在复制前因`Unsupported scene dimensions or View rect`拒绝，仅留下INCOMPLETE/failure；未取得真实帧。显示5120×1440不能证明内部纹理同尺寸，v1未记录实际数值。v2改用两帧256MiB总字节预算、保留rect/格式/设备检查，并在拒绝前写实际纹理/rect/预算metadata；新增padding与超预算硬件检查通过，重放原15raw不变。新包`artifacts/packages/ac7-two-frame-diagnostic-v2/`已准备，**未替换游戏内v1**；下一次退出游戏后只升级addon、保持显示设置，再采一次取得有鉴别力的数据。详见[采样记录](docs/AC7-Bounded-Sampler-Validation.md)。

**受限采样工具已实现并通过离线资格检查。** 使用ReShade公开API20作独立诊断宿主，固定两帧/最多256MiB逻辑资源，保留原draw/矩阵/jitter，无NGX。合成硬件两组10资源有效数据准确、状态/拒绝/debug0；AC7捕获重放插件实际匹配三个shader，采入两组10资源后停止，原15raw逐位。两次重放计数995/995正确标非相邻，不当真实时域证据。早期加载大栈帧及代理device指针误判已修复，失败/对照日志保留。见[有界采样验证](docs/AC7-Bounded-Sampler-Validation.md)。诊断包`artifacts/packages/ac7-two-frame-diagnostic-v1/`已备齐二进制、许可、触发命令与回退说明，**尚未部署到游戏，也未取得真实连续帧**。只读游戏目录清单未见dxgi/ReShade冲突，实际部署前仍需复核。

已创建首个本地提交`4a75c2d`，包含此前代码/验证材料与画质评估决策；原始捕获、模型/SDK、raw与构建均未入Git。提交后继续的[连续帧方案](docs/AC7-Temporal-Capture-Plan.md)和候选元数据脚本保持未提交，不自动再提交/推送。

**用户确认Next Frame存在但会crash，双捕获操作已撤回。** 此前F11采入约1363帧/675万事件并卡住，足以证明实际存在跨帧采集行为；现在缺的是可控的帧数上限/停止与可用资源导出，不是再验证能否采多帧。先复查已有长捕获及公开D3D11控制入口，当前不要求用户截图、重试Next Frame/F11或导出巨大包。找到可靠边界后再采2帧资格/8–16帧短序列；若公开Nsight路径不足，再评估窄只读资源采样。官方Graphics Capture `--frame-count`仍只适用D3D12/Vulkan，不能套AC7。真实render jitter和历史/reset另验，已有计数995/mod8=3、时间步约8.3336ms仍只是单帧候选。

**评估决策已明确：模型差异不等于缺陷。** NR预期改变光照/材质，暗部倍率与变化像素数只作定位，不因这些统计限幅或阻塞推进。接入错误用源身份/区域/UI/数值等契约判断；画质用具体可见异常、连续帧稳定性与用户观感判断。见[评估决策](docs/AC7-Image-Quality-Evaluation.md)。

**用户当前决策：延后预曝光调查。** 已有P=1单帧诊断未见黑屏、整体错位或明显全局亮度异常，后续实验暂用PreExposure=1并继续记录该假设，不要求先证明游戏真实P。优先验证真正SR插入位置、原后处理/清晰UI回接和连续帧输入；静态追P不再是当前任务。海面细小高光衰减仍是未验收画质项，尚无证据归因于P；后续若发生明显亮度跳变、曝光变化相关闪烁或历史异常，再将预曝光列回排查项。

**扭曲前的新配对NR→SR链已跑通。** 新GPU代理送入正式NR、GPU恢复HDR再送正式SR(P1/Reset)，两次求值均GPU完成、输入读回逐位、D3D12debug0。新465后边界完整源逐位后写SR，让原warp/后处理/UI继续，FXAA保留/旁路均完成。中性15项及SR恒等18项raw逐位；SR相对NR最终896,097像素变化、区外0，guides/扭曲场/三UI保持原阶段数据，旧代理/旧SR源两项错配写回前拒绝。NR暗部11通道>10倍、最高约17.1倍及SR细高光衰减未验收。见[新配对链验证](docs/AC7-Prewarp-NR-SR-Validation.md)。下一步真实连续帧输入、jitter/history/reset方案；仍非实时桥/时域/超分/FG验收，预曝光继续延后。

**候选位置现优先前移至原颜色扭曲前（465后、466前）。** 实际warp改变88,621颜色像素，深度32bit和速度逐位不变，stencil则变227,425像素；扭曲场texel中心最大位移长度约31.09输入像素，不能忽略。新边界中性HDR往返后15项raw与原重放逐位一致，原扭曲/UI/最终帧均保留。下一步必须用新prewarp颜色重新跑配对NR/SR，不复用旧postwarp模型结果，详见[边界验证](docs/AC7-Prewarp-Boundary-Validation.md)。这仍非透明/历史/时域或实时hook验收，P1暂用。

**FXAA对照已完成。** 同一SR结果下，保留FXAA默认路径13项raw与前次逐位一致；正确旁路仅复制有效区RGB，保留原FXAA alpha/黑边，上游及三张UI不变，最终78,379像素变化、区外0。飞机/海面选区的额外空间平滑已量化，但不代表动态锯齿/闪烁已验收。后续SR实验可优先采用FXAA RGB旁路候选、保留原FXAA作对照，不改游戏设置。下一步转向候选位置的warp颜色/guide及连续帧/jitter/history输入，见[回接与FXAA对照](docs/AC7-SR-Writeback-Validation.md)。

**最新完成：同尺寸SR离线回接。** 派生捕获中逐位核验NR后整张HDR后，写入匹配的SR输出，继续经过原曝光/tonemap、空间AA（用户确认游戏菜单只有FXAA）、后处理/UI和最终帧。旁路12项raw与原NR结果逐位一致；SR改变有效区416,948个HDR像素、最终896,406个像素，区外0变化，三张UI资源逐位不变。本轮复用既有SR结果、没有新增模型求值，见[回接验证](docs/AC7-SR-Writeback-Validation.md)。Draw467仍是离线候选点，已warp颜色与guide对应/透明时序未验收；后续先对照原FXAA保留与旁路，再验证真实连续帧。P1暂用，预曝光继续延后；未实时接入或提交。

用户接受候选顺序：场景HDR→SDR代理→NR→恢复HDR→SR(HDR)→原后处理/最终tonemap→FG最终数据与呈现。现已把同一GPU生成的SDR代理送入正式NR，并在派生AC7捕获中逐位核验代理后用GPU恢复/写回HDR，让修改经过原后处理/UI进入最终帧。中性9项raw与基线逐位一致，NR模式三张UI资源逐位不变，HDR166及20,566完全保护高光保留，见 [配对NR回写验证](docs/AC7-NR-GPU-Writeback-Validation.md)。

曝光统计变化，但目标2.470→2.450都被原[0.5,1]范围限到1，因此本帧增益不变；不代表动态曝光已验收。各GPU阶段仍通过离线文件/CPU staging衔接，不是实时资源桥或同进程连续GPU链。

SR现已在显式独立输入域下完成两组2560×1440 HDR/DLAA Reset图像诊断：原始HDR及NR回写后HDR，四张输入（含实际1×1曝光）读回逐位，GPU完成/debug0，全部RGB有限并保留>1。**PreExposure=1是公开记录的诊断假设，不是游戏实际值**；真实pre-exposure和时域/高输出接入仍未确认。输出峰值166→102.8125且有较大局部变化，未验收画质，未把SR结果写回游戏。见 [SR单帧诊断](docs/AC7-SR-Reset-Diagnostic.md)。

HDR生产链覆盖扩大至156个直接writer/33个唯一PS的统计和最后两级生产关系，但未证明预曝光因子。[后续空间分析](docs/AC7-SR-Spatial-Response.md)定位约90%绝对差异来自0.47%的稀疏高光像素；同参数一次合成Reset控制中，平坦166输出162.75、单像素160输出3.83，输出最大185.125，支持空间结构相关响应而非固定HDR上限约103。海面高光衰减仍未验收，不据此补写曝光参数。下一门槛为真实曝光/view语义及有明确导出方案的相邻帧；最新[View布局核对](docs/AC7-View-Exposure-Layout.md)已把AC7实际rect/尺寸/整数sample及override连续字段与公开UE4.18修改分支对齐：row129.y=1.333333是旧ExposureScale候选，参考填充代码注释仅用于MobileHDR=false，不等于预曝光。新版UE4.22在此新增字段且局部不兼容，不能套偏移；32个View绑定PS/槽位的字面row129扫描0命中也不构成全路径无P证明。曝光两端已进一步核对：Scene210→对数亮度/多级缩减→适应→tonemap的资源/常量和计算相符，CPU误差8.60e-8；旧/新曝光纹理四处PS/VS用途已区分，辅助时域权重不当作场景预曝光。六个生产PS的准确末端已核，但完整上游公共尺度仍未证明；倒数测光可能吸收固定P，因此“没看到显式除P”不能定P1。后续追具体CB/VS尺度来源，必要时先制定受控曝光观测方案，当前不让用户盲目抓帧。保持不深改SDK/模型/UE4工具链、未部署或提交。
