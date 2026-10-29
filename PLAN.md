# AC7 原生渲染适配推进计划

**一句话状态**：提交仍`81021a0`。[第四轮实际观察通过](docs/AC7-Live-View-Observation-Validation.md)：原生全宽View/guide绑定关联、8合格真实counter62/63/64/66/68/69/70/71，observed8/error0/applied0/bounded-stop/drained1。缓存16条、淘汰1024次，轮换修复实机有效。65明确时钟/计数拒绝，67未接受且精确原因未重报，不能称连续8帧或模型验收。当前观察版固定通过基线；下一阶段才是独立验证全宽中性复制及原warp同帧消费，无自动启用/部署/ZIP/新提交。

本文只保留目标、当前状态、接下来的阶段和固定约束。逐次验证细节、失败记录与旧版完整流水账见 [修改前完整归档](history/planning/PLAN-before-readability-cleanup.md) 及第 6 节的证据入口。

**用户最新调整**：当前直接用本地构建ASI和日志验证，不再创建ZIP/发行包；历史打包记录仅作证据，ZIP当前已不在工作区，不重建。ASI不依赖ReShade，旧采样诊断可备用，没有具体冲突不主动清理。下文已有包路径只是历史产物，不是操作前提。

## 1. 目标与处理链

目标：在普通屏幕下，为 AC7 实现原生 NR、SR 与 FG 适配，使用游戏真实数据，不以参考项目的功能列表选定方案。

已接受的候选处理链：

场景 HDR → SDR 代理 → NR → 恢复 HDR → SR(HDR) → 原 warp / 后处理 / tonemap → FG 最终颜色 / 引导 / UI 与呈现

- 资源是否为 HDR 按插入位置的语义判断，不按显示器开关判断。
- 完整接入方式未选定：仅比较薄AC7适配＋OptiScaler与自研接入＋直接NGX；现有ReShade只作为已验证的诊断采样工具，暂停专用回接。参考仓库未经实际验证，不计为已工作依赖。
- 只做 NR/SR/FG 所需的窄适配。不逆向或修改模型、NGX core，不试探私有 ABI，不重写 UE4 渲染器或整套调色链。
- 是否使用外部控制程序与 GPU 处理是否跨进程分别决定，不预设独立进程。

## 2. 当前状态

**已完成**

- 项目独立化与文件整理（阶段 0）。
- 运行 API 确认为 D3D11；采样工具已取得真实动态两帧与八帧样本。
- 独立离线工具 `gpu-chain` 跑通整链：真实动态八帧（698–705）经 GPU 代理 → NR → GPU 恢复 HDR → SR，56 个阶段的 raw 与原文件基线逐位一致；NR/SR 的 feature 持久、Reset 1→0、debug 为 0、清理正常。见 [GPU 数据链](docs/AC7-GPU-Chain-Validation.md)。
- 同一工具完成 post-submit 等待预算验证：真实 NR 与 SR 各注入一次超时（预算 2 ms），迟到结果被丢弃，busy 帧保留原图，两特性 Reset 后恢复并继续历史，六份输出与独立参考逐位一致。见 [等待预算与退休](docs/AC7-Deadline-Retirement-Validation.md)。
- 提取无日志/读回/等待循环的 `FrameSubmission`，支持绝对截止时间。相同四张源纹理复制后覆盖、连续复用，16 份 D3D12 输入仍逐位一致；在途过期、完成后才首次轮询和提交前已过期均按规则处理。整链与 Reset 回归通过。见 [帧提交与源覆盖](docs/AC7-Frame-Submission-Validation.md)。
- `ChainSchedule` 已统一输入准备、NR、HDR 恢复与 SR 的绝对截止时间和双 Reset；输入前过期、NR后/SR前过期、SR在途过期和忙帧回退通过，六份恢复/继续历史输出与参考逐位。预算是独立诊断边界，不是目标帧时间。见 [整链调度策略](docs/AC7-Chain-Schedule-Validation.md)。
- 固定尺寸 `NonblockingChain` 原型已按单次调用推进；NR/SR各16次受阻pending、停止拒新帧、D3D11和模型共同退休后销毁、三次驱动实例重建与双Reset通过。自有scene/exposure快照使调用后源覆盖仍能正确恢复HDR；六份输出与原始输入参考逐位。见 [单步驱动与退休](docs/AC7-Nonblocking-Chain-Validation.md)。
- 调用方报告处理错误后，驱动保留首次原因、隐藏输出、拒绝新帧，健康的NR/SR在途工作可正常退休。三处错误注入、六份恢复参考及正常56阶段回归通过；不等于真实API或设备失效验收。见 [错误停用与退休](docs/AC7-Chain-Error-Stop-Validation.md)。
- `FixedRuntime`统一持有转换资源、NR/SR与驱动，4次会话实际重建，32次受阻关闭保留资源、退休后释放；部分构造上下文不匹配能回收已创建特性，六份恢复/历史输出逐位。见 [固定会话所有权](docs/AC7-Fixed-Runtime-Ownership-Validation.md)。
- OptiScaler标准包的[首轮公开接口](docs/AC7-OptiScaler-Interface-Validation.md)已核验，后续自有共享桥＋公开D3D12入口在FL11.0上完成持久八帧历史/逐帧Reset：16输出与直接NGX逐位、64输入保持、双debug0和完成后清理正常。累计NR0/SR21；其D3D11包装层FL11.0/Shutdown1限制仍保留。见[八帧验证](docs/AC7-OptiScaler-Sequence-Validation.md)。

