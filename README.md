# AC7 Native Adapter

《皇牌空战 7》普通屏幕原生渲染适配项目。

目标是在游戏内部取得干净场景及正确的深度、运动矢量、jitter、曝光与时序信息，连接可复用的 **Pre-SR NR → SR → FG** 后端，并为超宽屏提供 NR 中心保护和周边减负。

## 当前状态

- 已通过 Nsight Graphics 2025.5 捕获AC7并确认D3D11；任务失败和正常飞行两份C++ Capture均已重放且最终图像核对通过，尚无游戏适配器。
- 已新增D3D11输入规范化模块，并通过硬件GPU测试和AC7独立重放插入验证：深度逐位一致、速度对CPU参考最大NDC误差约1.37e-7，同尺寸旁路后的场景/HUD/最终图像均与基线逐位相同。尚无实时游戏hook或NR/SR/FG接入。
- 正常样本未开超宽屏补丁，有效场景中央2560×1440。相邻帧、jitter、颜色曝光、低输入高输出及最终全幅适配仍待验证。**后端与实现路径未选定**。
- 优先复用现有后端，仅开发必要的 AC7 适配与窄接口；不预设从头重写整条链。
- “独立项目”指独立组织代码、文档和验证材料，不预设独立进程处理。
- 已有 Magpie 预设和 Luma 实验包只作外部对照/诊断，不能视为满足原生接入目标。

## 工作入口

| 文档 | 用途 |
| --- | --- |
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
