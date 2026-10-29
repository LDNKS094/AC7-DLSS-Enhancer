# 正式DLSS SR：HDR输入合同与本机前置验证

> 后续[同尺寸SR离线回接](AC7-SR-Writeback-Validation.md)已通过源/输出配对、旁路12项、有效区外0变化和UI逐位检查，原FXAA暂保留。此页“未回接”为此前阶段记录；真实连续帧/高输出/实时仍未通过。

> 用户最新决策：预曝光调查延后。依据已有P=1诊断无明显整体异常，后续插入位置/回接及连续帧实验暂用P=1并保留假设标记，不以真实P未知阻塞。下方旧阶段的标定停止条件保留为历史证据；高光画质和动态曝光/时域仍须实际检查，出现异常再排查P。

> 最新[View曝光字段核对](AC7-View-Exposure-Layout.md)：旧UE4.18参考ExposureScale局部序列与实际AC7相符，候选row129.y=1.333333，但源码注释仅用于MobileHDR=false；不是已识别的P。UE4.22预曝光字段偏移不可直接使用，下一步优先核实旧桌面HDR路径是否未预曝光。真实P仍未确认。

> 本页保留正式合同、创建阶段和原生标定门槛。后续[显式独立输入域Reset诊断](AC7-SR-Reset-Diagnostic.md)已对原始/NR后HDR完成同尺寸求值/GPU读回；PreExposure=1只是诊断假设，游戏真实值、画质和时域仍未验收。不要将本页早期“未Evaluate”读作当前所有诊断均未执行。

## 当前结论

已取得并核验正式SR运行时`nvngx_dlss.dll` **310.9.16.0**。本机RTX5080/617.42通过官方NGX loader、自建CUSTOM Project ID实测：**D3D11与D3D12的SR能力均可用；D3D12的HDR/DLAA创建、提交/GPU完成及清理成功**。该前置阶段没有Evaluate，后续显式假设下的单帧诊断见页首链接；仍无原生输入标定、高输出或时域验收。

官方完整SR指南确认：线性/高动态范围颜色使用HDR模式，SR应放在主要后处理和tonemap前。它支持既定候选顺序：**NR的SDR代理→恢复场景HDR→SR(HDR)→原后处理/tonemap→FG最终数据**，但不能据此认定AC7的pre-exposure、真实jitter、插入点和高输出回接已经成立。

## 正式文件和身份

从此前已完整校验的官方Streamline v2.14.16 zip提取**release**条目`bin/x64/nvngx_dlss.dll`，未使用development条目。包SHA256为`32041e51db0cab709a271b72a352281b690c2abbd669a827809401a5dd23956b`，已与GitHub asset digest匹配。

```text
运行时：nvngx_dlss.dll
版本：310.9.16.0（Windows原始字段310,9,16,0）
大小：58,956,912 bytes
SHA256：d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e
Authenticode：Valid，NVIDIA Corporation
```

[身份记录](../artifacts/research/official-sr/runtime-identity.json)。运行时、SDK头/库和指南均保留在忽略的artifacts/build内，未随项目源码重新分发或部署游戏。

新增[官方SR完整指南](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/doc/DLSS_Programming_Guide_Release.pdf)，84页，6,207,258bytes，下载内容与固定提交Git blob匹配，[文件身份](../artifacts/research/official-sr/guide-identity.json)。页码以下均指**PDF物理页**，非印刷正文页码。

同时核对同正式zip中的`docs/ProgrammingGuideDLSS.md`，明确区分Streamline和直接NGX合同，未混用两者缩放参数。

## 合同与AC7证据逐项对照

