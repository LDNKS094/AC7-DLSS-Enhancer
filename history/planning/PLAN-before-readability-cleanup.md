# 工作计划

## 当前决策

项目独立组织，研究范围是普通屏幕的 AC7 原生适配。后端和进程结构尚未选定。Luma、OptiScaler、Magpie 及其他项目均为参考或候选，不能以通用功能列表代替 AC7 的运行证据。

优先复用已有模型调用、周边压缩、超分和帧生成实现，只开发必要的游戏适配；是否使用外部控制程序，与 GPU 处理是否跨进程分别决定。

## 阶段 0：项目整理

- [x] 建立独立子目录和 Git 仓库。
- [x] 将相关需求、调查、选型记录与对照预设集中到本项目。
- [x] 保留参考仓库和历史产物的原位置，修复引用。
- [x] 明确尚未实现适配器，未选定后端，未确认实际运行 API。
- [x] 检查迁移后的文件完整性和本地链接。

本阶段只整理项目。游戏调查在本阶段完成后与用户一起进行。

## 当前执行状态

**独立整链post-submit等待预算/迟到退休/Reset已验证**：bridge abandon使同token过期也discard；Model begin/poll/finish busy前置拒绝allocator复用、四输入到fence终态才释放。真实NR超时及NR推进后SR超时各一次，后续busy帧不求值保原图，共4原图逐位、2迟到destination不变；f3/f7两特性Reset及f4历史继续，共6输出与独立参考逐位。normal8帧56stage回归/transport32in8out/debug0/cleanup通过。fault-v1与fault-v2各NR9/SR8含参考，normalNR8/SR8，总NR26/SR24。见[验证报告](docs/AC7-Deadline-Retirement-Validation.md)。预算仅upload+submit后2ms，固定测试调度仍有诊断CPU等待，非整帧实时/通用runtime；独立fixture内容不能当引擎复用保证。下一步去诊断调度入口、整帧deadline、资源内容退休及session/resize/device-loss/卸载，再host/scene/post回接；不需补采/部署，0游戏/AC7重放/配置写/提交。下方为此前阶段记录。

**完整同进程GPU模型数据链通过**：新gpu-chain独立工具重用HdrProxy/GuideNormalizer/SharedTextureBridge，新增evaluation_parameters公共政策供旧probe/新链共用；native fixtures→GPUguides/proxy→NRshared→returned11→GPUHDRrestore→R11toRGBA16F→SRshared→returned11。698–705每帧proxy/depth/motion/NR/restoreproxy/SRinput/SR共56raw逐位，Modelfeature+texturestable、actualtime/Reset1后0、debug11/12=0/cleanup；无中间CPU或文件图像反馈，但validity/stage Map/参考比较/保存/CPU等待仍有诊断开销。首typedFP16motion store截断v1 frame0 3632908components1ULP拒绝，明确exactsignificandhalf-even后v2只34 signedzero位差拒绝，仍0Eval；最终R16G16_TYPELESS+UINT UAV编码半位/GPUcopyfloat，40分量独立Pythonexpected GPU自检+8帧motion58982400components逐位。成功v3、scalar门禁runfinal、失败跟踪保护runaccepted各NR8/SR8，旧NRpair2/SRpair2+sharedSRreset8提参回归逐位，实际本轮NR26/SR34；最终验收链NR8/SR8。覆盖/缺lateDepth/最后countergap/nativeHDRNaN都Init前拒绝无输出，源hash再核。单Luna转换接口审读保留/父核。见docs/AC7-GPU-Chain-Validation.md、artifacts/research/gpu-chain-eight。独立进程退出保护不准复制到游戏；需deadline/drop-reset/资源退休/失效卸载策略，游戏host/proxy身份/最终SRcrop写原scene/原postprocess/FG均未接/未选。主会话无游戏/AC7重放启动、配置写/部署/提交，不需补采；环境复查同期真实游戏会话更新INI时间戳但1150bytes对旧保全逐位，autoOPS仍0/AC7识别22/新工具未登记AC7/未见AutoOPS apply，environment-review.json保留。下方旧“整链待接”仅历史。