- D3D12颜色模块复用同一HLSL，真实八帧中性/非中性48阶段分别对本轮D3D11与旧基线逐位；32输入保持、6参数/3配对拒绝、3非法模型像素保原图，GPU验证及双debug0。prepare/restore可分段接模型；本轮NR0/SR0，非中性用旧NR样本。代理实际RGBA16F，已更正上轮R8误写。见[颜色验证](docs/AC7-D3D12-Color-Validation.md)。

- OptiScaler公开独立feature18 NR首帧逐位通过，随后自有D3D12颜色→实际NR→HDR恢复→实际SR完成历史8帧与SR逐帧Reset8帧，80阶段逐位、64输入保持、两特性/资源持久、完成后清理与双debug0；内建NR pipeline关闭，无源码补丁。本轮NR17/SR16、Opti累计NR17/SR37。见[公开整链验证](docs/AC7-OptiScaler-GPU-Chain-Validation.md)。

- 原始D3D11 R11输入/规范引导与私有输出已接单共享桥，最终正常8帧40阶段＋8返回逐位；覆写四源后快照保持、整链过期/晚首poll/忙帧/stop拒新与双Reset恢复、D11复制fence退休通过，旧FP16 transport实际回归。阶段NR30/SR30、Opti累计47/67；仍独立fixture初始化、非游戏回接。见[共享整链与发布退休](docs/AC7-OptiScaler-Shared-Chain-Validation.md)。

- 同通路直接NGX的40阶段＋8返回逐位；4进程ABBA两路线各64有效计时样本/debug0/末帧精确，CPU差约0.070ms、GPU差小且波动，不是FPS验收。Opti失败40＋31次保留/排除，旧菜单30帧后COPY_SOURCE泄漏/全关sink不静默已定位，未改参考源码；配置勘误见[路线对照](docs/AC7-Route-Comparison-Validation.md)。本阶段Opti151/151累计198/218，直接88/88。

- 默认关闭无模型中性入口的独立合成D11 FL11.0验证通过：准确prewarp字节码注册/viewport与场景depth-View绑定形状、全scene和中央crop逐位、六stage/IA/RS/OM快照、CS隐藏counter及原CS继续、6绑定/时序拒绝与gate8pending/stop-drain/debug0。无hook/真实原warp draw/模型；见[中性宿主](docs/AC7-Neutral-Host-Validation.md)。

- ASI真实LoadLibrary/InitializeASI、debug设备双公开入口CreatePS/DrawIndexed hook已通过：原draw8次、中性2次、6份绘制输出与scene逐位、状态/pending8/停止debug0；shader替换停用控制通过。当时普通设备未通过（已由下条deferred路径修复），两个vtable实验废弃，普通核心和活动query单测通过；模型0。见[ASI验证与准确阻塞](docs/AC7-Neutral-ASI-Validation.md)。

- 普通设备ASI复制路径修复：分步透明/getter/swap通过、直接copy（无fence）触发device hung；换私有deferred context录制＋ExecuteCommandList(TRUE)，普通/debug完整hook矩阵及中性核心/替换回归全部通过。默认已用新路径，临时诊断开关移出正常构建，保留失败/source证据。见[修复与资格边界](docs/AC7-Neutral-ASI-Ordinary-Fix.md)。

