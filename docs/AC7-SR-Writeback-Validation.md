# SR同尺寸离线回接：保留原空间AA与UI

> [画质评估决策](AC7-Image-Quality-Evaluation.md)：变化像素/梯度/亮度差异不是质量分数；NR预期改变画面，FXAA平滑也不能单凭静态数值判坏。只在具体可见异常和连续帧证据下判断缺陷，不据这些统计限幅或阻塞推进。

> 最新[prewarp新配对链](AC7-Prewarp-NR-SR-Validation.md)已用新输入重新实际NR/SR求值并在465后回接，原warp/后处理/UI保留；本文467后与FXAA结果仍是真实历史证据。当前下一步真实连续帧/jitter/history方案，不再重复本页旧位置模型求值。

> 最新[prewarp候选边界](AC7-Prewarp-Boundary-Validation.md)验证后续模型应优先放465后/466前；原warp改变颜色但不改变depth32/MV，stencil另有变化。新边界中性15项逐位，尚未新跑NR/SR。本文467后模型/回接/FXAA对照保持为真实历史证据，不复用其输出到不同prewarp输入。

> 后续FXAA保留/旁路对照也已完成，见本页末尾。有效区RGB旁路、原alpha/黑边契约保留；上游及三张UI逐位不变，最终78,379个像素受FXAA影响、区外0。仅证实该Reset帧存在额外空间平滑，不验收动态锯齿/闪烁或性能。后续SR实验可将FXAA旁路作为候选，同时保留原FXAA对照。

## 已完成的结果

**同一真实GPU代理的NR→HDR恢复→SR(HDR、Reset、P1)→原曝光/tonemap→原空间AA→后处理/UI→最终帧，已通过离线文件衔接完成回接。** SR旁路12项raw与前一NR回接结果逐位一致；实际SR写入416,948个有效区HDR像素，最终帧896,406个像素变化，所有检查点有效区域外变化0，三张UI合成资源逐位一致。不是实时适配器、跨API实时桥、时域或高输出验收。

用户提供：**游戏画质菜单只有FXAA，没有TAA选项。** 捕获中Draw526位于tonemap之后、只采颜色、承担空间抗锯齿，与用户信息一致，以下以原FXAA称呼它；不是仅凭菜单信息就完成shader源码身份认证，也没有据当前菜单重建捕获时枚举设置。前面Draw427虽有历史/速度/曝光权重，但输出辅助纹理供后续场景着色器使用，不把它命名为全场景TAA。

预曝光调查按用户要求延后：P=1为暂用实验值，已有单帧未见明显整体异常；本轮不追P、不改SR参数或重跑NR/SR模型。仍检查最终亮度/布局，并保留高光、动态曝光和时域风险。

## 插入点及边界

本轮沿用已验证NR回接检查点Draw467之后：Scene210的最后直接writer结束、原曝光测量和最终tonemap之前、UI合成之前，能让同尺寸HDR输出继续走原管线。资源/rect均与SR已保存输入匹配，工作区域为5120×1440中的中央`[1280,0,2560,1440]`。

**这是离线回接候选点，不是最终运行时hook选择。** Draw466已根据辅助纹理warp取Scene210，Draw467复制回来；当前depth/MV未额外做同样warp，透明/云/水面等实际guide语义仍有待时域检查。当前结果只证明同一帧、同尺寸颜色资源可以正确写回；不能因此验收最佳Pre-SR位置或guide对齐，也不擅自将SR移到一个尚无对应颜色读回的新位置。

本轮保留Draw526原FXAA。这样先验证原后处理连接，仅增加SR一个变量；**DLAA后叠FXAA可能额外平滑**，没有宣称这是最终抗锯齿组合。后续应在SR回接已成立基础上做明确的FXAA旁路对照，再决定替换策略，不能提前假设关FXAA后最终UI/效果仍相同。

## 实现与身份校验

1. [prepare-sr-writeback.cjs](../tools/nsight/prepare-sr-writeback.cjs)校验完整源场景、SR输入合同、SR输入/输出哈希和Reset/P1标记。将SR非负有限FP16转换到R11G11B10 packed，round-to-nearest/ties-to-even，无曝光/调色变换；恒等组直接裁原packed字节。half→目标格式自检遍历两种mantissa的全部有限可表示码及tie情况。
2. [prepare-sr-replay.cjs](../tools/nsight/prepare-sr-replay.cjs)复用既有HDR重放生成器，只建立新的`build/sr-hdr-replay-source`，在NR恢复后加入SR回写，添加Draw526后读回；原捕获源码/data.bin不改。
3. [replay_sr.cpp](../tests/d3d11/replay_sr.cpp)通过`AC7_SR_EXPECTED_SCENE`、`AC7_SR_PACKED`读取显式raw，校验头/长/场景布局；GPU staging读回**整张NR后HDR并逐位比较**，一致才上传SR packed中央区域。无需新shader，不调用NGX，不改调色或渲染器。资源Copy不改变管线绑定。
4. [check-sr-writeback.cjs](../tools/nsight/check-sr-writeback.cjs)核对旁路全部前次raw、实际场景每行等于SR packed、有效区外、三张UI、后处理/最终帧变化和曝光读回。