**模型各自绑定共享桥已通过**：preflight新增NR/SR shared8history模式5与SRshared8reset6，原evaluate_frame参数/feature复用，模型索引{color,depth,motion,out,exposure}对bridge{0,1,2,4,3}，D3D11fixtureCopy共享输入→12模型→readback+COMMON→published回11，24个最终raw对12同次及原文件基线均逐位。NR8/3input=24与SR每支8/4input=32读回，Reset序列/actualtime/指针identity/debug11+12=0/cleanup/finite通过；output-onlyNaNseed12upload保留，模型输入12uploads0，11CPUfixture/readback只是测试。首轮后修录制/close成功才进入upload+submit保护段，明确published，否则standalone错误退出不释放pending；因同步顺序改变最终三组24复验，全仍逐位，实际本轮NR16/SR32不是只24。Luna只读scout复核/原报告保留，纠正其NaN误报（outputi3确有upload），落实published检查。前置覆盖/缺input/缺time Init前拒绝、GUIDself/旧文件SRchecker回归通过，无旧模型重跑。见docs/AC7-Shared-Model-Validation.md及artifacts/research/shared-model-eight/final-order/checks.json。NR与SR仍独立特性fixture，并非NR→恢复HDR→SR完整无文件链，下一步独立GPU整链与原基线核对；实时预算/drop-reset/资源生命周期/失效清理/gameproxy身份/host/post回接仍未选/未完成。无AC7/重放/游戏NVIDIA配置/部署/提交，不需补采。下方旧共享桥无模型状态仅历史。

**新共享传输桥独立原型通过（无模型）**：src/d3d11/shared_texture_bridge.{h,cpp}，仅5份固定模型格式纹理/同LUID/DIRECT队列/ID3D11Device5+Context4，11创建SHARED|NTHANDLE SRV/UAV→NT句柄→12Open；单sharedfence11Signalodd+Flush/12Wait+work+Signaleven，poll未完成不排11Wait/Copy，完成且token匹配才回11，晚到discard保原图。32输入/8输出raw逐位、debug11/12=0、PSslot7保留、资源identity固定；外加12gate确定pending/oldtoken丢弃/原sentinel逐位，busy/dupesubmit/token/错dest/缺曝光/错format/异LUID拒绝。最初test缺<string>构建与12创建→11Open本descE_INVALIDARG已保留，改public11origin路径hardware-v3/v4通过，无SDK/driverpatch。单Luna只读查原参考，flags未在其范围内，父会话官方API+硬件验证，报告保留。太阳原始mask质心+实际GPUbackMV独立验7段.013–.085px vs反号24–153px；inputmask外2px的history/reset>1000候选均0，旧亮足迹input<1/history>16/reset≤1候选0，不外推globalghostfree。sun/plane/sea原/NR/hist/reset细节图已查看。见docs/AC7-Shared-Bridge-Validation.md，artifacts/research/shared-bridge-v1及live-nr-sr-eight/temporal-detail-v2。新桥不含NGX，旧模型链仍文件，不宣称realtime性能/hostselection；caller需保持资源到retire、list关闭+COMMON、丢帧后Reset及异常卸载策略尚待集成。下一步真正NR/SR绑定共享资源独立8帧→回11核身份，再做host/原post/fallback；本轮0模型/游戏/重放/配置/提交，无需用户操作。

**58000真实八帧及完整链已验收执行资格：** 原始40资源复制hash保全；698→705，7timechain residual0/42matrixchains逐位/每段camera+scene/depth/MV变化区外0，allocation5120×1440。用户关闭自动OPS已确认0，无需再采，本轮0AC7/重放/Nsight/配置修改。扩既有固定2/8工具，8GPU中性逐位/guidevalid→NR同feature资源Reset1后7个0（8Eval/24读回debug0）→8pairedGPUrestore区外0→SR8history+8resetcontrol同feature纹理/每支32读回/debug0/全有限/清理；公共FrameTimeDelta按View实际17.48/15.93/16.67/32.85/88.59/23.59/90.18/18.36ms。首SR两支逐位，其余差异不评分。FP16固定.01检查被大motion量化边界触发，保留拒绝/zero自检失败并修正：边界4827867px原公式重算浮点max .0002742px，逐像素比较GPUfloat与CPUhalf bin+.001，不改motion；两halfmax .0625符合精度。原始HDR峰12288/12160，恢复逐帧相同，SR约12408→12136，不能当缺陷。单Luna审码复核/纠正logs=output误报，补View计数/时间绑定。新NRchecker/旧SR及全链回归/缺晚帧与最后输出冲突Init前拒绝/错proxy恢复前拒绝通过。见docs/AC7-Dynamic-Eight-Frame-Validation.md、AC7-Live-Eight-NR-SR-Validation.md与artifacts/research/live-nr-sr-eight。新NR8/SR16，无提交。下一步基于样本具体时域稳定性/遮挡及最窄实时bridge同步/回接/fallback；jitter0/P1暂用，约.286s非完整画质，尚无游戏内模型。下方旧等待补采状态仅历史。

**最新实机八帧失败为NVIDIA App自动优化的间接副作用，非扩大预算的理由。** 75616-128163625 metadata10240×2880/active5120×2880，两槽943726608bytes/八帧磁盘3774906944bytes，分配前拒绝0raw。NVIDIA config自动OPS=1，重放v4-eight localId2013091083在01:34:07明确Automatic OPS apply succeeded；原状态无边框/100→CurrentHistory全屏/200，shared user ini不隔离，22个AC7路径被识别。父会话对此间接改写负责，修正旧“无改动”措辞，AGENTS禁止进一步未隔离AC7重放。当前退出配置DrawScale5/模式1，不代改NVIDIA全局/INI/Steam，不需换addon；下一次启动确认100再采，若仍重置先停采保留现场。见[事件报告](docs/AC7-Eight-Frame-Display-Reset-Incident.md)，证据artifacts/research/eight-frame-scale-failure。只读Luna配合排查已复核/保留，本轮0游戏/模型运行无配置修改。

