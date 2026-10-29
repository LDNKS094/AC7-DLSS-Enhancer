# 真实连续帧与jitter：最小采样方案

## 当前进度与本轮范围

**现已完成同进程GPU NR→HDR→SR图像数据链验证。** 真实8帧56阶段raw与原文件链逐位，NR/SR各持久特性/共享纹理、实际时间/Reset/debug0通过；中间不经CPU/文件反馈，但诊断读回/等待仍存在。详见[GPU整链](AC7-GPU-Chain-Validation.md)。不需新采样；下一步实时deadline/drop/reset与回退，再host/proxy/原scene-post接回，当前仍无游戏内模型/性能验收。下方“后续做8帧/共享整链”为历史步骤。

**已取得首份真实动态8帧，不需用户再补采。** `AC7-eight-frame-58000-130125640`共40资源完整；698→705、7段game/real链残差0与6×7矩阵链逐位，scene/depth/MV均实际变化/区外0。allocation恢复5120×1440，P1暂用、实际jitter0；帧间16–90ms/约0.286s，写盘扰动保持，不作性能结论。用户关闭NVIDIA自动优化，已只读确认EnableAutomaticApplyOPS=0；本轮无AC7重放。详见[动态八帧输入资格](AC7-Dynamic-Eight-Frame-Validation.md)，后续复用独立GPU/NGX工具做固定8帧NR→HDR→SR历史与Reset对照。

**固定8帧工具已验证并准备实际补采样。** v4仅2在途槽、256MiB逻辑内存/1GiB磁盘上限，缺帧/忙/写失败终止，不无限录制或跳帧；40资源硬件/重放精确、原15raw不变，重放重复995正确标非相邻。现在按[八帧说明](AC7-Eight-Frame-Sampler-Validation.md)让用户正常飞行延迟5秒请求，一次提供AC7-eight-frame目录；不是在游戏内开启NR/SR。包未自动部署，模型求值/实时桥状态保持。

**实际NR→HDR→SR两帧链完成：** 新GPU代理/guide、NR1→0、GPU恢复HDR、持久SR1→0及Reset对照已通过，仍离线文件/staging。见[链路验证](AC7-Live-Pair-NR-SR-Validation.md)。下一次需要补8–16真实连续帧，先实现并验证有限内存/帧数的短序列采样器，不能把多个两帧包拼接或直接扩16份全量GPU staging。当前无需用户新操作，实时资源桥/游戏回接未就绪，尚不能游戏内NR/SR测试。

**真实两帧SR持久执行完成：** 同feature/texture按Reset1→0，在两帧原始prewarp HDR和新guides上通过Evaluate/GPU/输入逐位/debug0；1→1对照首帧逐位、次帧历史设置相关变化。隔离SR尚未叠加NR，P1/jitter0继续；详见[SR历史执行](AC7-SR-Live-Pair-History-Validation.md)。下一步复用此路径接真实连续NR输出，当前不再请求重采，两帧不验收长时质量。

**动态两帧已取得并通过基本输入资格。** 用户70932目录1123→1124、game/real约16.98ms同步推进、两链残差0、六矩阵链对应，相机/颜色/depth真实变化；objectMV恒等但depth/View回退运动非零。详见[动态两帧](AC7-Dynamic-Live-Pair-Validation.md)。现在优先利用已存数据做规范化/持久feature首Reset次history执行准备，不再请求重复两帧。jitter0与模型时域/8–16帧质量门槛继续独立，不能以采集成功代替质量验收。

**已取得首次真实两帧：** v3用户77324输出完成，计数393→394、real时间链和六组矩阵对应，支持相邻渲染区间；游戏时间/相机/depth/MV保持，颜色有实际变化，动态资格未通过。real链通过而game链false与旧UE4派生PrevGameTime在暂停状态的含义相容；原manifest保持。索引7→0独立于帧号mod8，旧单帧相等不能作为规则。见[首次真实两帧验证](AC7-First-Live-Pair-Validation.md)。下一次无需换DLL，只更换延迟5秒触发命令，启动后立即回游戏，先取模拟正常推进的两帧再扩短序列。不继续重抓Nsight、不重复当前帧冒充动态。

**最新：第二次实机为consumer重复，v3已备好。** 启动约4秒便arm/失败，早于现场新请求约84秒，支持旧marker启动误触发。v3按process创建时间归档旧请求、最多16个guide组合按实际prewarp depth/View/viewport唯一配对（相同绑定去重、速度歧义拒绝），并留最多64候选事件trace。硬件配对/门禁与重放原输出不变通过；当前不代表真实飞行或尺寸问题已经验收。退出后换v3 addon、移走旧request、进入飞行后单次新请求；游戏文件本轮未改。详见[最新记录](AC7-Bounded-Sampler-Validation.md)。