- View身份协议与默认关闭包：完整4096字节View由Map/Unmap、Update/Update1观察，counter137.y扩64位，双时钟/rect/零jitter/CB range验证；精确guide shader/depth/MV/View/viewport及完整字节匹配。GPU copy/未知CL作废缓存。原八帧普通/debug各中性8、默认off观测8复制0，12拒绝全原draw正确；在途699跳过且不晚补、700恢复通过。已安装UAL副本独立实际自动Initialize通过，实际AC7启动待用户只观察日志。包/独立核验/负对照见[View ASI报告](docs/AC7-View-Identity-ASI-Validation.md)。

- 首轮实际游戏日志确认Initialize/installed与双shader捕获，未确认View。用户启用宽屏补丁，旧固定中央协议不足；新中央/全宽只观察、error13拒绝全宽复制、一次性context/CB/上传/拒绝诊断已通过独立测试。原日志/旧ASI完整保留，不新打ZIP、不自动移除补丁或ReShade。见[实际入口诊断](docs/AC7-Game-Entry-Diagnostics.md)。

**尚未完成**

- 任何游戏内接入：未写游戏 scene，未接原 warp / 后处理 / UI，未启用 NR、SR 或 FG。
- 生产环境整帧预算与实时性能：独立工具已共用绝对截止时间，但仍有 CPU 轮询与测试开销；未包含游戏输入捕获、scene 写回和呈现，不代表 FPS、延迟或显存验收。
- 资源生命周期：已有固定尺寸驱动stop/drain和重建验证；真正的尺寸/模型重建、NGX或设备会话重建、设备丢失与游戏安全卸载尚未实现。
- 路线1/2均需完成游戏host/proxy身份核验与scene / warp / post回接；当前没有任何已选定的游戏模型宿主。
- 质量验收：NR 暗部局部增益、SR 细高光、动态稳定性与遮挡尚未验收。

**不能据此宣称**：实时可用、已接入游戏、整帧性能合格、画质通过、P 已校准、jitter 已验证。

## 3. 已确认事实与当前假设

- 离线验证使用 NR 与 SR 的 310.9.16.0 正式 runtime。本机 NR 的 D3D11 特性不可用，因此离线链经 D3D11 共享纹理交给 D3D12 NGX 执行，再回到 D3D11。
- 当前模型候选边界是原颜色 warp 之前（465 后 / 466 前）。depth32 与 MV 另行判断，原 warp 保留在模型之后。旧的 466/467 后模型结果只作历史证据，不能套用于新输入。
- 曝光 P 为 **P=1，未校准**。已有诊断未见明显整体异常，预曝光调查延后且不阻塞；若出现明显亮度跳变、闪烁或历史异常，再重开排查。
- 实际 jitter 为 0，不伪造非零 jitter。
- 游戏画质菜单只有 FXAA，没有 TAA。捕获中的辅助历史 / 速度滤波不能称为全场景 TAA。
- 当前动态样本的纹理分配为 5120×1440，有效场景是中央 2560×1440；尚不代表最终全幅超宽输入。

## 4. 接下来的阶段

按以下顺序推进；已有资源定位、动态采样和模型执行证据直接复用，不重新要求用户采样。独立调度验证先行，宿主选型在游戏接入前完成。

### 阶段 1：OptiScaler接口验证与两路线比较（当前下一步）

工作：

- 自有共享桥＋OptiScaler公开D3D12 SR的持久八帧/Reset和完成后清理已通过。仍仅使用这一套桥；OptiScaler自身D3D11层的FL11.0/Shutdown1限制不称已修复，在途停止/迟到丢弃还未移植验证。
- 同通路对照已完成。工程建议下一固定尺寸NR/SR原型优先直接NGX，复用现有颜色/桥/发布控制；路线1继续保留作FG/压缩候选，完整游戏宿主/FG架构未选，路线3暂缓。具体数字/菜单与日志配置勘误见[对照报告](docs/AC7-Route-Comparison-Validation.md)。
- 默认关闭观察已实际通过，不再重复同一轮。下一阶段补全宽5120中性复制路径，先独立验证scene/状态/退休/原Draw继续，再准备明确的用户8帧中性试验；当前全宽复制仍禁止。
- 实际合格帧中有65/67间隙，65明确frame-clock-counter拒绝，67精确原因未重报。模型历史前核时钟断链和Reset，不放宽或补假帧；当前像素/guide内容/模型/FG/性能未验。见[实机观察边界](docs/AC7-Live-View-Observation-Validation.md)。
- 本项目代理为FP16，上轮R8是文档误写。OptiScaler在此次FP16输入下也用FP16；默认SoftKnee/sRGB及compose/MaxRatio2仍与已有ACES/2.2差分政策不同。实际NR须用原始prewarp HDR，不能复用已NR的SR输入或暗中套默认限幅；P校准继续延后。
- 与本项目直接NGX的合格输入对照，比较所需窄改动、复制/同步点和生命周期；研究NR→HDR恢复→SR留在同一D3D12链的可能，避免叠加互操作桥。
- 路线1是薄AC7适配＋OptiScaler，路线2是自研接入＋直接NGX先做NR/SR、FG另选。路线3完整自研FG暂缓；ReShade专用中性原型暂停，诊断采样成果保留。