**v4固定8帧采样器已准备，可通知用户补测。** 保留TwoFrameSampler默认2/API，新增固定8参数，仅2在途槽/每frame query，DONOTFLUSH/DO_NOT_WAIT写盘后释放；复用忙则失败不覆盖/跳帧。256MiB逻辑在途/1GiB磁盘（metadata预留）/空间余量/120callback10s边界；写盘同步扰动时序明确记录。请求8独立marker/cmd延迟5秒，2/8冲突拒绝/旧请求归档/每进程一次保留。硬件默认2、8个不同合成输入40有效资源、countergapfalse、缺帧/写失败INCOMPLETE无manifest、8frame磁盘预算拒绝和原所有guide/request/state测试debug0；AC7重放8份40资源精确、原15raw逐位、24events，peak117968904/total943751232bytes，写盘409.223ms；8个995正确非连续。单只读Luna清单已保留，父会话所有修改/验证。新check-live-sequence校验7时钟/矩阵链/hash，包ac7-eight-frame-diagnostic-v4备齐哈希未部署，game插件仍v3。现在需要用户退出后换addon+新八帧cmd、清理遗留marker，正常飞行延迟请求立即回游戏，提供AC7-eight-frame路径；预期900MiB。详见[八帧验证](docs/AC7-Eight-Frame-Sampler-Validation.md)。无新游戏/Nsight/模型/游戏文件改动/提交，仍不能游戏内NR/SR实时处理。

**真实NR→GPU HDR→SR两帧链通过。** 70932源由新gpu-live-pair复用HdrProxy/GuideNormalizer，GPU中性fullscene逐位、depth逐位/validity全1/2；最初validity R32_UINT误作byte读失败保留，修正后通过。GPU vs CPU half pixel最大.001953125，GPU half转换误差.0009765625。prepare_live_nr_pair核source hash/neutral/depth/mask/SDR，NR扩展d3d12-nr-pair-history同feature/texture Reset1→0两Evaluate、6输入逐位/debug0/cleanup通过；restore重算proxy逐位等actualNRinput后发布HDR，两帧HDRmax176/174、区外0。prepare_chain_sr_pair复用原格式转换，用此HDR和GPUguides送SR1→0及1→1各2Evaluate，8输入/组逐位/debug0/有限HDR；首帧对照逐位、次帧2989365像素差仅诊断。错proxy翻1byte负向在restored scene发布前拒绝，NR覆盖Init前拒绝。新专文[离线链](docs/AC7-Live-Pair-NR-SR-Validation.md)、checker/预览已保存，预览统一ACES/gamma非原LUT无动态验收。1只读Luna合同/入口核对已复核保存，父会话全部修改/GPU/决策。共2NR/4SR，0游戏/Nsight/文件更改/提交。下一补测8–16连续真实帧，先准备有界采样器不直接囤16frames超256MiB；当前无需用户操作。无实时D3D11↔12桥/后处理回接，尚不能游戏内模型测试，FG/超分/jitter门槛保留，P1延后。

**同feature真实SR两帧Reset→history已完成。** prepare_live_sr_pair逐帧读70932源/hash/资格，原prewarp HDR精确转half，depth32保字节，实际View/depth/object解码CPU参考motion float32NDC→向后pixels half，最大2.51527/2.98419px、half误差<.001；jitter实际0、P1假设。扩展现有preflight/evaluate为d3d12-sr-pair-history与reset-control，feature/5textures持久、每帧上传/屏障/提交/fence/读回，Reset1→0及1→1各2Evaluate通过，8输入读回/每组debug0/RGB每帧11059200有限保HDR，释放清理Success。首帧两组逐位，次帧1849323像素差、HDR mean.017731/max64.3125仅诊断；证明history设置相关，不验收质量。旧单帧frame1重跑与Reset对照逐位，缺输入/覆盖Init前拒绝/GUID自检通过；共5SR、0NR/游戏。新增[历史执行验证](docs/AC7-SR-Live-Pair-History-Validation.md)/checker/原始证据。下一步实际两帧GPU规范化/代理→NR→HDR恢复→复用SR持久路径；不重采当前两帧，8–16质量/真实jitter/实时另验，未提交。

