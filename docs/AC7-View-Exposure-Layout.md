# AC7 View常量：旧版曝光字段与新版预曝光的区分

> 用户最新决策：预曝光调查延后。已有P=1单帧诊断无明显整体异常，后续实验保留P=1假设，优先SR真正插入位置/回接及连续帧；本页后续追P建议保留为历史研究路线，不是当前阻塞条件。游戏真实P仍未确认；后续若有明显亮度/曝光或历史异常再重开此项。

本轮获得了比版本字符串更具体的布局证据：**AC7实际View CB的局部连续字段与公开UE4.18 GameWorks分支旧布局相符；该旧布局的`ExposureScale`不是新版`PreExposure`。不能按UE4.22偏移读取，更不能把读到的1.333333当作预曝光。** 游戏真实P仍未确认，本轮没有GPU/NGX调用或新增接入代码。

## 来源与适用边界

- 官方`EpicGames/UnrealEngine`的4.18.3-release `SceneView.h`地址在当前未认证访问下返回404；这不证明文件不存在。没有尝试绕过登录，也未取完整引擎。
- 公开第三方`windystrife/UnrealEngine_NVIDIAGameWorks`的`4.18-GameWorks`分支：Build.version实际MinorVersion=18；取`SceneView.h`、`SceneRendering.cpp`、`SceneView.cpp`、`BasePassPixelShader.usf`、`PostProcessTonemap.usf`作有限参考。它是修改过的GameWorks分支，**不是AC7或官方未修改4.18的认证来源**。
- 公开第三方`folgerwang/UnrealEngine`的release分支：Build.version实际MinorVersion=22；仅用于对照新版字段，不外推至AC7。
- URL、返回码、内容SHA256及命中行在[源文件获取记录](../artifacts/research/ue-view-layout/source-fetch.json)、[旧布局获取记录](../artifacts/research/ue-view-layout/gameworks-fetch.json)、[实现获取记录](../artifacts/research/ue-view-layout/implementation-fetch.json)、[Renderer获取记录](../artifacts/research/ue-view-layout/renderer-fetch.json)。分支URL可能变化，当前判断绑定已保存字节哈希；尚未独立认证这些镜像的完整上游谱系。

初始网页搜索误设近期过滤，结果主要是新版/无关项，没有用于论证4.18行为。实际比较依据下载保存的具体参考源码及捕获字节，不据搜索摘要确定版本或实现。参考源码仅在忽略的研究产物中保存，未纳入项目编译/分发。

## 为什么旧字段不能当作预曝光

[旧SceneView.h](../artifacts/research/ue-view-layout/old-view-layout.h)行625–637的顺序：

```text
ClipToPrevClip
TemporalAAJitter
GlobalClippingPlane
FieldOfViewWideAngles / PrevFieldOfViewWideAngles
ViewRectMin
ViewSizeAndInvSize
BufferSizeAndInvSize
NumSceneColorMSAASamples / ExposureScale
DiffuseOverrideParameter
```

旧header没有`PreExposure`/`OneOverPreExposure`成员。更关键的是[旧SceneRendering.cpp](../artifacts/research/ue-view-layout/old-scenerendering.txt)行979–980明确：

```cpp
float ExposureScale = FRCPassPostProcessEyeAdaptation::ComputeExposureScaleValue(*this);
ViewUniformShaderParameters.ExposureScale = ExposureScale; // Only used for MobileHDR == false
```

这说明旧字段有特定参考用途，**不等于证明AC7场景HDR存储乘过该因子**。没有仅按名字把它赋给NGX PreExposure。

[UE4.22参考header](../artifacts/research/ue-view-layout/new-view-layout.h)行605–617则在这一段包含`BufferBilinearUVMinMax`，之后是`NumSceneColorMSAASamples`、`PreExposure`和`OneOverPreExposure`。新版确实存在预曝光字段，但这一段已与旧布局不同。

## 与实际捕获对齐

[audit-view-layout.cjs](../tools/nsight/audit-view-layout.cjs)验证motion consumer实际绑定的View blob123，仅从原始data.bin定点读取384字节，未改capture。结果见[布局审计JSON](../artifacts/research/ue-view-layout/capture-layout-audit.json)。

| AC7 View row | 实际值 | 局部旧布局对应候选 |
| --- | --- | --- |
| 110–113 | 已验证clip到previous clip矩阵 | 已有运动规范化证据 |
| 114 | (0,0,0,0) | 已验证当前/前帧jitter |
| 125 | (1.0995575,0.6638917,1.0995575,0.6638917) | 当前/前帧FOV角 |
| 126 | (1280,0,0,0) | ViewRectMin，与实际viewport一致 |
| 127 | (2560,1440,1/2560,1/1440)，浮点精度 | ViewSizeAndInvSize |
| 128 | (5120,1440,1/5120,1/1440)，浮点精度 | BufferSizeAndInvSize |
| 129.x | 按uint解释为1 | NumSceneColorMSAASamples候选 |
| 129.y | 1.333333373 | 旧ExposureScale候选，**不是已确认P** |
| 130–132 | 都是(0,0,0,1) | 三组材质override候选 |