| 项目 | 官方合同/证据 | AC7当前状态 |
| --- | --- | --- |
| HDR颜色 | PDF p.19–20：在主要后处理/tonemap前Evaluate；线性颜色或不满足LDR条件的输入设IsHDR。HDR算法不将可接受亮度限定为[0,1]。LDR必须是类似sRGB的感知编码，不能直接传线性颜色 | 真实R11G11B10场景最大166；NR恢复能保HDR，尚未完整标定working primaries/pre-exposure或SR实际输入格式 |
| 创建 | 官方`NGX_D3D12_CREATE_DLSS_EXT`映射render/output尺寸、PerfQuality、Create Flags、node mask等；IsHDR、MVLowRes、DepthInverted等为SR专用标志 | D3D12 2560×1440同尺寸HDR/DLAA创建实测通过；不是超分或图像求值 |
| 运动方向 | PDF p.26–29：current pixel + motion vector = previous-frame position，像素单位；直接NGX InMVScale用于换算到像素 | 现有规范化是current-minus-previous NDC，NR诊断已转(-x×W/2,+y×H/2)向后像素。公式方向与SR合同相符，但真实相邻帧独立验证仍缺失 |
| jitter | PDF p.33：每帧实际投影jitter，render/input像素单位，约[-.5,.5]，坐标/方向与MV匹配 | 仅捕获这一帧为0，不等于游戏恒为0，更不能造jitter序列 |
| depth | PDF p.35：默认near0/far1；DepthInverted用于near1/far0 | 硬件depth已逐位读取，反向无限远Z仍是有依据候选；未因此声明通用相机标定完成 |
| exposure | PDF p.35：HDR每帧需要正确曝光增益，乘输入后将中灰带到算法期望值；1×1首通道，建议R16F | 已确认游戏tonemap的实际R增益；这不自动证明其时机/数值正是最终SR输入要求 |
| pre-exposure | PDF p.37：若帧预乘该因子并在tonemap时除回，必须每次Eval传InPreExposure（CPU float，非曝光纹理），否则可能双重曝光/历史失配 | **尚未确认AC7是否使用及其真实值**；不会因为样本曝光近1就填写1 |
| D3D12资源状态 | PDF p.24：输入NON_PIXEL_SHADER_RESOURCE；输出UAV且需ALLOW_UNORDERED_ACCESS；SR返回资源至已知状态。具体格式须由实际API/特性支持验证 | 独立NR状态验证不能替代SR实际资源/输出验收；没有实时桥 |
| 尺寸与后处理 | Create分别设置render/output；Eval有render subrect及各资源偏移 | 当前场景分配5120×1440、有效2560×1440；高输出不能仅换纹理尺寸，采样、深度/MV和后处理常量/viewport仍未验证 |

`MVJittered`只描述MV是否包含jitter，**不是运动符号方向开关**。直接NGX的像素缩放与Streamline不同：SL common constants将MV缩放至归一化范围，像素数据通常用(1/renderW,1/renderH)。不能把这个SL值直接填进NGX InMVScale；指南与SR helper共同区分了两套合同。

## 现有AC7数据的门槛核查结果

后续已按用户边界做一次有界核查，**没有再跑SR创建、没有SR Evaluate或游戏注入**。三种门槛分开，避免把单帧和时域条件混为一谈：

| 门槛 | 现有证据 | 当前判断 |
| --- | --- | --- |
| 首个同尺寸Reset帧的jitter | 从实际捕获data.bin blob123、观测View CB row114重新读取current/previous四值，全为0；与既有motion分析一致 | 本帧零值有数据依据，不是默认值；不证明每帧恒为0 |
| 首个HDR SR原生接入的pre-exposure | 8个相关shader只证明曝光R在tonemap时乘入；EXE含UE4 4.18分支标记，未找到UsePreExposure/Override名称，配置和现有UE4SS日志无该值 | **仍不能证明不存在额外预乘因子，也不能确认数值1；原生标定仍阻塞，但不阻止显式假设下的独立Reset诊断** |
| 真正多帧SR | 两份捕获分属不同状态/时间，重放repeat是同一帧重置 | 缺真实相邻帧矩阵/jitter/MV/history；不把重放repeat当稳定历史 |
| 低输入/高输出回接 | 9个关键pass同时存在中央2560×1440、全5120×1440、2560×720、80×23及1×1 viewport；资源分配与有效rect不同 | 需明确viewport/subrect/UV和后处理采样，不能仅放大SR输出或把左右padding当高输出区域 |

### pre-exposure：现有证据的上限

同一只读scout（`magpie/auto/gpt-6-luna`）限8个相关shader提取证据，主会话核对后确认：