**动态输入资格已通过：70932两帧1123→1124。** 传输/10资源/rect/6事件完整，game16.980648ms/real16.981125ms推进、B前=A当前精确，6矩阵链逐位而相机矩阵块变化。颜色1411575、depth-stencil1598651有效区变化、外0；objectMV raw恒等但当前depth/View相机回退3408330像素中1372198>0.1px、max2.98419px、nonfinite0、732前rect外，回退公式参考非完整真值。jitter0、曝光相同、P1继续。原dynamicTemporalQualified:false保留，指完整动态质量未验收，不否认模拟推进与采集资格。原数据/log复制保全及check-live-pair审计；新增[动态两帧验证](docs/AC7-Dynamic-Live-Pair-Validation.md)。下一步用已有样本接规范化/代理并准备persistent feature Reset→history检查，不继续要求用户重复两帧；后续8–16帧另受限扩展，不拼独立包。当前无新游戏/Nsight/GPU模型/游戏改动/提交。

**首次v3真实两帧成功，动态资格分开。** 77324目录传输2×5资源完整/235937808bytes，393→394、real time前后精确串接、6组当前/前帧矩阵精确，支持相邻渲染区间。GameTime两帧相同、相机矩阵块/depth/MV相同，颜色75109有效区像素变化、区外0；objectMV>0.1px=0，jitter0，曝光同。原candidateTimeChain=false来自GameTime：公开旧UE4构造PrevGameTime=当前−delta，暂停与静止下不强制等于上一采样；保留原manifest，不判采集失败，也不验收动态时域。采样索引7→0不是帧号mod8，初稿审计断言被反例拒绝后已纠正旧脚本单帧巧合假设。新check-live-pair审计raw/哈希/real矩阵链，重复重放995/995负对照false。原文件/log复制保全；新增[首次实机验证](docs/AC7-First-Live-Pair-Validation.md)。只更新触发cmd等待5秒（独立测试5256ms发请求、已有请求拒绝不改原文），包ac7-two-frame-flight-trigger备齐未动游戏。下一次重启进正常飞行后发延迟请求立即回游戏，先动态两帧资格再8–16；当前0新游戏/模型/提交。

**第二次实机：非尺寸错误，v3处理旧请求和多consumer。** 102664目录报Duplicate motion consumer，无raw/scene metadata；log00:33:25启动、29.601arm、30.676失败，现场request00:34:53.309后建，arm早于该新请求约84秒，支持遗留请求在启动阶段被消耗，不能视为正常飞行验证。已保全证据。新增公开process时间请求门禁，旧marker归档不arm；guide最多16个绑定/viewport组合精确匹配prewarp DSV/View/viewport，相同组合去重、不同MV歧义拒绝，原scene/曝光/缺帧约束保留；64event候选trace与v2描述/预算保留。单元/GPU旧新请求、配对/歧义/上限及原校验debug0通过；派生重放旧请求不arm归档，fresh新请求完成两帧/6events，原15raw/8纹理快照精确，995/995仍非相邻。首次日志watch触发测试因缓冲太晚未采入，保留并改测试触发方式，不当真实游戏定时策略。v3包准备未改游戏；下一次退出后只换addon、移走遗留request，飞行后单次请求。无真实游戏启动/模型/提交；原尺寸与真实多视图还需样本。

**首次实机反馈与v2修正：** 用户118836采样目录仅INCOMPLETE/failure，`Unsupported scene dimensions or View rect`；插件加载/请求/目标draw观察路径已走到scene guard，但无真实帧资源。原日志已保全；swapchain5120×1440/ResolutionQuality100不能替代内部allocation/rect，实际触发分支未知。修正v1把离线尺寸当硬上限、错误信息不足的问题：v2以两帧总预算≤256MiB检查，超预算在分配前拒绝，保留D3D11/rect/格式/device，scene先写3资源描述/rect/预算metadata；插件viewport上限同步修改。新增32×1456padding接受/4096×2049超预算拒绝硬件回归及原自检debug0通过；派生重放原15raw/8纹理快照精确、metadata正确、两帧停止、995/995非相邻。v2新包已准备但游戏内仍v1，不替换加载中DLL、不改分辨率/配置；下一次退出后仅升级addon，一次真实采样确认。无新真实游戏启动/模型/提交。

**有界采样已从方案推进到可构建、通过离线传输验证的工具。** 新D3D11 `TwoFrameSampler`固定2帧、256MiB逻辑上限、GPU staging/query异步完成后写盘，缺帧/重复/rect/deferred/跨device拒绝；硬件两组10资源有效分量精确、绑定保持/debug0。可选ReShade公开API20插件识别三个实际DXBC，保留原draw，最终派生AC7捕获重放exit0、两组10资源与原数据精确、原15检查点逐位；计数995/995、候选step/time=false，不冒充真实序列。已修复DllMain64KiB栈缓冲导致的加载退出及代理device指针误判（公开private-data标记验证底层identity）；全部失败/控制日志保留。单只读Luna复查SDK公开入口无已文档化D3D11有界控制，主会话核原头。新增[采样验证](docs/AC7-Bounded-Sampler-Validation.md)，官方ReShade固定commit/许可/哈希、独立诊断包已备齐但未部署；只读游戏目录未见dxgi/ReShade冲突。下一步真实游戏独立宿主两帧资格，不重试Nsight/NextFrame，不预设最终后端。无真实游戏/Nsight启动、无新模型/新真实帧/提交。

