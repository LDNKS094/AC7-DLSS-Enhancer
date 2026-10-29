# AC7单帧HDR→SDR→NR→HDR参考验证

## 已确认的候选处理顺序

用户已接受本项目按以下顺序继续窄适配验证（尚非整链路实测通过）：

`游戏场景HDR → 保留原HDR并生成SDR代理 → NR(SDR) → 恢复场景HDR → SR(HDR) → 原游戏后处理/最终tonemap → FG所需最终颜色、引导、UI与呈现接入`。

SDR/HDR按**插入位置的资源语义**判断，不按显示器开关判断。AC7即使输出SDR，tonemap前场景仍是HDR；只有输入本来已符合NR的SDR合同时才省去转换。SR有HDR输入合同，不能预定NR和SR都在SDR后再恢复HDR；SR实际格式/曝光/时域尚待验证。

NR临时代理不是FG最终颜色。FG不因中间有代理必然失效，但恢复伪影/闪烁、时序、SR后坐标尺度、UI分离、颜色空间和执行成本会影响它，需单独验收；当前没有FG执行/呈现通过证据。

局部GPU颜色pass及同一GPU代理的真实NR非中性回接已完成，见 [最新验证](AC7-NR-GPU-Writeback-Validation.md)：现场代理逐位核验后恢复/写回HDR，原曝光/后处理/UI及最终帧已检查。跨阶段仍用离线文件，不是实时桥；下一步核实SR HDR合同与真正插入/高输出回接。

## 当前HDR输入结论：需要SDR兼容代理

针对本机已验证的**正式NR310.9.16.0、公开NGX接口、RTX5080/617.42**，已完成直接场景HDR对照，排除把原始HDR直接输入作为项目可行路线。这里不是仅凭格式或某篇社区文章推断。

| 检查 | 结果 |
| --- | --- |
| 官方Remix源码合同 | 明确“DLSS-NR operates in SDR space”，前后有fast/inverse tone mapping |
| 正式NR专用defs/helpers完整核查 | 193+159+105行；Create仅Width/Height，未见HDR、color-space、pre-exposure或paper-white参数；不能借用SR的HDR标志宣称NR同样支持 |
| 直接HDR输入 | 真实AC7场景，只有R11G11B10→RGBA16F格式转换，无tone map/曝光/gamma；最高166，353,271个RGB通道>1 |
| Enabled=1，四项修改强度归零 | Evaluate/GPU完成成功，输入读回逐位一致、debug error0；**输出全部RGB严格等于clamp(input,0,1)**，353,271个HDR通道全部成为1 |
| 同一输入，原有参考强度开启 | Evaluate成功，仍无任何>1输出，最高1 |

所以“接受浮点纹理、Evaluate返回Success”不能证明原生HDR语义支持。强度归零对照还排除了“只是模型主动改了高光外观”作为完整解释：在这条公开调用配置里，超1值已被钳制，最大166→1。

**工程结论：当前版NR前必须提供SDR兼容的显示域/有界代理。** 不强制只能用本页ACES曲线，但不能用更换浮点格式、HDR10/PQ编码或改swapchain色彩标记代替经过验证的模型输入合同。原始HDR可以旁路保留，整条游戏渲染链不需要变成SDR。

这条结论限定于当前正式版本/公开接口，不宣称证明了所有私有或未来版本永远不支持HDR。公开研究论文下载HTTP403，未读到正文；Unity社区说“不支持HDR”的内容仅搜索摘要可见，不作为官方合同证据。当前结论由官方源码、公开专用接口及本机直接运行共同支撑，无需继续寻找隐藏开关。

证据：[接口审计与有界结论](../artifacts/research/official-nr/hdr-contract-confirmation.json)、[直接HDR准备文件说明](../artifacts/research/official-nr/frame-20261028-direct-hdr/input.json)、[零强度调用](../artifacts/research/official-nr/direct-hdr-neutral.json)、[零强度数值检查](../artifacts/research/official-nr/direct-hdr-neutral/direct-hdr-checks.json)、[正常强度数值检查](../artifacts/research/official-nr/direct-hdr-active/direct-hdr-checks.json)。