**首次真实采样到达scene检查后被拒绝，v2待实机复核。** 用户目录118836-122153859报`Unsupported scene dimensions or View rect`，无帧资源；原日志保全。已移除v1从离线样本借用的5120×1440分配上限，改用不增加的两帧256MiB字节预算，并在拒绝前记录实际allocation/rect/三资源描述。硬件padding接受/预算拒绝与重放原输出一致通过；不能据此断言本次实机原因已解决。v2诊断包已准备，未自动更改游戏内v1；退出后只替换addon、保持设置再采一次。详见[最新实机与修正记录](AC7-Bounded-Sampler-Validation.md)。

**后续推进：已实现固定2帧的窄D3D11资源采样核心和可选ReShade公开API20诊断插件。** RTX5080合成两帧/10资源有效分量及拒绝检查通过；独立AC7捕获重放中，三个实际shader签名匹配、两组10资源与原数据精确对应、两帧后停止，原15检查点逐位不变。重放计数995/995正确标记非相邻，**仍没有新真实序列**。独立诊断包已在项目artifacts准备，未部署/启动游戏，详见[有界采样验证与失败修复](AC7-Bounded-Sampler-Validation.md)。后续不再重复搜索未知Nsight帧数参数；现有公开接口审计没有找到D3D11受限控制，下一步准备独立宿主真实两帧采样，保持不改渲染/不NGX/不深改工具链。

**最新实测反馈：用户确认界面存在Next Frame，但点击会导致crash。撤回“导出A→点击一次Next Frame→导出B”的操作要求，暂停此路线，不要求重试。** 崩溃主体（游戏、Nsight或两者）、异常码与具体时刻尚未定位，不能把历史UE4SS异常记录套给本次。用户同时确认此前F11曾采入非常多帧并导致卡死，帧数不可控；这与已有Frame3062–4424/6,753,807事件证据一致，说明实际存在跨多个帧边界的数据采集行为。**当前缺的是可控停止/帧数上限与可导出资源，不应再把问题描述成“是否有多帧能力”。** 可控入口尚未找到，异常长捕获也尚未验证成完整可用的连续资源序列。

当前无需用户新抓帧或截图。先复查已有长捕获记录与公开D3D11控制入口；在确认有明确帧数上限/停止机制前，不再用F11或Next Frame试采。目标是一次有界短序列（初次2帧资格，再8–16帧），无需完整API调用录制；若Nsight公开接口不能提供可靠边界，再评估只读导出所需颜色/depth/MV/View/曝光的窄采样器，仍遵守不深改工具链的限制。

扭曲前的单Reset NR→恢复HDR→SR→原扭曲/后处理/UI已通过配对回接。本轮在提交`4a75c2d`之后继续，只读核对采集能力、NGX合同与已有View字节；**没有启动游戏/Nsight、没有采到新序列、没有运行模型**。

质量评估遵循[当前决策](AC7-Image-Quality-Evaluation.md)：NR预期改变光照/材质，暗部增益、变化像素及SR平滑都不是单独的缺陷证明或阻塞项。继续连续帧是为了检查实际运动/历史行为，不是追求NR与原图恒等。P=1暂用、预曝光调查仍延后。

## 必须区分的两件事

| 目的 | 当前可做的起点 | 不能据此宣称 |
| --- | --- | --- |
| 真实相邻帧、MV/帧对应与历史诊断 | 先取同一游戏会话的两个真实渲染帧，保留实际jitter（即使为0）；核对帧身份、相机/时间、同视图颜色/depth/MV | 零jitter序列不能完整验收DLSS子像素重建/超分质量 |
| 完整SR时域重建质量 | 需要真实渲染时应用的jitter及其准确参数，连续输入与正确reset | 只改NGX jitter数字、对已渲染图片移位、重复同帧都不产生真实新样本 |

因此**不把“暂无jitter注入”变成所有相邻帧诊断的停止条件**。先校验真实帧的输入链，完整SR质量条件单独记录。

## 官方采集工具的API边界

已取得并保存Nsight Graphics **2025.5归档文档**，没有用当前版本的功能列表外推旧版。页面HTML及SHA256在[frame-docs-fetch.json](../artifacts/research/temporal-preflight/frame-docs-fetch.json)。

