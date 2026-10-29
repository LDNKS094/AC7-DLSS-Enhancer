# 同进程八帧完整GPU NR→HDR→SR链

## 当前结果

**此前分开的共享模型路径现已连接成同一进程内的GPU数据链。** 输入采用已保全的真实动态帧698–705，原生HDR/depth/packed velocity/View/exposure只在入口初始化D3D11测试纹理。之后：

```text
D3D11 native scene/depth/MV
  → GuideNormalizer + SDR proxy
  → D3D11 shared input → D3D12 NR → D3D11 returned NR
  → D3D11 HDR restore + R11→RGBA16F
  → D3D11 shared input → D3D12 SR → D3D11 returned SR
```

最终验收集8帧×7阶段共**56份raw与原文件链逐位一致**；NR8/SR8的feature与共享输入/输出资源identity保持，两API debug0，实际SR时间和Reset1→0序列对应。既有[八帧对照图](../artifacts/research/live-nr-sr-eight/comparison.png)也适用于本次相同结果，不重复生成预览。

**“GPU数据链”指中间图像不通过CPU/文件反馈给下游。** 本probe仍执行CPU轮询、guide validity读回门禁、阶段读回/参考文件比对/保存快照；不是无CPU同步或无诊断开销，不用于实时FPS结论。尚无游戏host、原scene最终写回、postprocess回接或FG。用户不需补采/部署。

## 最小实现

- 新独立工具[gpu_chain.cpp](../tools/nr/gpu_chain.cpp)，CMake target `gpu-chain`，复用原HdrProxy、GuideNormalizer与SharedTextureBridge，不改其shader/接口。
- 将原求值参数提取成[evaluation_parameters.cpp](../tools/nr/evaluation_parameters.cpp)/[header](../tools/nr/evaluation_parameters.h)，原file/shared工具和整链共用同一参数政策。没有复制一套NR强度、色域、MV、SR曝光逻辑，原NR设置与P=1/实际jitter0保持。
- 同一D3D12 device/queue、一次公共NGX Init、两个独立parameter/feature。NR和SR各持久一套共享桥、allocator/list和返回D3D11纹理，每帧复用，不重建feature伪装历史。创建参数与既有probe相同；正式模块路径加载后按filesystem identity核对。
- GuideNormalizer输出cropped depth32/NDC float32；输入全部validity1或2才准入模型，validity3拒绝。NR与SR共用本帧实际depth/motion/exposure。
- HdrProxy restored本来就是active2560×1440 R11G11B10 SRV，无需CPU/全allocation copyback就可转RGBA16F交SR；固定GPU shader只转换格式/alpha1，不tonemap或限幅。
- 每帧NR已完成并发布返回纹理后，第二次HdrProxy.record仍用**同一原HDR、同rect和曝光**，重新生成proxy并逐位核对NR源，再用实际NR返回恢复HDR，供SR直接读取。
- 只有模型纹理输入的GPU边参与模型数据传递。保存/比对的vector只用于断言，没有写回后续纹理。原生fixture在每帧CPU上传是测试入口，不能描述成已接游戏native资源。

## 运动FP16量化与失败证据

首轮`run-v1`在运动stage比较处被严格拒绝，**尚未Evaluate任何模型**（已Init/Create NR/SR），INCOMPLETE与输出保留。GPU直接写RG16F在此路径采用截断，frame0有3,632,908个half分量与CPU的round-to-nearest-even基线不同，通常1ULP。代理与depth阶段已经逐位。

先改为精确integer-significand量化round-to-nearest-even后，`run-v2`只剩34个分量的+0/−0位型差异；仍在模型求值前拒绝，没有放宽逐位检查。

最终局部转换明确编码IEEE binary16位型：

- 已有float32 NDC的sign/exponent/significand，与1280=5×2^8、720=45×2^4作精确32位整数乘法（最大product<2^30）。
- 正常数/half subnormal分别按最近偶数取整，保留符号零；不先把乘积舍入float32，避免double-rounding差别。
- 使用R16G16_TYPELESS临时纹理的R16G16_UINT UAV写入bit pattern，再GPU CopyResource到模型要求的R16G16_FLOAT，避免typed浮点store再次改变舍入/零符号。
- 没有改变运动方向/尺度、范围、数据源或模型参数；不是为了像原图而弱化模型。

`--self-test-motion`在GPU上验证40个分量：独立Python `struct.pack('<e', float32 * scale)`生成的预期值，涵盖正负零、正常数、half subnormal、极小数、向偶数不同方向的正中点与普通值；全部逐位/debug0，0NGX调用。证据[motion-self-test.log](../artifacts/research/gpu-chain-eight/motion-self-test.log)。其后真实八帧的58,982,400个motion half分量逐位匹配既有GPU-NDC→CPU-half参考，继而所有模型阶段也逐位。

## 硬件验收与回归