### 用户限定与停止条件

用户要求：如果需要深入修改工具链则不做。本轮只扩展已有独立探针的零强度诊断入口与离线格式准备/检查；未修改SDK、NGX core、模型、驱动或游戏。

- 不做模型逆向/patch、重训练、私有ABI试探或隐藏参数枚举，也不为原生HDR改SDK/core。
- 不为此重写UE4渲染器、替换游戏整套tonemapper/调色工具链或导入大型框架。
- “原生HDR直入NR”分支已停止，合同核查完成；用户后续授权先评估并验证局部GPU颜色往返，不重启隐藏模式探索。
- 公开接口上的局部SDR代理/原HDR保留方案可作为后续候选，但GPU实现、跨API/游戏回接的复杂度仍须单独评估；若超出窄适配范围就停止该路线，不能把已有数值验证当作继续扩张的理由。

[probe_hdr.py](../tools/nr/probe_hdr.py)可复现格式准备/统计；`preflight-nr.exe d3d12-hdr-neutral ...`只把Intensity、local tone、local structure、skin structure设为0，Enabled保持1，其他参数及资源状态沿用已验证探针。相同输入正常强度使用原有`d3d12-evaluate`，未重命名模块或修改模型。系统NGX初始化自身的既有缓存副作用仍适用。

## 结果与范围

使用正常飞行捕获的真实R11G11B10_FLOAT场景和实际曝光增益，已完成**CPU准备SDR代理 → 正式NR的D3D12 GPU求值 → CPU恢复HDR**。关闭NR的中性往返在浮点阶段零变化，重新打包原格式后全部3,686,400像素逐位一致。开启NR后，HDR输出全部有限，最高166保留，20,566个完全高光保护像素不变。

本节保留当时CPU准备/恢复实验，不是连续GPU链或最终Pre-SR画质结论。随后已完成两个局部GPU颜色pass与AC7中性离线回接，见页首链接；尚未验证NR真正消费GPU代理后的原后处理/曝光反馈与时域。

## 参考来源与发现

官方Remix固定提交`890740c237e72cf9df97438605e9582a8f8820f9`：

- `tonemapping_fast_tonemapping.comp.slang`：曝光后ACESFilmApproximation，再linearToGamma，生成SDR代理。
- `tonemapping_inverse_tonemapping.comp.slang`：保留原始HDR，按模型SDR结果与原始tone-map值的比例恢复；可用高光mask混回原始HDR，避免让NR修改被曲线压平的高光。
- `tonemapping.slangh`：ACES近似为`saturate(x*(2.51*x+.03)/(x*(2.43*x+.59)+.14))`。
- `color.slangh`：这里的gamma是**pow(1/2.2)、pow(2.2)**，不是分段sRGB。没有把Overglaze另一套颜色codec误写成该公式。

后两份新增源码已核对官方Git blob，见 [来源与输入合同](../artifacts/research/official-nr/frame-20261028-hdr/input.json)。工具保留NVIDIA MIT授权通知；没有把SDK或模型并入可提交源码。

Overglaze的pre/post ratio实现也保留HDR并转移模型改动，但其颜色算子、分段sRGB、比例限幅与其他扩展不是本轮采用的官方公式。没有引入完整Overglaze框架。

### 中性往返的实际问题与窄修正

首次边界自检失败：官方公式的分母下限`max(T,1e-6)`使极暗通道在低曝光时往返变暗，FP16 SDR代理还会引入小量化偏差。原始失败日志保留在`artifacts/research/official-nr/hdr-initial-self-test.log`。这不说明官方实现不能使用，而是它没有对所有亮度承诺中性往返严格恒等。