- [Graphics Capture概览](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/graphics-capture-overview.html)明确：**“The Graphics Capture activity supports D3D12 and Vulkan APIs.”**
- [Graphics Capture CLI](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/graphics-capture-cli.html)提供`-n,--frame-count`；介绍写最多60帧而help摘录显示[1–600]，存在文档内部差异，不选择其中一个当AC7实证。**更重要的是它属于Graphics Capture，不是AC7的D3D11 Frame Debugger，不能照搬此参数。**
- [Frame Debugger概览](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/frame-debugger-overview.html)说明支持Nsight一般支持的API，并有C++ Capture导出；这与已有D3D11实测相符，但没有据此证明一个可限制帧数的多帧功能。
- [启动UI说明](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/ui-launch-application.html)中的Capture Mode=Manual/By Frame Index指定**何时触发**，不等于采集长度；证据在[UI文档获取记录](../artifacts/research/temporal-preflight/ui-docs-fetch.json)。

此前项目已记录AC7曾异常采到1363帧、约675万事件，Frame Debugger设置中未找到有效frame-count，根因未解决；成功单帧或probe单帧不消除这个问题。不能用未知的CLI/UI参数盲目重抓，也不能把那次巨大捕获当已验证的相邻序列导出方法。

归档`UserGuide/index.html`只是87字节meta-refresh跳转，通用可读提取没有得到正文；主会话跟随其公开目标并取具体官方页面，不修改工具配置或绕过访问控制。

## 当前单帧可用的身份候选

[audit-temporal-candidates.cjs](../tools/nsight/audit-temporal-candidates.cjs)核验实际motion consumer绑定的View blob123，定点读取112字节，结果在[capture-candidates-v2.json](../artifacts/research/temporal-preflight/capture-candidates-v2.json)。

| 候选 | 实际值/一致性 | 证据限度 |
| --- | --- | --- |
| View row114当前/前帧jitter | (0,0,0,0) | 已有绑定/字节证据，只代表此帧 |
| row137.y按uint的帧计数候选 | 995 | 旧布局局部顺序+实际数值；尚未跨帧证明字段身份 |
| row137.z模8候选 | 3，且995%8=3 | 内部相容，单帧不能证明该值随真实帧递增 |
| row133.zw前一游戏/实时时间候选 | (7.8507690,8.4549437)秒 | 对应旧字段顺序，仍需跨帧串接验证 |
| row136.zw当前时间候选 | (7.8591027,8.4632769)秒 | 同上 |
| 两组时间差 | 0.008333683 / 0.008333206秒 | 与实际曝光适应dt0.008333601秒相差<1e-6 |
| 导出源码帧集合 | Frame0Part00/01，仅Frame0 | 两个Part是同一帧分块，不是两个真实帧 |

这些是**候选帧元数据**，不会给现有raw强行贴上“已校准Frame995”。下一对真实样本应验证其递增关系、前后时间/矩阵串接，才能继续命名语义；文件名、时间戳或颜色hash不同都不单独证明相邻。

## 文档已确认的控制与两帧资格测试（采集操作已因崩溃暂停）

用户指出可以直接查文档后，主会话补查2025.5官方Frame Debugger UI、After Launch、Generate C++ Capture、全局选项和文档搜索索引。**撤回上一版“先提供工具栏截图才能推进”的要求；当前不需要用户截图。** 公开资料先由助手核对，只有以后出现明确的文档/实际行为差异，才针对差异请求辅助。

已确认操作和含义：

| 操作 | 官方文档语义 | 对连续帧的意义 |
| --- | --- | --- |
| 主工具栏Pause and Capture Frame | 暂停应用并进入Frame Debugger，捕获帧 | 可触发帧捕获，未保证与此前样本紧邻 |
| 目标窗口默认F11 | 与捕获入口对应，可在Tools→Options配置 | 快捷键不是采集长度或逐帧保证 |
| HUD左右事件按钮、Ctrl+左右键、Home/End | 在已捕获帧的事件/draw之间移动 | **不是推进真实游戏帧**；HUD与API事件导航表对方向文字存在不同表述，故不将某一方向当新帧快捷键 |
| Generate C++ Capture专用活动 | 从主工具栏开始导出，临时暂停；完成后应用恢复运行 | 便于导出，但单次完成不等于停在恰好下一帧；该行为只针对专用活动，不外推至Frame Debugger内导出的全部行为 |

