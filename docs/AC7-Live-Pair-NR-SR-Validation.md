# 真实两帧GPU代理→NR→HDR→SR离线链

## 结果与范围

实际相邻帧1123→1124已完成：**D3D11 GPU guide规范化/SDR代理 → D3D12正式NR → D3D11 GPU恢复HDR → D3D12正式SR连续历史**。NR和SR各自在同一feature、同一组texture中按Reset1→0求值。所有求值/GPU完成/输入读回/debug/有限数据检查通过，错代理负向拒绝。两帧原始数据保持。

这是文件/CPU staging衔接的离线链，**不是D3D11↔D3D12实时互操作，也未把输出写回真实游戏或原后处理**。后端/进程架构未因此确定。当前仍需更长真实序列验证稳定性，不能据两帧宣布完整模型画质通过。用户授权在需要补测或可进游戏测试时明确提示；本阶段不要求重复两帧，不提供未经验证的实时部署包。

## 子任务与公开合同

用户要求简单中间任务用subagent：本次只有一个只读Luna scout，workflow`7e929cab-c388-475e-837f-5fc577943598`、child`b8bbbcaf-fe4a-4b05-9614-4d682e29a239`，无修改/GPU/游戏动作；[结果已保留](../artifacts/research/live-nr-sr-pair/nr-pair-contract-audit.md)。主会话核验公开`nvsdk_ngx_defs_dlssnr.h`的`DLSSNR_Reset`及helper的InReset映射。公开参数可以表达1/0，**不由此推断NR内部如何利用时间历史**；本次没有NR Reset对照，因此只接受其连续公共API执行合同。

NR保持此前强度/optional mask/反深度/MVScale参数，未削弱模型、修改SDK/模型或试探私有接口。SR保持HDR/DLAA2560×1440、P1/ExposureScale1显式假设、实际jitter0。NR使用当前公开SDR路径，HDR在外部保留并恢复，不尝试原生HDR直入。

## 实现与配对

新增[gpu_live_pair.cpp](../tools/capture-addon/gpu_live_pair.cpp)和CMake目标`gpu-live-pair`，只接文件，不注入游戏，复用已有`GuideNormalizer`和`HdrProxy`：

```text
gpu-live-pair prepare LIVE_PAIR NEW_GPU_OUTPUT
gpu-live-pair restore LIVE_PAIR NEW_GPU_OUTPUT NR_PAIR_OUTPUT EXPECTED_GPU_PROXY_PAIR
```

逐帧检查实际View rect/allocation/jitter及raw头/长度。prepare生成GPU proxy、cropped depth、NDC motion、validity，并用代理自身中性恢复原scene；两帧整图原格式逐位。restore重新计算同一scene代理，**逐位匹配实际NR输入后**才发布恢复后的HDR。只写有效rect，全图左右padding严格保留，绝不修改原文件。

首次构建遗漏`<string>`已修正；首轮GPU准备错误把validity的R32_UINT当1字节读回，立即报Invalid guide并停止；改为实际32bit分量后通过。失败日志/输出保留在`gpu-build.log`、`gpu-prepare/`，成功使用`gpu-prepare-v2/`，没有把失败输出用于模型。

新增[prepare_live_nr_pair.py](../tools/nr/prepare_live_nr_pair.py)检查实际audit/hash、GPU中性原scene、depth逐位CPU裁切、mask只含1/2、代理有限SDR，再转向后像素FP16。两帧GPU-vs-CPU FP16运动最大分量差0.001953125px，GPU浮点到half误差最多0.0009765625px；保留差异，不宣称逐位相同或完整透明/遮挡引导校准。默认参考阈值0.01px，超出即拒绝。

现有preflight/evaluate最小扩展`d3d12-nr-pair-history`，复用前次SR的持久texture/逐帧提交读回循环；NR Reset分别1/0，原强度/错误处理/释放不变。SR的输入缺曝光/覆盖初始化前检查保留。

新增[prepare_chain_sr_pair.py](../tools/nr/prepare_chain_sr_pair.py)核NR输入hash与restore的proxy完全相同，复用已有R11→RGBA16F精确转换和曝光处理，生成NR后SR输入；实际两帧policy在derived contract记录为1/0。既有单Reset转换函数的stdout仍含历史单帧诊断说明，**实际本次求值/Reset以NR/SR运行日志和sequence-contract为准**，不拿转换打印当执行证据。