本项目参考公式只转移**NR相对实际FP16输入代理的改变量**，而不是重建整张HDR：

```text
X = 保留的原始HDR
E = 当前帧实际曝光增益
T = ACES(E * X)
C = FP16(pow(T, 1/2.2))       // 实际给NR的代理
N = NR(C)                    // 保留模型自己的SDR输出，不改模型历史
M = 官方高光保护mask

Y = X + (1-M) * (pow(N,2.2) - pow(C,2.2))
                 * (E*X / max(T,1e-6)) / E
```

结果限制在有限非负FP16范围。**这是本项目中性修正，不是声称官方逐式等价**。N=C时改变量严格为零，包括黑色、极暗和FP16量化后的代理。真实功能关闭时仍应直接旁路，不运行NR或转换；本轮关闭NR测试是让代理直接作为中性输出，不伪称测试了运行时Enabled=0行为。

没有增加比例限幅、颜色矩阵、预设选项或曝光默认值。输入NaN/负值/超界曝光会拒绝，不能用曝光1掩盖未知输入。

## 输入

| 数据 | 本轮来源 |
| --- | --- |
| HDR场景 | 正常飞行捕获Draw467后的真实scene-hdr.raw，原分配5120×1440，裁[1280,0,2560,1440]；RGB约0.00235至166 |
| 曝光 | tonemap实际读取的1×1资源R通道，**1.0000050067901611**；保留原始数据，不替换为1 |
| SDR代理 | CPU float32官方ACES/gamma公式，存RGBA16F；不是上一轮游戏post-tonemap/LUT输出 |
| 深度/速度 | 复用同帧已校验的规范化深度和向后像素速度，保持原文件字节 |
| NR设置 | 同此前单帧测试：Reset=1、jitter=(0,0)、DepthInverted=1、同尺寸原点0、Style0与固定强度 |
| HDR恢复 | CPU float32中性修正与官方高光mask，存RGBA16F |

源HDR快照不是已确认的最终Pre-SR插入点。曝光近1只证明这个样本，不能外推动态曝光、working primaries或pre-exposure完整契约。引导方向及反向Z候选的限制沿用 [首次单帧求值](AC7-NR-Single-Frame-Evaluation.md)。

## 关闭NR：全图对照

| 检查 | 官方比例恢复 | 本项目中性修正 |
| --- | --- | --- |
| 浮点发生变化的像素 | 3,665,828 | **0** |
| 最大浮点相对通道偏差 | 0.000994176（约0.0994%） | **0** |
| 最大绝对HDR偏差 | 0.00134349 | **0** |
| 重新打包R11G11B10后变化像素 | **0** | **0** |
| 最大HDR值 | 166 | 166 |

本帧官方偏差没有跨过原格式量化边界，不应写成“官方公式损坏本帧”。中性修正还覆盖了边界自检中官方epsilon下限导致的暗部损失。黑色、低曝光极暗、普通亮度、高光、FP16及原格式打包自检通过。

[关闭NR原始统计](../artifacts/research/official-nr/frame-20261028-hdr/roundtrip-checks.json)。独立字节比较确认original-hdr.raw与hdr-roundtrip.raw SHA256相同：`171adc85d9aef41f8921853a27b60c0cd21cc75a68f83c43cda9342983726904`。

## 开启NR：模型执行与HDR恢复

正式NR310.9.16.0，RTX5080/617.42，官方loader、自建CUSTOM Project ID。Evaluate/GPU完成/清理成功，三张输入读回逐位一致，启用D3D12调试层，error为0。NR代理输出11,059,200个RGB通道全部有限且非恒等。这次确实执行模型，没有复用上一轮不同SDR输入的输出。