**最新实测：Next Frame存在但会crash，撤回A→Next Frame→B操作，暂停所有盲重试。** 用户重申F11曾无控采入非常多帧并卡死，已有Frame3062–4424/6,753,807事件记录支持跨帧采入能力；当前问题是可靠停止/帧数上限、完整资源导出与相邻核验，不再争论是否能多帧。崩溃主体/异常码未知，不能套用旧UE4SS异常。先复查既有记录/公开D3D11控制入口，暂无已证可用受限接口；有可靠边界再采，必要时评估窄只读资源采样。当前无需用户截图/新抓帧/导出巨大包；下方交互双捕获及历史fresh F11建议均暂停。0新抓帧/游戏/GPU/提交。

**文档优先，撤回截图前置。** 补查2025.5官方HUD/快捷键/导出章节及80条搜索索引：Pause and Capture Frame/默认F11是捕获，HUD箭头/Ctrl左右是当前帧事件导航，专用Generate C++活动导出后恢复应用；未找到D3D11保证恰好推进并捕获紧邻帧的明确合同。下一步按已证路径采样后独立核验计数/时间/矩阵相邻性；跨帧不送模型当连续，必要时评估有帧数上限的窄只读采样器，不反复抓帧碰运气。当前不要求用户截图/操作；下段旧“先请用户工具栏”已撤回。仅更新采样方案/文档证据，仍无新捕获/游戏/GPU/提交，4a75c2d保持唯一提交。

**已按用户要求提交一次并继续。** 首个本地提交`4a75c2d`含代码/离线验证/质量评估决策，提交时工作区clean，没有push或提交raw/模型/捕获。提交后新增[连续帧方案](docs/AC7-Temporal-Capture-Plan.md)和候选元数据脚本，不再自动提交。实际View候选995/mod8=3、前后时间差约8.3336ms与曝光dt相容，但仍需真实两帧核验身份；现有导出只有Frame0两Part。官方Nsight2025.5 Graphics Capture多帧CLI只适用D3D12/Vulkan，不能套AC7D3D11 Frame Debugger。下一步先请用户提供当前工具栏/tooltip确认控制，不按F11/不盲抓，再同会话2帧资格测试→8–16真实帧诊断；jitter必须实际渲染应用，零jitter序列可用于有限运动/历史诊断，不能完整验收SR。0新GPU/游戏启动或捕获，P1暂用，差异不当缺陷、不据倍率阻塞。

**画质判断最新决策**：NR预期重建/补光/材质变化，不以非中性输出与原图不同判缺陷。11暗通道>10倍等仅为诊断定位，不凭此加限幅、削弱NR或阻塞后续。接入正确性按明确契约，质量靠具体可见异常/连续帧与用户偏好；SR稀疏高光平滑/FXAA梯度也不是质量评分。见[评估决策](docs/AC7-Image-Quality-Evaluation.md)。下一步继续真实序列/jitter/history窄方案，P1暂用、预曝光延后。

**最新：扭曲前新配对NR→HDR→SR实际求值/回接完成。** 465后新代理/源哈希与旧postwarp不同，模型实际各Evaluate1次、输入逐位/fence/debug0，旧结果未混用。GPU恢复/源全图逐位后SR写回，保留原466/467warp、曝光/后处理/UI，FXAA保留及RGB旁路均通过。中性15raw、SR恒等18raw逐位，SR比NR最终896,097像素变化/区外0，guides/扭曲场/3UI与原阶段逐位，错旧代理/源两次负向在对应写回前拒绝。NR20,478高光保护像素不变但11暗通道>10倍(max17.1)，SR细高光衰减未验收；不是画质评分。见[prewarp新模型链](docs/AC7-Prewarp-NR-SR-Validation.md)。下一步真实连续帧/帧身份/jitter/history/reset的窄方案，不能循环同帧冒充序列。P1暂用、预曝光延后，5成功重放+2拒绝、新模型2次、0游戏，无原capture/SDK/游戏改动/提交；实时桥/高输出/FG尚无。下方“尚未新跑模型”是旧边界阶段记录。

**最新：候选边界优先移到原warp前（465后/466前）。** 已读取prewarp颜色/扭曲场/深度与MV；460场景=465场景，原466/467改变88,621颜色像素而depth32/MV逐位不变，stencil变化227,425像素。最大texel位移长度31.09输入像素。新边界中性HDR往返15项raw全部逐位，原postwarp/final与旧捕获逐位。初次整块depth断言失败经分量核对保留stencil差异，未掩盖。见[prewarp验证](docs/AC7-Prewarp-Boundary-Validation.md)。下一步以新prewarp输入重新配对GPU代理/NR/HDR/SR，写回后保留原warp及FXAA RGB旁路对照；严禁直接复用旧postwarp模型结果。P1继续暂用，0新增模型求值/游戏启动，未改原捕获/SDK/游戏无提交。新边界只是同尺寸离线候选，透明/上游偏移/时域/低高输出另验。