来源：[Frame Debugger UI / Application HUD](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/frame-debugger-ui.html#application-hud)、[After Launch](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/after-launch.html)、[Generate C++ Capture](https://archive.docs.nvidia.com/nsight-graphics/2025.5/UserGuide/generate-cpp-activity.html)。补查页面/哈希在[控制文档获取记录](../artifacts/research/temporal-preflight/frame-controls-fetch.json)及[补充记录](../artifacts/research/temporal-preflight/frame-controls-extra-fetch.json)，全局选项页超时后重新取得200，见[重取记录](../artifacts/research/temporal-preflight/ui-global-options-retry.json)。

2025.5官方搜索索引包含80个文档条目，主会话据此定位上述章节；本次没有找到**对D3D11 Frame Debugger保证“恰好推进并捕获下一个真实帧”的明确合同/快捷键**。这是已查范围内未确认，不是声称产品不存在Next Frame功能；也不能拿事件箭头、Graphics Capture CLI或更老/新版本的功能替代。

以下是此前方案的资格核验要求，**不再作为当前操作指令；Next Frame双捕获路线已经撤回**。未来有受限采集方法后，仍需将“采样与资格核验”分开，不先保证相邻：

1. 后续实际采样时，在同一游戏会话保留两次独立真实帧的完整C++ Capture，新目录保存A/B；使用文档明确的捕获/导出路径，避免覆盖A。此步骤本轮未执行，也不在现有异常会话上盲按F11。
2. 助手为A/B独立建立资源/插入点索引，不沿用UID/Draw465。核对候选计数是否+1、前后时间/相机矩阵能否串接、同视图颜色/depth/MV是否对应，记录暂停/失焦/采集开销。
3. 只有通过核验，才把这对样本当相邻；如果跨帧或时间不对应，只作两次独立采样，**不靠文件名、hash不同或快捷键操作次数冒充连续**。
4. 若文档支持的交互采样无法取得可信相邻帧，下一步评估一个有明确帧数上限的窄范围运行时只读采样器；先检查可复用公共接口/实现范围，必要时先做旁路验证，超出“不深改工具链”范围则停止。不反复抓帧碰运气，也不预设后端/宿主。

**停止条件**：若再次出现大量帧/事件、捕获持续增长或长时间卡住，不Resume/反复F11/导出巨大多帧包；若B跨了多帧或计数/矩阵无法对应，也不送入模型当相邻。保留证据，再选择受限采样方法。

若两帧资格通过，再取8–16个真实相邻帧做历史/运动诊断，仍不一次请求大长录。两帧只是采集资格/输入对应检查，不能作为“质量已经稳定”的充分样本。

## NGX jitter与reset合同

依据已核验官方[SR指南文本](../artifacts/research/official-sr/sr-guide-extracted.txt)：

- §3.7.2，**PDF物理32页 / 印刷22页**，文本1694–1706：让相机/viewport发生小幅实际移动以改变光栅样本，示例为修改projection矩阵。
- §3.7.3，**PDF物理33页 / 印刷23页**，1716–1737：“DLSS must know how the jitter for this frame was applied”；每次Evaluate传入实际已应用的jitter，以**输入/render target像素**为单位；0表示没有实际应用jitter。若MV包含jitter，创建时对应flag需匹配。
- §3.13，**PDF物理40页 / 印刷30页**，1954–1957：重大场景变化后的首帧设置InReset非零，丢弃旧时域状态；误用会造成闪烁/锯齿等。

连续诊断应保持同一NGX会话、参数/feature与资源身份，首帧或明确切换/尺寸变化才按合同Reset，普通连续帧保留历史。现有工具每进程创建一次然后只Evaluate1帧、Reset1；未来要加的是有真实序列后的持久会话循环，不提前对重复当前帧做伪时域测试。

AC7菜单无TAA选项、现有帧jitter0，因此**完整SR时域采样可能需要窄范围实际渲染jitter适配**。目前没有实现或选定宿主，也没有证明修改单个矩阵就覆盖深度、速度、阴影/透明、UI等所有关联。拿到序列/当前路径证据后评估公共接口上最小适配，超过用户“不深改工具链/引擎”边界就停止该路线；不会只伪填NGX jitter以宣称完成。

单只读scout提取了此前失败与NGX要求，[报告已保留](../artifacts/research/temporal-preflight/scout-prior-evidence.md)，run`ccf4a4a8-b520-44bd-927a-fd38e87d2639`。其把印刷页写作PDF页，主会话已按`PDF_PAGE`标记纠正为上面的物理页码。采集/API适用性与最终方案由主会话决定。

本方案与提交后的新增脚本/文档保持未提交工作区；用户要求的“一次提交”已经完成，没有自动再提交、推送、部署或采样。