- Draw482的适应shader从亮度统计/旧曝光生成增益；该增益不是已经识别的pre-exposure。
- Draw525的VS从1×1曝光R加载至插值，PS实际将颜色乘此增益后进入LUT/tonemap。**没有看到一条已识别为“上游额外预乘、此处除回”的配对证据。**
- 若干HDR producer的乘法可能是材质、光照或alpha运算，不能凭乘法存在命名为pre-exposure；也不能因有限样本中未识别就证明整个引擎不使用。

主会话只读检查了当前磁盘数据：原EXE仍匹配已核验SHA1`fdcaf9f40ae0c18b683270601769b0bee9c49602`；UTF-8/UTF-16均有UE4 4.18分支标记，常见曝光/AA CVar名称可匹配，而`r.UsePreExposure`/`r.PreExposureOverride`两编码均未匹配。Engine.ini没有相关设置，GameUserSettings的AntiAliasing=2等是**当前持久配置，不能直接映射为捕获时TAA枚举**。现有UE4SS日志有EngineVersion探测失败记录，但没有pre-exposure观测，不据此诊断当前进程或要求修UE4SS。

这些信息支持“旧引擎没有新版命名设施”的假设，不足以排除自定义颜色缩放。一次有界公开检索未取得对应版本的权威证据；Epic 4.19官方发布页HTTP403，没有绕过访问或改配置，未以网络摘要/版本号补齐运行值。

此外，单帧HDR值C和最终曝光增益G本身不能唯一确定隐藏因子P：`C=P×L，G=E/P`的多种分解都能得到同一个`C×G`。核查脚本用独立的数学反例验证这一点，**反例中的1/4等不是填给AC7的参数**。因此简单再抓一张相同画面、或“曝光接近1”，不会自动解决该未知项。

### 最小补证据与停止边界

继续SR图像接入需要以下任一项对**当前渲染路径**成立：

1. 与游戏实际编译路径匹配的view/producer曝光布局及绑定来源，能定位额外预乘因子或证明该设施未用；
2. 现成、可信的引擎运行观测，明确记录pre-exposure的实际启用状态/因子与对应HDR输入。新版字段名不一定存在，不能先强改UE4SS版本或写引擎补丁来“读出”它。

仅普通相邻帧颜色/截图并不必然辨识pre-exposure；时间条件另需真正相邻的GPU资源/矩阵和jitter数据。若现有工具无法给出上述语义证据，就按用户“不深入修改工具链”的限制**暂缓原生标定/接入验收**，保留已验证NR离线链。显式独立域下的Reset单帧诊断可以单独推进，但假设不能转为游戏事实，不能靠默认值、自动曝光或重复创建越过生产验收门槛。

证据：[捕获实际CB及尺寸核查](../artifacts/research/official-sr/ac7-input-gates.json)、[有界shader摘要](../artifacts/research/official-sr/ac7-pre-exposure-shader-evidence.md)、[当前磁盘只读观测](../artifacts/research/official-sr/ac7-readonly-metadata.json)。新工具[audit-sr-input-gates.cjs](../tools/nsight/audit-sr-input-gates.cjs)检查实际CB绑定、重读row114、验证viewport类别及数学反例；它不调用GPU/NGX，也不修改捕获。

## 本机运行阶段

新增既有探针的三个明确模式，没有另建运行框架：

```text
preflight-nr.exe d3d11-sr <SR目录> <独立日志目录> <本项目GUID>
preflight-nr.exe d3d12-sr <SR目录> <独立日志目录> <本项目GUID>
preflight-nr.exe d3d12-sr-create <SR目录> <独立日志目录> <本项目GUID>
```

| 阶段 | D3D11 SR能力 | D3D12 SR能力 | D3D12 HDR/DLAA创建 |
| --- | --- | --- | --- |
| 硬件设备、NGX Init、GetCapabilityParameters | 成功 | 成功 | 成功 |
| 实际加载路径 | 均为显式项目私有正式SR文件 | 同左 | 同左 |
| SuperSampling.Available | 1 | 1 | 1 |
| FeatureInitResult / NeedsUpdatedDriver | Success / 0 | Success / 0 | Success / 0 |
| 返回的MinDriver | 470.0 | 470.0 | 470.0 |
| CreateFeature | 未调用 | 未调用 | **Success，有效handle** |
| 提交/GPU fence | 不适用 | 不适用 | **completed=1** |
| Release/DestroyParameters/Shutdown | 参数销毁/关闭成功 | 同左 | 全成功 |
| Evaluate、图像及高输出 | 未做 | 未做 | 未做 |