**最新：FXAA保留/旁路对照完成。** 默认路径13raw与前轮逐位；仅替换有效区RGB、保留alpha/区外契约后，上游和三张UI逐位一致，最终78,379像素改变、区外0。首轮全图copy因改变padding alpha被拒绝/修正，失败证据保留。UI前统计保留FXAA时尾翼梯度比例0.97629、海面0.93371，天空选区无变化；只有空间平滑证据，无动态抗锯齿或性能结论。后续SR实验优先FXAA RGB旁路候选、保留原组对照，不改游戏设置。下一步候选位置warp颜色/guide及真实连续帧/jitter/history/reset。P1暂用/预曝光继续延后，0新增模型求值、无游戏/原capture/SDK改动无提交。见[FXAA对照](docs/AC7-SR-Writeback-Validation.md)。下方旧“下一步FXAA对照”是前阶段记录。

**最新：SR同尺寸离线回接通过资源/区域/UI检查。** 用户确认菜单仅FXAA、无TAA选项；捕获Draw427辅助历史滤波不当作全场景TAA，526的post-tonemap空间AA本轮保留。派生重放467后NR恢复→整图逐位核验实际SR源→SR packed写回→原曝光/tonemap/FXAA/后处理/UI；恒等12项raw逐位，SR最终896,406像素改变、有效区外0，三张UI逐位，错源负向写回前拒绝。用既有NR/SR输出、0新增模型求值，P1继续暂用；见[同尺寸回接](docs/AC7-SR-Writeback-Validation.md)。下一步原FXAA保留/旁路对照，再解决候选位置warp颜色/guide及真实连续帧，未选最终hook/桥/宿主，非实时或高输出/时域验收。下方“未SR回接”是此前诊断阶段记录，当前指未实时接入。

**用户最新优先级：预曝光调查延后。** 基于已有P=1单Reset诊断未见明显整体异常，后续实验暂用P=1，保留未校准标记；不再以找出真实P阻塞插入位置、回接和连续帧验证。下一步转向真正SR输入/插入位置及原后处理/UI回接。海面小高光衰减仍未验收，不能归因P；若后续出现明显亮度/曝光跳变、闪烁或历史异常，再重开预曝光排查。本节下方追查P的后续建议仅为此前阶段记录，不是当前执行要求。

正式NR310.9.16.0已完成D3D12真实AC7单帧SDR Evaluate/GPU完成/读回：三张输入逐位一致、11,059,200个输出RGB通道全部有限、输出非恒等、D3D12 debug error为0。NR在本机D3D11特性不可用；D3D12 scratch PlatformError保留，创建和求值均不依赖其先成功。官方Remix明确NR工作在SDR空间，本轮采用无HUD post-tonemap真实场景及同帧规范化depth/MV，Reset=1，见 [单帧求值](docs/AC7-NR-Single-Frame-Evaluation.md)。

已完成真实HDR→CPU SDR代理→GPU NR→CPU HDR恢复参考验证：关闭NR的中性往返浮点零变化、原R11G11B10逐位一致；开启后全部输出HDR有限、最大166保留、20,566完全高光保护像素不变。官方公式的epsilon暗部限制已复现，本项目改为转移NR相对实际FP16代理的改变量。7个暗部通道增亮>10倍仍待画质验收，见 [HDR参考验证](docs/AC7-HDR-NR-Roundtrip.md)。

当前合同核查已完成：NR310.9.16.0直接输入真实HDR时，即使修改强度全零，输出RGB也严格等于clamp(input,0,1)；官方路径明确SDR、公开专用接口无HDR切换参数。因此关闭原生HDR直入分支，模型输入必须是SDR兼容代理，原HDR可保留在外部链中。

用户接受的候选顺序已固定为：场景HDR→SDR代理→NR(SDR)→恢复HDR→SR(HDR)→原后处理/最终tonemap→FG最终数据与呈现。资源是否HDR按插入位置语义判断，不按显示器开关；不把NR临时代理交给FG。

后续继续授权的局部GPU验证已推进至实际NR回写：D3D11 GPU代理真正送入正式D3D12 NR，重放现场再逐位验证同一代理后GPU恢复/写回HDR，修改经原后处理/UI进入最终帧。中性9项raw逐位一致，NR模式三张UI合成输入逐位一致，HDR166及20,566完全保护高光不变。故意配错CPU代理的负向测试在写回前拒绝，见 [GPU代理NR回接](docs/AC7-NR-GPU-Writeback-Validation.md)。