完成标准：取得可复核的接口执行/输出证据，或列出准确的接口/构建阻塞；比较两路线工作量、同步成本与FG复用边界后再选型。当前未有框架性能对比，不承诺哪条更快。

### 阶段 2：选定路线的最小游戏回接

工作：

- 依据阶段1结果确定宿主，复用已取得的真实scene/depth/MV/View与shader签名，不重新要求用户采样。
- 固定当前尺寸，先实现默认关闭的中性回接、全状态恢复、区域外保持、错误保原图与安全停止；独立合成D3D11宿主验证后再准备用户操作的游戏包。
- 不用捕获draw编号作实时hook；模型阶段必须解决同帧曝光取得和原warp消费前完成时序，不写回迟到结果。动态resize不作为首次固定尺寸中性测试前置。

完成标准：中性旁路、状态、区域、错误和关闭控制通过；准备可审阅包与操作说明。未经用户部署/实际游戏验证，不宣称已接入。

### 阶段 3：游戏内同尺寸 NR → HDR → SR 回接

工作：

- 先做中性与失败回退控制，再逐项接入已验证的 NR、HDR 恢复及同尺寸 SR 路径。
- 将结果写回原场景，让原 warp、后处理、tonemap 与 UI 按原顺序继续。
- 检查连续帧身份、真实 jitter、历史、双 Reset 和超时回退；默认保留 FXAA，并与仅旁路其 RGB 的候选对照，保留 alpha、黑边和 UI。

完成标准：游戏内中性回接与原图一致；模型开关、输入错配、超时与历史中断均可复现，失败不写入错误结果。画质依据具体可见异常和连续帧对照评估，不以非中性差异本身判缺陷。

### 阶段 4：真正超分与超宽周边压缩

工作：

- 在同尺寸接入稳定后验证不同输入 / 输出尺寸的 SR，处理颜色、引导数据与真实渲染 jitter 的对应关系。
- 验证超宽屏中心保护、横向周边压缩、平滑过渡与正确解包，保留中心与边缘对照。
- 检查细高光、遮挡、透明物与历史稳定性；出现明确曝光异常时再重开 P 的调查。

完成标准：尺寸、坐标与历史对应正确；HDR 结果仍在原 warp / 后处理 / tonemap 之前回接，UI 与黑边保持；超分和周边压缩各有连续帧质量与开销记录。

### 阶段 5：FG 与整链验收

工作：

- FG目标保留，优先复用合适后端；完整自研FG/交换链基础设施（路线3）暂不考虑。
- 测量整链真实帧率、输出帧率、GPU 帧时间、显存与延迟。
- 检查飞机、导弹、云层、遮挡、海面高光与 HUD，并验证失败回退。

完成标准：整链性能与动态画质均有实测记录；未通过项列出具体原因与回退方式。

## 5. 固定约束与暂缓事项