## 实测

目录`artifacts/research/live-nr-sr-pair/`保留全部raw、日志、合同、hash和[chain-checks.json](../artifacts/research/live-nr-sr-pair/chain-checks.json)。可复查工具：[check_live_chain.cjs](../tools/nr/check_live_chain.cjs)、[check_sr_pair.cjs](../tools/nr/check_sr_pair.cjs)。

| 检查 | frame1123 | frame1124 |
| --- | ---: | ---: |
| GPU中性scene | 全5120×1440逐位 | 全5120×1440逐位 |
| GPU有效guide | camera3,408,330 / object278,070 / invalid0 | 同左 |
| NR Reset | 1 | 0 |
| NR有限RGB范围 | 0.0167847–1 | 0.00389862–0.999023 |
| GPU恢复HDR非有限RGB | 0 | 0 |
| 恢复HDR峰 | 176 | 174 |
| 恢复HDR有效区改变像素 | 3,633,436 | 3,645,643 |
| 区外改变像素 | 0 | 0 |
| SR Reset历史分支 | 1 | 0 |
| SR有限RGB通道 | 11,059,200 | 11,059,200 |
| SR HDR峰 | 109.3125 | 96.5 |

NR两帧Create/Evaluate/fence/3输入读回各次逐位，D3D12debug0、release/destroy/shutdown成功；NR feature及color/output地址在两帧间不变，实际其他textures由持久容器持有。SR同样通过4输入读回和清理；GPU prepare/restore D3D11debug0。GPU恢复的代理与实际NR color源完整相同。

SR在同一组**NR后HDR输入**上另跑Reset1→1对照：首帧逐位相同；第二帧2,989,365个RGB像素不同，HDR绝对差均值0.0181648363、最大64.34375，支持SR历史设置影响输出。此对照两组NR输入完全相同，没有重新独立跑NR引入额外变量；差异仍仅诊断，不判优劣或缺陷。

总新求值2次NR、4次SR；D3D11 prepare成功1组、restore成功1组，另有保留的prepare格式误读失败和错代理负向。`nr-output`覆盖重试在NGX Init前拒绝；错代理（人为翻转frame0 proxy一个字节）在写`scene-restored.raw`前拒绝，见[拒绝日志](../artifacts/research/live-nr-sr-pair/reject-wrong-proxy.log)。

## 预览与画质边界

[诊断对照图](../artifacts/research/live-nr-sr-pair/comparison.png)：上/下两行对应1123/1124；左/中/右列为原HDR、NR恢复HDR、NR+SR历史输出。复用[preview_live_chain.py](../tools/nr/preview_live_chain.py)，各面板640×360、统一ACES/gamma；不是原游戏LUT/tonemap、没有原warp/FXAA/UI，也不是视频。主会话观察未见该缩略预览中的黑屏、全局错位或全图曝光爆炸；不能从缩略图验收细节、动态伪影、拖影或高光稳定性。

![诊断图：原HDR / NR恢复HDR / NR+SR，行1123/1124](../artifacts/research/live-nr-sr-pair/comparison.png)

根据[画质决策](AC7-Image-Quality-Evaluation.md)，NR预期光照/材质变化、SR统计差异和HDR峰值不单独判缺陷，不限幅来追求恒等。中性逐位只检验颜色适配器，非中性输出不要求恒等。

## 测试状态与下一步

**尚不能进行真实游戏内NR/SR实时处理测试。** 当前游戏里装的是只读采样插件，它不运行模型；实时共享资源/同步/最终回接与回退尚未实现，本次离线通过不能替代。也未实施FG、超分或超宽屏中心/周边映射。

下一项确实需要补充测试的是**同次会话连续8–16帧**，用于查看NR→SR历史稳定性和转场reset。现有固定2帧采样器不能通过多次独立运行凑序列；需先准备并验证有明确帧数/内存/磁盘上限的短序列采样器，再给用户一次具体操作。不得直接把2帧全量staging扩为16份而无视现有256MiB预算。当前无需用户操作；准备好后明确通知。

原始捕获/真实两帧/游戏文件均未修改，无游戏或Nsight启动，无新提交/推送；P1调查继续延后，实际jitter0与完整SR采样门槛保留。