原曝光测量响应了NR修改，但目标2.470→2.450均被实际[0.5,1]限幅到1，当前增益不变，复算误差8.60e-8；不是动态曝光/闭环时序验收。GPU阶段通过离线文件/CPU staging衔接，不是实时资源桥或同进程连续链。

正式SR310.9.16.0已从能力/创建推进到两组显式独立输入域下的2560×1440 HDR/DLAA Reset诊断：原始HDR和NR后HDR均Evaluate/GPU完成/四输入读回成功、debug0、全部RGB有限且>1范围保留。PreExposure=1/ExposureScale=1明确标为诊断假设，未证明游戏实际因子；峰值166→102.8125、较大局部变化尚未验收画质，未SR回写、未多帧或超分。见 [SR Reset诊断](docs/AC7-SR-Reset-Diagnostic.md)。

此前“所有SR Evaluate暂缓”已收窄为“原生输入标定/接入验收暂缓”，允许有显式假设的单帧API/格式诊断。只读scout扩展生产链到156writer/33唯一PS统计及末两级，仍无pre-exposure语义证明。

最新[SR空间响应分析](docs/AC7-SR-Spatial-Response.md)：两组已有raw约90%绝对差异来自0.47%海面稀疏高光像素，最强点17×17数值和约仅保留44%，不宣称邻域守恒。同参数仅新增一次合成Reset控制：平坦166输出162.75、单像素160输出3.83、65×65亮块中心159.625，全图输出最大185.125；不支持固定HDR上限103，支持空间结构相关响应。公开helper核对没有已证明的必需缺项，未填虚构帧时间/猜曝光。真实pre-exposure和时域/高光画质仍未验收；下一门槛为实际view/曝光语义及有明确导出方案的真实相邻帧。现在不让用户盲目F11重抓，不重复单帧当历史。用户不深改工具链限制保持，FG/高输出/性能未验收，未部署或提交。

最新[View布局核对](docs/AC7-View-Exposure-Layout.md)：公开UE4.18 GameWorks参考的旧ExposureScale局部序列与AC7实际row125–133多字段相符，row129.y=1.333333为旧字段候选；参考填充代码明确“Only used for MobileHDR == false”，不能当P。UE4.22新增PreExposure/倒数及其他字段，直接套偏移不成立。32个实际View绑定PS/槽位字面row129扫描无访问，有覆盖限制，不证明全路径不存在P。下一步优先核实旧桌面HDR生产路径是否未预曝光，而非预设存在新版字段；仍不猜P=1，不盲目采样，不深改工具链。本轮0GPU/NGX/游戏调用、无部署/提交。

曝光两端后续：Scene210→469/471采样/平均→473对数亮度→475/477/479/481平均→482适应→525乘曝光的资源链/CB及CPU预测对上，误差8.60e-8。旧/新曝光纹理全draw已声明PS/VS关联四处（427时域辅助混合权重、482适应、484阈值、525tonemap），不混淆为场景预曝光。六个生产PS准确末端已由主会话检查/脚本断言；child初稿有混淆，已挑战修订且保留残余措辞错误，结论按原shader不直接采信。完整上游尺度未证明，倒数测光可吸收固定P，“无显式除P”不能证明P1。下一步追具体保留颜色项/光照向量/VS插值尺度来源（Draw33 View138.x实际0，已排除其本帧贡献），再评估是否需受控曝光观测，禁止无界阴性扫描；0GPU/NGX/游戏、无参数/配置变更无提交。

文档维护：当前状态集中在本节、README和API核对；Nsight启动/首帧Live Analysis旧调查已移入history/nsight-startup，RenderDoc实验在history/renderdoc。历史文件用于追溯，不再作为当前执行步骤。

## 阶段 1：共同调查游戏运行事实

