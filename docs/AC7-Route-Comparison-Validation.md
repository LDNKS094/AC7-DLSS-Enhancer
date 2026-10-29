# 同通路路线对照：OptiScaler 与直接 NGX

结论：直接NGX在同一份单桥整链代码上完成真实八帧40阶段＋8份D3D11返回逐位对照；随后两路线各64个有效诊断样本显示，直接NGX的NR＋SR Evaluate CPU调用均值少约0.070ms，而颜色＋模型D3D12 GPU均值约4.00ms，差异很小。没有依据把这些数值换算为游戏FPS或宣布某框架明显更快。

工程建议：**下一固定尺寸NR/SR原型优先路线2（自有适配＋直接NGX）**。理由是当前两路线共享同样的自有颜色、共享桥和发布控制，直接调用已经等价，OptiScaler在此组合只提供NR转发与SR包装，还带入全局hooks和菜单/配置生命周期。路线1保留，尤其作为后续FG/压缩的复用候选；完整游戏宿主和FG架构尚未选定，路线3继续暂缓。这是分阶段建议，不扩成全自研FG。

## 同条件范围

[color-interface与chain-direct-interface](../tools/optiscaler/CMakeLists.txt)从同一份[color_probe.cpp](../tools/optiscaler/color_probe.cpp)、[model_chain.inl](../tools/optiscaler/model_chain.inl)和[measure_chain.inl](../tools/optiscaler/measure_chain.inl)编译：

- OptiScaler仍动态加载标准DLL的公开导出；直接版只将入口绑定为官方SDK loader的公开函数，不加载OptiScaler。官方loader SHA256为bc43df9ec032004fde681d3e3afd01cde239e03a0125b822e88674c84287f99c。
- 模型均为相同官方310.9.16.0 NR/SR，实际加载路径及文件SHA核验。相同GUID、2560×1440同尺寸HDR/DLAA诊断（不是真超分）、NR强度、实际dt/jitter0/P1诊断合同与Reset控制。
- 原始5120×1440 R11＋规范引导→同一NT共享桥→D3D12颜色/NR/HDR恢复/SR→D3D11私有目标；相同ChainPublication、队列与退休策略。两边都是四份输入复制、一份返回复制和相同的11→12/12→11排序，没有因为选框架而叠加第二座桥。
- 相同D3D11/D3D12 debug配置，RTX5080。测量内无阶段图像读回、NaN上传或磁盘写入；每进程结束后读回末帧对照。参数函数新增默认开启的diagnostics参数，仅测量传false关闭测试cout，所有模型参数值不变。

直接版先单独跑真实八帧，40阶段及8个returned11均与原合格基线逐位；输入/源覆写和释放清理/debug0保持。它未使用OptiScaler菜单或代理，公开Shutdown1由官方SDK调用；OptiScaler仍使用已合格的公开无参Shutdown12。

## 测量设计与结果

执行顺序：Opti-A → Direct-A → Direct-B → Opti-B，四个新进程。每进程40次NR/SR：首8帧预热，其后32帧计数；每路线共64有效样本。片段为**重复同一真实八帧**，每轮用token gap触发双Reset；这是成本诊断，不是新采样、长时域画质或游戏工作量。

源数据在GPU初始化前读入，D3D11源纹理和目标、共享资源、颜色scratch与两模型持续复用。CPU计时分参数设置、Evaluate、record和bridge submit；五个D3D12 timestamp标记prepare、NR、恢复/转换、SR。每帧仅回读40字节时间查询，图像只在全部迭代结束后读一次。

下表为两进程合并的**均值**：

| 指标 | OptiScaler | 直接NGX |
|---|---:|---:|
| NR Evaluate CPU | 251.98µs | 220.51µs |
| SR Evaluate CPU | 136.42µs | 97.52µs |
| 两Evaluate CPU合计 | 388.40µs | 318.03µs |
| 整段命令record CPU，含公共诊断/查询/barrier | 618.81µs | 517.63µs |
| bridge submit CPU | 129.43µs | 115.91µs |
| prepare GPU | 0.0291ms | 0.0294ms |
| NR GPU | 3.2610ms | 3.2119ms |
| HDR恢复＋格式转换 GPU | 0.0451ms | 0.0453ms |
| SR GPU | 0.6958ms | 0.7167ms |
| 上述GPU阶段合计 | 4.0310ms | 4.0033ms |

两Evaluate合计的中位数为381.95/309.05µs，P95为415.8/374.4µs；GPU阶段合计的中位数为4.0235/4.0083ms，P95为4.3816/4.3367ms。每轮原始数据和分布保留，不做异常样本剔除。

CPU Evaluate包含模型runtime、驱动和包装行为，不能把全部0.070ms差异叫作“纯框架成本”。GPU合计不包括D3D11输入/返回复制的GPU时间，也不包括游戏、host取数/状态恢复、FXAA/warp/post/UI或呈现。completionPollWall是独立工具yield/fence等待，不能用其倒数算游戏FPS。样本相关、仅两个进程/路线、debug开启且没有功耗/频率控制；不能由0.028ms GPU均值差下性能定论。