MinDriver是这些query返回值，不扩展为所有模型/配置的产品最低要求。创建探针没有启用图像求值模式的D3D12 debug layer，不能借用此前NR的debug0作为SR debug验收。

创建配置为2560×1440→2560×1440，DLAA、node masks1、flags=11（HDR|MVLowRes|DepthInverted）、不启用output subrect。字段映射按官方SR helper逐项设置；反向Z等只是创建测试配置，不说明已经传入/验证游戏资源。没有提供猜测曝光/pre-exposure，没有运行Scratch或Evaluate；成功不代表HDR输出已验证或NR→SR已串通。

[三个原始运行记录](../artifacts/research/official-sr/d3d11-sr.json)、[D3D12能力](../artifacts/research/official-sr/d3d12-sr.json)、[HDR/DLAA创建](../artifacts/research/official-sr/d3d12-sr-create.json)、[断言与委派记录](../artifacts/research/official-sr/sr-preflight-checks.json)。无参数/非法GUID的SR模式测试在Init前拒绝，避免落入NR模式。原NR测试模式保留。

官方NGX初始化本身仍可能维护ProgramData模型缓存、启动官方更新器并记录telemetry；未手工改系统、OTA或游戏设置，沿用既有副作用边界。

## 捕获位置：只有候选，尚未锁定

对固定正常捕获的索引与已反汇编tonemap shader检查：

- Scene210最后观测到的Draw写入是467；它是已做NR离线回写的检查点。
- Tonemap Draw525的PS t1绑定Scene210，shader实际声明并采样t1；不是只靠残留绑定猜消费关系。
- 后续Draw526是已识别的空间AA，Draw528是AA后效果，HUD合成另在545。

[位置证据摘要](../artifacts/research/official-sr/capture-sr-boundary.json)。索引不是完整copy/compute/hazard解释器；该顺序支持HDR-before-tonemap候选，但不能证明所有前置效果适合SR，或低输入/高输出所需采样/viewport修改已具备。不能只把SR放进Draw467再让旧空间AA继续执行就宣称完成替换。

## 保守委派与主会话验收

按用户授权，首次使用**一个只读scout，magpie/auto/gpt-6-luna**核对本地头文件；同一child只续查一次新增官方指南，交付结果摘要，不改项目、不跑GPU、不发子任务。主会话负责取得/核验正式文件、运行探针、对照捕获及形成决策。

child首次只看headers时指出的MV/HDR语义缺口，随后由正式PDF补齐；不能把早期“头文件未写”当作SDK永远无合同。补充摘要中把MVJittered措辞写成方向旗标，主会话未采用该措辞，已按公开枚举和指南明确它仅是jitter包含标志。

[保留的子任务摘要](../artifacts/research/official-sr/subagent-guide-audit.md)及run IDs见断言记录。子任务原始outputReference为工具托管artifact，主会话另保存到研究目录，避免托管保留期到期失去证据。没有把子任务attestation当作GPU成功。

## 下一步与停止条件

当前SR正式前置阶段已通过，后续显式Reset图像诊断也已通过API/数值检查，见页首。真实游戏pre-exposure及输入标定仍未成立，不把诊断的1当作测量；单帧输出高光/局部变化也未验收。

接下来解释SR重建/高光变化，继续寻找当前路径的view/曝光语义来源，再决定原生回接和多帧采样。低输入高输出、时域/FG需独立验证，不盲目重抓普通单帧，不提前选桥或宿主。

SR本身不强制D3D12；本项目NR已用D3D12，后续是否统一处理要由最小复用/同步证据决定。用户“不深入修改工具链”限制保持：不改模型/SDK/core、不重写UE4或整套调色链。原始捕获/游戏保持原状，未Git提交。