本阶段已取得D3D11设备/交换链、两份可独立重放的C++ Capture、正常飞行scene/HUD分离与打包速度/矩阵相机回退证据。D3D11输入模块GPU数值与状态恢复通过，同尺寸旁路后最终图逐位一致；曝光应用通道已读回复算。详见 [正常帧](docs/AC7-Nsight-Normal-Flight-20261028.md)、[代码验证](docs/AC7-D3D11-Guide-Bypass-Validation.md)、[颜色曝光](docs/AC7-Color-Exposure-Backend-Gates.md)。最新正常样本未开超宽屏补丁，有效场景中央2560×1440，不能当最终全幅条件。另用户报告后续Nsight抓帧卡30%；只读检查发现14:01 Nsight UI AppHang及UE4SS同偏移间歇崩溃，但尚不能将其与进度阶段对应，见 [30%挂起调查](docs/AC7-Nsight-30-Percent-Investigation.md)。用户截图已确认多帧：当前Frame Debugger捕获约6,753,807事件、Frame3062–4424；基线单Frame10245约15,085事件。工作集高约16.9GB，长采集导致耗时是强假设；但当前项目Frame Debugger仍为Manual且没有frame-count字段，设置变化原因未知。先保全当前结果，不Resume/Next Frame/导出巨大捕获。之前CLI启动的probe实际卡住（PID39680，现已退出），不能算捕获成功。用户关闭该运行后独立启动probe，Nsight截图目标PID26464仅单Frame #108、5个事件；因此Nsight不会对所有D3D11目标无条件多帧，AC7会话/捕获路径仍是差异候选。用户确认probe截图也是F11手动捕获；故同一F11在probe上单Frame而AC7上约1363 Frame，目标应用/会话交互而非hotkey普遍设置差异成为强嫌疑。F11为触发请求，Nsight在目标进程观察D3D11/Present并据此采集；应用可影响观察时序和帧边界，但无证据它可直接设置frame-count。1363 Frame候选为一次采集边界或host-target状态异常，不是高帧率本身。根因未定位；若继续，先结束当前会话安全保全，再用fresh Nsight UI对AC7同配置只F11一次。未再执行CLI或操作当前UI。公开检索找到NVIDIA论坛UE5/Vulkan PathTracing捕获卡55%且版主复现并开内部case的相似但非同类报告；未找到AC7/D3D11/多帧事件的精确匹配或公开修复。详见 [30%挂起调查](docs/AC7-Nsight-30-Percent-Investigation.md)。

- [ ] 记录实际游戏版本、启动参数、显示模式、现有加载器和相关 mod。
- [x] 确认真实使用的图形 API、设备、适配器和交换链描述。
- [ ] 选定可重复的飞行片段，记录当前画质、渲染比例和显示分辨率。
- [x] 确定采样工具与最小观测方式，保留回退路径。
- [ ] 定位原 TAA、空间放大、色调映射、后处理和 UI 绘制阶段。
- [ ] 取得颜色、深度、运动矢量、jitter、曝光与视图常量的样本及绑定证据。
- [ ] 确认资源有效区域、帧对应、生命周期及历史重置。
- [ ] 标明高分辨率 SR 结果可回接的位置，以及无 HUD/UI 数据来源。

现有帧捕获已确认运行API为D3D11；既有源码检查、CPU/GPU映射一致或requirements通过都不等于原生时序输入和目标模型求值已验收。

阶段产物应包括采样条件、可重现步骤、证据索引及“已确认/未知/不适用”清单；不能用零值或配置默认值填补未知项后宣布通过。

## 阶段 2：以游戏证据完成选型

- [ ] 按真实 API、资源格式、后处理回接和呈现条件，逐项比对候选后端。
- [ ] 区分现有 API 可直接使用、需要窄接口扩展、需要侵入修改的部分。
- [ ] 比较同进程及跨进程处理的资源共享、等待、失败恢复和维护范围。
- [ ] 核对 Pre-SR 周边 NR 的数据变换与历史实现。
- [ ] 核对 FG 的相机、时间、无 HUD/UI 及交换链前提。
- [ ] 记录选择、淘汰理由和仍待验证的条件。

候选资料见 [架构调查](docs/AC7-Native-DLSS-Architecture-Selection.md)。本阶段完成前，不将任何候选标为最佳或默认宿主。

## 阶段 3：验证最小适配接口

仅在阶段 1、2 有足够证据后开展：

- [ ] 定义实际所需输入/输出、帧身份、所有权及同步契约。
- [ ] 验证后端调用和真实失败行为，不只检查成功返回码。
- [ ] 先同尺寸旁路/重建，验证图形状态、历史与游戏回接。
- [ ] 实现低分辨率输入、高分辨率输出，替换原 TAA/放大并保留清晰 UI。
- [ ] 验证加载、暂停、相机切换、任务重启、分辨率变化和退出。

“适配接口”不自动等于外部 IPC，也不等于必须把某个完整框架装入游戏；形式由选型决定。

## 阶段 4：逐项接入目标链

- [ ] 整幅单次 Pre-SR NR，验证颜色空间、曝光、引导数据和历史。
- [ ] 超宽屏 NR 中心保护、横向周边减负、平滑过渡及正确解包。
- [ ] 先 2× FG，验证无 HUD/UI、帧身份和呈现节奏。
- [ ] 测量整条链的真实帧率、输出帧率、GPU 帧时间、显存和延迟。
- [ ] 检查飞机/导弹/云层/遮挡/HUD 的动态画质及失败回退。

## 工作记录规则

- 本项目相关的新文档、适配器代码与验证工具放在此目录。
- 参考源码、已运行测试和 AC7 实测分开记录，明确不能据此推断的内容。
- 每个可执行阶段完成后更新结果和未决项，不因某个模块编译成功跳过前置调查。
- 未确认的数据会阻止该阶段验收，但不阻止继续整理证据或排查。