这条路径仍用CPU staging/磁盘衔接D3D11与D3D12。SR求值本身是上一阶段的正式GPU运行，本轮只是消费其已保存输出：

- NR后源场景SHA256 `b584f7abda065cef8be933c1f118b69779b2aecb9b7432bcef373f7ae1b6754e`
- 对应SR输入SHA256 `6e36496f508447158b571fc5bab087437f6a472a38b6abb61e9487859163d881`
- 对应SR输出SHA256 `6a09ed6d136f6034b5f3f5b5c4233af1a2b8f5d748443c4e3bb5b326fca73540`
- SR packed SHA256 `8a61070406963b4d6e65ef213c5ee0b4aa862f93b4e595a9bb78404d043cae59`

输入合同：[writeback-input/contract.json](../artifacts/research/official-sr/writeback-input/contract.json)。SR输出FP16有2,476,329个RGB变化像素，重新量化到较粗的原R11G11B10后只有416,948个packed像素不同；二者比较域不同，不是模型输出被替换。

## 检查结果

| 检查 | 结果 |
| --- | --- |
| 派生工程Release构建 | 成功 |
| NR后HDR→恒等SR回写→原后处理 | 重放退出0；前次全部12项raw逐位一致 |
| NR后HDR→实际SR输出回写→原后处理 | 重放退出0，源整图逐位核验通过 |
| SR有效区写入与packed输出 | 逐行逐位一致 |
| HDR有效区变化 / 区外 | 416,948 / 0 |
| post-tonemap变化 / 区外 | 953,722 / 0 |
| 原FXAA后变化 / 区外 | 897,681 / 0 |
| 后处理后变化 / 区外 | 897,681 / 0 |
| UI合成及最终帧变化 / 区外 | 896,406 / 0 |
| 三张UI输入 | ui-layer、ui-aux-849、ui-aux-850逐位一致 |
| 曝光before/after/tonemap使用 | 都与旁路逐位一致，R=1.00000500679、G=1 |
| packed SR HDR | RGB全部有限，max=103，334,032通道>1 |
| 本轮新增模型求值 | 0 |

packed max103是先前FP16 max102.8125重新量化到R11后的值，不是新的固定HDR限幅证据。当前曝光增益仍相同只代表这帧（原限幅条件仍存在），不代表动态曝光稳定。此重放检查没有独立D3D11 debug-layer0结论，不能把前次D3D12模型debug0嫁接成当前重放debug0。

负向测试故意把NR后场景的expected换成原始NR前场景，重放退出1并报`SR expected full scene mismatch; refusing writeback`，没有scene-hdr dump或SR回写后结果。见[负向运行](../artifacts/research/official-sr/writeback-source-mismatch/run.json)。未修改模型或输入以掩盖身份错误。

[完整raw检查](../artifacts/research/official-sr/writeback-checks.json)、[恒等运行](../artifacts/research/official-sr/writeback-identity/run.json)、[SR运行](../artifacts/research/official-sr/writeback-active/run.json)。

## 最终画面与当前下一步

[原游戏tonemap/UI后的最终对照](../artifacts/research/official-sr/writeback-comparison.png)，上：NR+SR旁路+原FXAA；下：NR+实际SR Reset(P1)+原FXAA。按最终R10G10B10格式正确解码，裁有效区、保持16:9，不是之前ACES代理预览。助手检查飞机/海岸/云/绿色HUD保留，无明显黑屏或整体错位；海面细高光仍有衰减，不能由缩略图验收细节/时域。

初次预览脚本按BGRA格式检查最终raw，格式断言失败；raw实际format24/R10G10B10A2，随后按10bit分量解码生成上述预览。未改raw、未重跑模型，失败与成功日志分别保存。并非读回失败或模型异常。

## 后续FXAA保留/旁路对照

本轮只改变原Draw526的颜色效果，使用同一NR→SR结果/packed SHA256，P1继续暂用，0新增模型求值。新建`build/sr-fxaa-replay-source`与派生EXE，未改原捕获或游戏设置。