- **游戏与配置**：当前不运行游戏、不改游戏或 NVIDIA 配置、不部署、不替换插件，也不要求用户补采，除非另行决定。
- **AC7 派生重放**：未隔离配置前禁止运行任何 AC7 派生离线重放，即使 NVIDIA 自动优化现已关闭也不能解除。不得只靠改 exe 名宣称隔离成功，不自动修改 NVIDIA 全局偏好，也不锁定用户 INI。依据见 [事件报告](docs/AC7-Eight-Frame-Display-Reset-Incident.md)。
- **测试专用代码**：standalone ExitProcess 保护、CPU 轮询、fixture 读回与文件比较只用于测试，不得进入游戏代码。
- **Nsight / Next Frame**：多帧捕获卡顿的原因未定位，暂停盲目重试；不作为当前路径。
- **画质评价**：NR 的重建、补光与材质改变属预期，非中性输出不要求与原图一致。暗部倍率与改动像素数只作定位线索，不单凭数值判缺陷、限幅、削弱 NR 或阻塞推进。画质缺陷须具体到可见异常或连续帧对照，并参考用户观感。依据 [评估决策](docs/AC7-Image-Quality-Evaluation.md)。
- **曝光与 jitter**：P=1 未校准但不阻塞，预曝光调查延后。jitter 实际记为 0；需要非零值时，必须由游戏实际渲染应用。
- **抗锯齿**：FXAA 是游戏内唯一选项；不把辅助滤波称为 TAA。
- **工程边界**：只做窄接口适配。若路线需要深入修改工具链或侵入引擎，超出范围即停止该路线并重新评估。
- **提交**：已按授权创建本地提交`81021a0`，未push；提交后的OptiScaler验证代码/报告尚未提交，不自动再提交。
- **时间**：本计划不含时间估计；文档整理不等于已取得运行采样。

## 6. 证据入口

下列为各主题的代表性报告，完整历史见归档。

- 当前决策：[路线1/2比较与验证顺序](docs/AC7-Integration-Route-Decision.md)；最新结果：[OptiScaler接口验证](docs/AC7-OptiScaler-Interface-Validation.md)。
- 当前最新：[固定会话所有权](docs/AC7-Fixed-Runtime-Ownership-Validation.md)、[最小游戏测试路径](docs/AC7-Minimal-Game-Test-Path.md)、[错误停用与退休](docs/AC7-Chain-Error-Stop-Validation.md)、[单步驱动与退休](docs/AC7-Nonblocking-Chain-Validation.md)、[整链调度策略](docs/AC7-Chain-Schedule-Validation.md)、[帧提交与源覆盖](docs/AC7-Frame-Submission-Validation.md)、[GPU 数据链](docs/AC7-GPU-Chain-Validation.md)、[等待预算与退休](docs/AC7-Deadline-Retirement-Validation.md)
- 共享桥与模型：[共享传输](docs/AC7-Shared-Bridge-Validation.md)、[共享模型](docs/AC7-Shared-Model-Validation.md)
- 实机动态样本：[动态八帧](docs/AC7-Dynamic-Eight-Frame-Validation.md)、[实机样本离线 NR/SR 八帧](docs/AC7-Live-Eight-NR-SR-Validation.md)、[动态两帧](docs/AC7-Dynamic-Live-Pair-Validation.md)、[首次两帧](docs/AC7-First-Live-Pair-Validation.md)
- 历史与时域：[SR 历史](docs/AC7-SR-Live-Pair-History-Validation.md)、[两帧 NR/SR](docs/AC7-Live-Pair-NR-SR-Validation.md)、[连续帧方案](docs/AC7-Temporal-Capture-Plan.md)
- 采样工具：[有界采样](docs/AC7-Bounded-Sampler-Validation.md)、[八帧采样](docs/AC7-Eight-Frame-Sampler-Validation.md)、[八帧显示 Reset 事件](docs/AC7-Eight-Frame-Display-Reset-Incident.md)
- 离线模型链：[prewarp 边界](docs/AC7-Prewarp-Boundary-Validation.md)、[prewarp NR/SR](docs/AC7-Prewarp-NR-SR-Validation.md)、[SR 回接与 FXAA](docs/AC7-SR-Writeback-Validation.md)、[NR GPU 回接](docs/AC7-NR-GPU-Writeback-Validation.md)、[HDR 往返](docs/AC7-HDR-NR-Roundtrip.md)、[SR 诊断](docs/AC7-SR-Reset-Diagnostic.md)、[SR 空间响应](docs/AC7-SR-Spatial-Response.md)
- 游戏事实与曝光：[View 布局](docs/AC7-View-Exposure-Layout.md)、[颜色与曝光门](docs/AC7-Color-Exposure-Backend-Gates.md)、[图形 API](docs/AC7-Graphics-API-Support.md)
- 质量：[图像质量评估](docs/AC7-Image-Quality-Evaluation.md)
- 调查历史：[Nsight 30% 挂起](docs/AC7-Nsight-30-Percent-Investigation.md)、[history/nsight-startup](history/nsight-startup)、[history/renderdoc](history/renderdoc)
- 项目约定：[README](README.md)、[AGENTS](AGENTS.md)

完整旧版计划：[history/planning/PLAN-before-readability-cleanup.md](history/planning/PLAN-before-readability-cleanup.md)（修改前字节保全）。