Init＋两特性Create＋GPU等待的单次诊断约Opti1.84–1.92s、直接1.26–1.27s；Opti源码本身含500ms初始化等待。这里夹带缓存/首次runtime开销，记录供初始化设计参考，不作为稳定帧成本。静态loader在main前的成本不在entryBindingUs内，因此该字段不与LoadLibrary耗时作比较。

## 发现并保留的失败与配置勘误

原计划第一轮opti-a运行40次后报10条D3D12状态错误；第二轮opti-diagnostic-v2在索引30发现第一条错误即停止，已实际NR31/SR31。两轮退出1、INCOMPLETE保留，不进入64个有效样本。

资源命名确认错误对象是MeasureSR：调用方期待UNORDERED_ACCESS，实际留下COPY_SOURCE。固定源码`IFeature_Dx12.cpp:375`在OverlayMenu=false且_frameCount>30后进入旧Menu_Dx12路径；`Menu_Dx12.cpp:139–196`的非render-target分支先把SR输出切为COPY_SOURCE，但隐藏菜单未RenderMenu时没有还原。与首次索引30触发一致。**不修改OptiScaler源码、不压低debug检查，也不把测试缩短到30帧以内回避问题。**

配置勘误：OverlayMenu=false是选择旧菜单模式，并非禁用overlay。修正后的独立host使用默认true，避免该旧路径；本probe没有swapchain/Present，未触发现代菜单呈现。这不是“已关闭所有overlay”，更不是游戏菜单兼容修复。之前八帧证据没触发该分支，仍有效，但不能据其推断更长运行。

另一个源码事实：当LogToConsole/LogToFile等sink全关，PrepareLogger未安装带level过滤的新logger，初始化默认控制台logger仍可打印。本轮设置LogToFile=true、LogLevel=6/off，让logger按配置建立；最终OptiScaler.log为空，热循环无文件/控制台日志，未修改库。早版“LogToFile=false即彻底静默”不成立。

## 两路线仍需承担的工作

| 范围 | 路线1：OptiScaler公开NR＋SR | 路线2：直接NGX |
|---|---|---|
| 游戏取数、shader身份、状态与原warp前回接 | 自己实现 | 自己实现 |
| 当前颜色、共享桥、deadline/drop/双Reset与退休 | 本项目自己实现 | 同一实现 |
| 模型调用 | NR原生转发，SR包装层；需额外配置/路径/句柄管理 | 官方loader/API；同样需参数和生命周期管理 |
| 进程全局影响 | 已观察DllMain hooks、菜单、raw input/background机制；隔离SDK长期兼容要额外核对 | 本原型不加载OptiScaler，不带它的全局hooks；NGX仍有进程级runtime/cache生命周期 |
| 停止/卸载 | 完成后关与健康在途stop已有证据，模块热卸载未验 | 公共发布代码可复用；本轮实际验证完成后清理，尚未把直接版故障矩阵单独重跑 |
| NR代理与恢复政策 | 为保持现有合同，未采用内建compose/默认MaxRatio | 原合同直接可用 |
| FG、压缩、UI和呈现 | 功能存在但本项目实际复用资格仍0 | 后端另选，不自动变成自研FG |

下一步建议：以直接NGX优先构建**默认关闭、固定尺寸、无模型中性模式**的最小宿主，先在独立D3D11合成宿主核shader/资源匹配、全状态恢复、有效区域/外部保持、错误保原图与安全停止。已有诊断采样接口继续供定位参考；不自动恢复ReShade专用方案。接入前还要接在线GuideNormalizer、核宿主/proxy身份、同帧曝光及原warp消费前完成，准备用户操作的游戏包后再实测。

## 证据和计数

[证据目录](../artifacts/research/route-comparison-v1)包含直接8帧、两个失败目录、ABBA四进程的measurement.json/最终图/退出/日志、exe/配置和源码审查。

[check_route_comparison.py](../tools/optiscaler/check_route_comparison.py)核原采样、loader/模型摘要、直接40＋8逐位、128有效样本及四末帧逐位、Reset/预热/数值有效性、失败排除。[comparison-checks.json](../artifacts/research/route-comparison-v1/comparison-checks.json)保留每轮均值、全分布和限制。错误样本/不完整测量会被拒绝，不能以生成JSON代替验收；将有效样本数32改成31的负对照已被拒绝，未生成成功报告，未额外运行GPU。

本阶段实际：OptiNR151/SR151（失败40＋31、通过40＋40）；直接NGX NR88/SR88（资格8＋40＋40），合计239/239。Opti累计NR198/SR218。未再提交，仍81021a0；游戏/NVIDIA配置哈希与22个AC7登记路径复核不变，无游戏、AC7派生重放或部署。