| HDR检查 | 数值 |
| --- | --- |
| 有限RGB通道 / 无效像素 | **11,059,200 / 0** |
| 恢复后>1的RGB通道 | **342,807**，没有强制夹到SDR范围 |
| 最大恢复HDR值 | **166** |
| 完全受保护高光像素 / 其中变化像素 | **20,566 / 0** |
| 含部分高光保护的像素 | 152,242 |
| 相对原HDR平均绝对变化 | 0.00781334 |
| 最大绝对变化，有/无高光恢复 | **1.005859 / 14.125** |
| 最大HDR值，有/无高光恢复 | **166 / 158.75** |

这些差异不是ground-truth误差，不能用更小差异判定NR画质更好。没有高光恢复的结果由**同一份模型输出**做CPU对照，未重复调用模型。

暗部仍有画质门槛：7181个通道增亮>2倍，7个>10倍；最强相对变化位于(1087,1054)红通道，从0.0047607421875到0.0655517578125，约13.77倍、绝对变化0.060791。它不是浮点溢出，也不能直接判为可接受；没有为了通过数值检查把增益限幅。

预览检查没有明显黑屏/整体错位，飞机和海岸布局保留。预览把恢复HDR再次通过**本轮测试ACES/gamma曲线**显示，不是AC7实际3D LUT、bloom和最终tonemap，因此不能当作最终游戏截图。

- [三阶段对照预览](../artifacts/research/official-nr/evaluate-20261028-hdr/comparison.png)
- [HDR恢复预览](../artifacts/research/official-nr/evaluate-20261028-hdr/hdr-preview.png)
- [NR调用原始记录](../artifacts/research/official-nr/evaluate-20261028-hdr.json)
- [NR输出检查](../artifacts/research/official-nr/evaluate-20261028-hdr/checks.json)
- [恢复公式及高光对照统计](../artifacts/research/official-nr/evaluate-20261028-hdr/hdr-checks.json)
- [独立Python HDR读回检查与暗部坐标](../artifacts/research/official-nr/evaluate-20261028-hdr/hdr-readback-checks.json)
- [阶段断言汇总](../artifacts/research/official-nr/hdr-validation-checks.json)

## 代码与后续

新增 [hdr_reference.cpp](../tools/nr/hdr_reference.cpp)：离线CPU准备/恢复和合成自检；SDK自带DirectXPackedVector负责原格式/FP16打包，没有新增依赖。[check_hdr.py](../tools/nr/check_hdr.py)独立解码输出检查高光与暗部，复用既有PNG写入函数。模型仍由原有D3D12探针执行，不引入宿主框架。

```text
hdr-reference.exe --self-test
hdr-reference.exe prepare <AC7正常帧artifact目录> <既有SDR测试guides目录> <新的HDR准备目录>
preflight-nr.exe d3d12-evaluate <正式NR目录> <新的求值目录> <自建GUID> <HDR准备目录>
python tools/nr/check_frame.py <HDR准备目录> <求值目录>
hdr-reference.exe restore <HDR准备目录> <求值目录>
python tools/nr/check_hdr.py <HDR准备目录> <求值目录>
```

当前已确认：NR310.9.16.0的公开调用路径不能直接保留场景HDR；零修改强度时输出RGB严格等于clamp(input,0,1)。原生HDR直入分支关闭，不再搜索隐藏参数或深入修改工具链。

局部GPU准备/恢复和配对实际NR非中性离线回接已通过，见页首记录；它不同于修改工具链，也不等于实时资源桥。下一步核实SR HDR合同及真正插入/高输出回接。若需改模型/SDK/core、重写渲染器或整套tonemap工具链就停止，不改变普通屏幕原生数据目标。正式SR/FG、时域和实际性能仍是独立未知项，不能把参考结果等同全链路可用。

本轮原始capture/readback、游戏文件保持原状，无Git提交。初始化仍可能触发官方NGX缓存维护/更新器，见 [已知副作用](AC7-NR-Runtime-Preflight.md#ngx本身的副作用)；未手工修改系统或OTA设置。