[replay_sr.cpp](../tests/d3d11/replay_sr.cpp)新增`ValidateFxaaReplay`，生成器在Draw526后调用；未设置环境变量时完全保留原行为，显式`AC7_SR_BYPASS_FXAA=1`才替换颜色。这个离线控制仍执行原draw，借CPU staging读回两张BGRA8纹理，**仅在中央有效区替换RGB，保留原FXAA输出alpha与区外全部字节**，再交原后处理/UI。复制回资源不重绑定管线，没有新增shader/依赖，也不声称取消原draw开销或得到运行时性能收益。

### 先修正alpha契约，再比较FXAA

第一次整张CopyResource虽然视觉黑边仍是黑色，但检查发现Draw525输出alpha并非普通不透明alpha：PS48329行90明确`dp3_sat o0.w, r1.rgb, [0.299,0.587,0.114]`，有效区是亮度，区外为255；原Draw526有效区alpha=255，区外为0。直接复制会改变全部3,686,400个区外像素的alpha，违反原输出契约，因而**拒绝作为合格对照**。

修正后只替换有效区RGB，alpha保持原Draw526值、区外不改。最终检查逐像素断言这些条件，不以“看起来差不多”代替。初次运行保存在`fxaa-bypass/`，正确运行使用`fxaa-bypass-v2/`，未覆盖失败证据。初次构建也因把设备状态检查写在context上报C2039，修正为通过device检查后构建成功；原始/修正构建日志保留。没有工具链或模型修改。

### 对照结果

[check-fxaa-comparison.cjs](../tools/nsight/check-fxaa-comparison.cjs)及[完整结果v2](../artifacts/research/official-sr/fxaa-checks-v2.json)：

- 默认保留路径13项raw（含新加post-fxaa）与上一轮SR回接逐位一致，说明添加控制未扰动默认结果。
- 两组场景HDR、测光缩减、曝光before/after/tonemap、post-tonemap及三张UI资源全部逐位一致。
- 旁路后的有效区RGB逐字节等于post-tonemap RGB；所有alpha及区外字节逐位保持原FXAA输出。
- post-fxaa及post-effects有77,953个像素变化；UI合成及最终帧78,379个像素变化，**区外0**。
- 最终有效区平均绝对RGB差异约0.08898/255，最大单通道差异约98.96/255；平均小不代表每个边缘像素差异都小。

为避免HUD覆盖影响局部统计，以下取**post-effects、UI合成前**相同区域的显示编码亮度：

| 区域 | FXAA保留/旁路的平均相邻亮度变化比例 | 平均亮度绝对差异（8bit尺度） |
| --- | --- | --- |
| 飞机尾翼 | 0.97629 | 0.21709 |
| 飞机轮廓区域 | 0.97646 | 0.17111 |
| 海面高光区域 | 0.93371 | 0.79740 |
| 天空/云层选区 | 1.00000 | 0 |

比例小于1支持FXAA在SR之后仍额外平滑这些区域；**不是锐度ground truth、重建正确性或时域锯齿评分**。旁路并不能恢复前面SR已经衰减的细高光，也不说明关闭FXAA就一定更好。

[最终图及局部4×对照v2](../artifacts/research/official-sr/fxaa-comparison-v2.png)左侧保留、右侧RGB旁路；整帧显示最终R10输出，局部使用UI前BGRA8且nearest放大，避免把雷达覆盖当海面细节。v1统计/图片使用最终合成后的局部区域，保留为历史，结论以v2为准。助手查看飞机/海岸/云/HUD布局保留，无明显整体异常；静态图不能衡量运动闪烁。

### 当前决策与下一步

**后续SR实验可优先使用FXAA RGB旁路候选，保留原FXAA作对照**，避免把双重抗锯齿额外平滑误归因SR。仅是实验选择，没有关闭用户游戏内FXAA或确定最终产品默认值；最终选择应随连续帧边缘稳定性验证。

接下来转向候选点颜色/guide对应，尤其Draw466 warp与透明/水面覆盖，再制定真实相邻帧及合法jitter/history/reset输入方案。FXAA菜单没有TAA选项，不代表DLSS需要的抖动和时域历史已具备；当前捕获jitter0只作为实际单帧事实，不伪造成完整时域验证。原SDK/模型、游戏配置与捕获均未修改，无Git提交，预曝光继续延后。

## 原回接阶段的下一步（历史）

下一步优先做**原FXAA保留/旁路的同尺寸对照**，区分额外空间平滑影响；再明确候选位置与guides的warp/透明语义，制定真实连续帧输入方案。最终超分需另处理allocation、viewports、subrect、UV及后处理采样，不能只换SR目标尺寸。FG和实时桥仍未实现，不提前选宿主；原游戏、SDK和模型均未修改，无Git提交。