这里不只是“某个数恰好为1”：尺寸、倒数、rect、整数sample数和紧接的override序列构成多字段对齐。与此同时，AC7在jitter与FOV之间有额外矩阵状字段，**完整布局并不等同参考引擎，不能全局套偏移**；上述仍是有依据的局部字段命名候选。

对Scene210全部156个直接writer做实际View绑定检查，扫描32个已绑定该View的唯一PS/槽位组合，**未找到字面row129访问**。其他已见row130–133的override访问与局部旧序列一致。该检查有明确限制：不是完整反汇编数据流分析，不覆盖动态CB索引、上游其他资源、compute、其他View或游戏自定义路径，因此不能以0命中直接证明全游戏无预曝光。

旧tonemap参考也具有EyeAdaptation R在VS加载、在PS乘场景色的路径，与已实测AC7曝光路径相容；但这是辅助相似性，不是游戏源身份或额外预曝光不存在的证明。

## 后续：场景测光、适应与终端颜色的两端核对

新增[audit-exposure-dataflow.cjs](../tools/nsight/audit-exposure-dataflow.cjs)，[完整机器可查结果](../artifacts/research/ue-view-layout/exposure-dataflow-v3.json)。不是再次执行GPU，使用原始捕获中已保存的shader和读回值，并定点读取当前绑定CB的96字节。

### 曝光端已明确的运算

实际资源链为Scene210→Draw469采样→471四点平均→473编码对数亮度→475/477/479/481继续平均→482适应曝光→525 tonemap使用。每一级t0对应前一级输出，按索引实际关联，不能把仍留在其他槽位的无关绑定算作读取。

Draw473/PS55832行27–32：

```text
Y = dot(sampledRGB, [0.2126,0.7152,0.0722])
alpha = a*log2(max(Y, floor))+b
```

绑定blob731 row20实际`a=0.0833333358`、`b=0.6666666865`、`floor=0.00390625`。它直接对采到的RGB求亮度，没有在该shader内单独除某个P。中间平均shader只采t0，alpha随四个采样平均传递。

Draw482/PS55854使用相同a/b解码，完整公式：

```text
target = clamp(6.25*exp2((weightedMeanAlpha-b)/a), min, max)
previous = scale / oldExposureR          # oldR=0时用1
blend = 1-exp2(-rate*dt)
adapted = clamp(previous+(target-previous)*blend, min, max)
newR = scale / max(adapted, 0.0001)
newG = scale / max(target, 0.0001)
```

实际blob744：范围[0.5,1]、scale=1、dt=0.008333601秒、两个适应速率3/1。CPU预测`[1.0000049207733401,1]`，对现有GPU读回误差`[8.6016821e-8,0]`。因此本帧的测光/适应路径和最终曝光增益已数值对上，**这些式子没有单独识别出来的P参数，但CB参数在CPU端的生成来源未完全证明**。

### 曝光纹理不只在tonemap使用

按所有draw的PS/VS声明纹理槽位筛选，再与实际槽位资源关联；547draw无缺失shader实例，已知旧/新曝光资源出现四处：

| Draw / stage | 资源 | 已核对用途 |
| --- | --- | --- |
| 427 VS1719 | 旧曝光uid1597 | 载R传TEXCOORD1.x；PS1720行127–136进入`1/(value*gain+4)`形式混合权重，用于带深度/MV/历史资源的辅助时域滤波 |
| 482 PS55854 | 旧曝光uid1597 | 通过倒数还原previous，再适应 |
| 484 VS48303 | 新曝光uid55850 | 传入PS48304，行31参与曝光相关阈值运算 |
| 525 VS1719 | 新曝光uid55850 | 已确认乘入场景RGB后再进LUT/tonemap |

**不能把“采了曝光纹理”一律判为场景预曝光。** Draw427是辅助资源生产/时域混合，不据此改称整个Scene210已经TAA，更不能误判它在全场景乘P。该资源扫描是已知UID的声明PS/VS采样，不覆盖资源别名、compute、CPU先写CB或其他曝光资源。

### 颜色生产端的六个实际末端

主会话逐行复核并让脚本检查原文和哈希：