最终结果：[checks-v2.json](../artifacts/research/gpu-chain-eight/checks-v2.json)、[run-accepted日志](../artifacts/research/gpu-chain-eight/run-accepted-process.log)、[完成标记](../artifacts/research/gpu-chain-eight/run-accepted/complete.txt)。检查器[check_gpu_chain.cjs](../tools/nr/check_gpu_chain.cjs)核raw头/长度/哈希、NR/SR先前有限值证明的哈希、stage字节、feature/texture identity、Reset、实际SR时间、debug与清理，不启动模型。

| 阶段 | 八帧结果 |
| --- | --- |
| SDR proxy | 与既有NR实际输入whole raw逐位 |
| depth32 | 与既有本帧原生depth crop逐位 |
| backward pixel FP16 motion | 按明确量化逐位，含signed zero |
| shared NR returned | 与既有NR8输出whole raw逐位 |
| restore時重新proxy | 与该帧实际NR输入逐位，不复用别帧源 |
| GPU R11→RGBA16F restored HDR | 与既有SR实际输入whole raw逐位 |
| shared SR returned | 与既有SR history8输出whole raw逐位，RGB/alpha有限性随原证明hash验证 |

- 原始8帧全文件provenance/动态资格在调用前由既有audited_frames核验；CLI自身检查完成capture/audit存在、raw格式/长度、View布局/实际jitter0、counter/+1、game-real时间链、曝光范围、transform有限及原HDR/depth有效数值。它不引入JSON库完整解析audit，调用方必须先完成原有审计，不能仅凭同名audit文件认定源资格。
- 旧file模式NR pair2与SR pair2在参数提取后重新执行，与旧完整链各2份raw逐位；SR shared-reset8也重新执行，8份与原reset基线逐位。证明legacy未因公共参数提取改变。
- 原输出重用、缺最后depth、第7帧计数篡改、原native HDR NaN均Init前拒绝、无输出目录；篡改fixture先独立复制被改文件，未修改hardlink指向的保全源。原source哈希重新核对不变。见[pre-init-negative-checks.json](../artifacts/research/gpu-chain-eight/pre-init-negative-checks.json)。
- 两个最初量化失败0Evaluate；成功探索`run-v3`、补scalar门禁后的`run-final`和最终失败资源跟踪保护后的`run-accepted`各NR8/SR8。另有legacy NR2、legacy SR2及shared reset SR8。因此**本轮实际新增NR26/SR34，共60次Evaluate**；最终验收链本身是NR8/SR8，不混淆总调用数。
- 全部成功运行保持独立目录，失败/中间/最终日志不覆盖；原始捕获与游戏不改，无提交。

只读Luna检查转换接口，workflow6819e542-483b-4b89-8823-fd35957e3049 / child660688c8-de6c-450c-a3d4-e3429eb9d83a，[原报告](../artifacts/research/gpu-chain-eight/gpu-chain-conversion-scout.md)保留。主会话复核restored cropped格式与持久prepared覆写风险，并使用已独立验证的(-1280,+720)实际向后pixel尺度；所有实现/GPU/验收由主会话负责。

## 生命周期、时序与尚未完成

模型命令录制/关闭成功后才排共享输入/submit；NR/SR输入输出回COMMON后交棒。poll必须published且帧token匹配。每个model run在独立probe中等待本帧退休（10秒上限），submitted错误/无法跟踪creation完成时ExitProcess保护未退休资源，不释放仍pending的list/feature/桥。构造失败但尚未提交时释放已经获得的feature/parameters。**这些是独立测试进程边界，不是可复制到游戏里的失效/卸载方案。**

诊断读取stage会阻塞GPU和渲染线程；validity现在也整图读回。即使数据未反馈，也不代表实时可用率、延迟或性能已合格。未来需去掉诊断开销并定义有界等待/丢帧策略，串联NR/SR历史Reset、设备失效/resize/session及资源退休流程。

本轮主会话只启动gpu-chain/preflight/构建与检查工具，没有启动真实游戏或AC7派生重放，也未部署。环境复查发现同期真实Steam游戏会话在本地06:40–06:43运行，显示INI时间戳随退出更新，但其1150字节与此前保全副本**逐位一致**；NVIDIA自动OPS仍0，AC7识别路径仍22个，新工具没有被分类成AC7，相关时段没有发现Automatic OPS apply。见[environment-review.json](../artifacts/research/gpu-chain-eight/environment-review.json)。不把“工具未主动写配置”扩大成“外部会话没有保存文件”，也未复现此前全屏/200间接覆盖。

下一阶段已完成独立post-submit等待预算/迟到退休/双Reset恢复测试，见[等待预算报告](AC7-Deadline-Retirement-Validation.md)；整帧预算与通用运行时仍未完成。以下为本报告当时的后续计划。

下一步：把此独立整链拆出可控调用入口，先验证deadline/drop/reset与同帧失败回退，再确定host/proxy-native device身份和原warp/post/UI回接。当前结束于D3D11 RGBA16F SR返回，**尚未写回原生scene，更没有启用游戏内NR/SR/FG**。P1暂用、actual jitter0、全动态guide与画质/性能未验收；不需要用户新的采样或替换插件。