| Draw / PS | 最后RGB运算 | 能证明什么 |
| --- | --- | --- |
| 33 / 55759 行210 | `o0.rgb = View[138].x*r2.rgb + r5.rgb` | 实际绑定blob123 row138.x=0，所以本帧该项为0、输出来自r5；不是把全部结果统一乘P |
| 422 / 55794 行341–343 | 标量r0.w*r3.w，再乘CB1[1].rgb，与r1.rgb相乘写o0 | 实际blob150的向量为(7.0282154,5.7998528,4.9534063)，不是单个等通道P；仍可能折入其他尺度，语义待上游验证 |
| 428 / 48299 行260–267 | 合成r1，乘r0，再非负限制后mov o0 | 复杂上游项，不能仅因最后mov就说无缩放 |
| 448 / 57512 行735–737 | 合并r11*r3+r0，再加r8，最后`o0.rgb=r0.rgb*v3.w+v3.rgb` | 存在顶点插值贡献，不能视作copy |
| 466 / 50261 行28–37 | t1决定warp坐标/边界，t0取色写o0 | 本shader不另乘RGB公共曝光；上游仍未证明 |
| 467 / 50263 行24 | t0直接采色写o0 | 同上 |

生产侧覆盖6个代表性唯一PS，**未完成全部33个PS及上游纹理/VS的数据流证明**。Draw448的VS58148行229–230写出`o3.rgb=r0.rgb*r1.w+r1.rgb`、`o3.w=r2.w*r1.w`，已把PS最后的v3追到实际VS输出，但尚未命名更上游系数语义。既有row129扫描和本次已知曝光资源扫描没有发现“把旧ExposureScale1.333333统一乘入这些场景RGB”的直接证据，但不以阴性结果确认P1。

子代理初稿有格式/着色器混淆（把448误作466的warp），主会话拒绝该部分并resume一次要求更正。[保留的修订摘要](../artifacts/research/ue-view-layout/producer-terminal-scout-revised.md)仍有422最近指令转述不精确、开头writer计数措辞不清；**以主会话逐行检查的上表/JSON为准**，未将child摘要直接当验收。原run`3ec41a85-ad78-41e6-a344-e3236f637744`，纠正resume`dc14d6d4-427a-48a0-be97-7bc6656349d2`。未另起并行writer或GPU任务。

### 为什么现在仍不能由曝光端反推P=1

此前说明“生产端P与补偿E/P应能对上”是有用方向，但**不能要求一定出现显式除P指令**。如果对已经乘P的缓冲区直接测光，倒数曝光本身可能吸收该尺度，例如不考虑限幅/历史时：

```text
C = P*L
meterGain = 1 / measured(C)
C * meterGain = L / measured(L)
```

固定P可能在整个过程抵消；CPU也可能把补偿折进CB参数。因此本轮发现“测光直接看HDR、未看到显式除P”不能独立证明未预曝光。工具保留P1/P4算术反例自检，仅用于展示不可唯一反推，没有给游戏指定候选值。

**目前可审查的进展是：测光/适应/使用路径已对上，旧曝光纹理的四处用途已区分，六个生产末端已准确定位；真实P仍未识别。** 下一步应追剩余生产项的具体公共尺度来源，优先Draw33实际保留的r5、Draw422的light向量与448的VS插值上游来源，结合旧版对应源码语义；如果仍依赖未知CPU生成链，则需要受控曝光变化观测，而不是继续无边界增加阴性扫描。观测方案必须能固定场景/只改变曝光、导出HDR与候选常量并建立真实帧边界，再请求用户操作；当前不要求盲目抓帧。

本轮没有GPU/NGX/游戏调用、没有更改SR参数/捕获或游戏配置，没有提交。原P1仅为诊断假设，所有正式标定、时域和FG门槛保持。

## 本轮结论和后续获取路线

- 排除直接移植UE4.22 `PreExposure`字段偏移的方法；本轮没有新增猜测式reader。
- 找到AC7 row129.y=1.333333的旧曝光候选，并用参考源码限定其解释，**不把它传作P**。
- 更应优先验证“AC7是否走旧版未预曝光的桌面HDR路径”，而不是预设一定存在新版P字段。若对应编译路径最终确证未预曝光，P=1应来自路径证据，而不是单帧近unity曝光/字段缺失。
- 现阶段仍不能宣布P=1；需要覆盖主要桌面HDR生产路径和对应曝光补偿，或取得匹配游戏分支的可信视图字段语义。真实相邻帧可以帮助验证候选随曝光的关系，但单凭数值变化也不能命名字段。
- 不对SDK/模型/UE4做深入修改，不通过盲调P/自动曝光或重复单帧越过标定门槛；目前不要求用户重抓普通截图/F11。

一个只读Luna scout完成本地限定检索，确认Luma通用UE适配只设`pre_exposure=0`，没有4.18 View布局。报告已[本地保留](../artifacts/research/ue-view-layout/local-scout-evidence.md)，run`fc76a2ca-ea9f-477a-9088-a013f8e7947b`；主会话负责公开参考获取、实际CB/着色器复核和结论。未使用其阴性检索来证明AC7没有P。

本轮脚本包含绑定/实际字节长度、矩形/尺寸、源码字段序列断言和禁止覆盖检查；不改变原SR诊断假设或已有raw。未部署、未提交Git，时域、SR回接和FG仍未验收。
