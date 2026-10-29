# 真实NR/SR共享纹理求值验证

## 结果

**后续已连接完整GPU图像数据链：** NR共享返回直接供GPU HDR恢复，恢复结果直接供SR共享求值，真实八帧56阶段raw与原基线逐位；中间无CPU/文件图像反馈，仍保诊断读回与CPU等待。见[整链验证](AC7-GPU-Chain-Validation.md)。下方“NR/SR分开、整链未接”是本页独立特性阶段的历史限制；游戏/实时预算仍未验收。

**真实NR与SR已经分别绑定到共享纹理桥，完成独立八帧D3D11输入→D3D12模型→D3D11返回。** 最终代码三组24份返回raw全部与[既有八帧文件基线](AC7-Live-Eight-NR-SR-Validation.md)逐位一致：NR history8、SR history8、SR每帧Reset8。每份返回也与同次D3D12模型输出读回逐位一致；feature和共享color/output identity保持，输入逐位、GPU完成、两API debug0、有限值和清理通过。

**这不是完整无文件NR→HDR→SR链。** NR与SR仍分开运行，SR使用既有文件链配对恢复后的HDR fixture。新增的是每个模型自身的共享GPU传输/执行资格，没有新增游戏内写回、宿主选择、性能或完整画质验收。无需用户补采/部署，本轮没有运行AC7/重放/Nsight或改游戏/NVIDIA配置。

最终证据：[final-order/checks.json](../artifacts/research/shared-model-eight/final-order/checks.json)、[NR日志](../artifacts/research/shared-model-eight/final-order/nr-process.log)、[SR历史日志](../artifacts/research/shared-model-eight/final-order/history-process.log)、[SR重置日志](../artifacts/research/shared-model-eight/final-order/reset-control-process.log)。原始首轮shared结果留在父目录，不覆盖。

## 最小改动

复用[preflight.cpp](../tools/nr/preflight.cpp)及[evaluate_frame.cpp](../tools/nr/evaluate_frame.cpp)原参数/feature生命周期，仅新增固定8共享传输模式，避免平行复制模型参数实现：

- `d3d12-nr-eight-shared-history`（sequenceMode5）
- `d3d12-sr-eight-shared-history`（5）
- `d3d12-sr-eight-shared-reset-control`（6）

原single/pair/eight文件模式保持。构建只把已有shared_texture_bridge.cpp编入工具并加include目录，无新依赖或SDK/模型修改。

共享桥内索引是color0/depth1/motion2/exposure3/output4，模型原顺序color0/depth1/motion2/output3/exposure4；显式映射`{0,1,2,4,3}`保持原求值参数。NR不消费曝光，以独立unity1×1满足现桥固定四输入接口；SR曝光使用每帧真实fixture。

1. 同NGX D3D12 adapter LUID创建独立D3D11 debug device，建立上一阶段[公开共享桥](AC7-Shared-Bridge-Validation.md)。
2. 每帧CPU文件只用于构造D3D11测试fixture；模型D3D12输入不再经过UPLOAD buffer，直接引用持久shared资源。D3D11 CopyResource→shared fence→D3D12 Wait是真正输入路径。
3. 先完成全部参数/命令录制，关闭list后才在受保护submission区upload共享输入与submit，避免录制/参数失败时提前留下未退休事务。提交后poll、错误/10秒未退休在独立probe里ExitProcess，不在GPU pending时释放allocator/list/模型/桥；这不是可直接移入游戏的退出策略。
4. 模型输入COMMON→NON_PIXEL_SHADER_RESOURCE，output COMMON→COPY_DEST诊断NaN seed→UAV；Evaluate后转COPY_SOURCE做测试readback，再全部回COMMON。NaN seed仍使用一个D3D12 upload，仅用于识别未写完整输出，不属于模型输入数据交换。
5. poll必须明确published才读返回，不把discard当成功；本测试使用相同frame token并在独立进程里等待每帧完成，未模拟实时预算。
6. 返回D3D11后再readback核对D3D12结果，核对通过才保存raw。D3D11 staging Map和输出文件是检查证据，不是跨API生产传输。

Reset沿用首帧1/其后7个0，对照每帧1。NR强度/结构/风格参数、depth inverted、向后motion、SR DLAA HDR flags、P=1、actual jitter0与实际FrameTimeDelta均保持基线值。

## 硬件验收

| 检查 | 最终代码结果 |
| --- | --- |
| NR8次 | 同feature/共享资源、Reset1后0，24原始输入读回逐位、8Evaluate/fence/返回11成功 |
| SR history8次 | 同feature/共享资源，32输入读回逐位、实际frame time一致、8Evaluate/返回11成功 |
| SR reset-control8次 | 每帧Reset1，32输入读回逐位、8Evaluate/返回11成功 |
| 输出身份 | 24份D3D11返回=同次D3D12输出，24份返回=原文件基线整个raw，最大差0 |
| 数值 | NR全部RGB有限[0,1]；SR每份11,059,200有限RGB，alpha有限，NaN seed未残留；HDR峰与基线完全相同 |
| 状态/清理 | 每帧D3D11/12 debug errors0；ReleaseFeature/DestroyParameters/Shutdown均Success |
| pre-init拒绝 | 原输出重用、缺输入、缺actual time文件均Init前拒绝，未启动求值 |
| 旧路径回归 | GUID self-test无NGX、既有文件SR结果checker重新通过；没有为了回归重跑旧文件模型 |

新增[check_shared_models.cjs](../tools/nr/check_shared_models.cjs)核三模式帧数/顺序/Reset、feature纹理指针稳定、88输入读回、24返回、debug计数、cleanup、原fixture哈希、实际SR时间、FP16有限与原基线逐位。前置负向证据[negative-checks.json](../artifacts/research/shared-model-eight/negative-checks.json)。

首轮24次已全通过。之后主会话把shared upload推迟至录制完成、加明确published判断，因同步顺序改变，再按最终代码复验完整三组24次，仍全逐位。**本轮总新增NR16次、SR32次，合计48次**，其中最终代码的验收集NR8/SR16；不是只报最终24来隐去复验。

## 子审查与主会话复核

只读Luna，workflow`4d028173-bef3-4635-b180-bba1d1cd38ec`、child`68b6416a-3cf9-4807-9f8f-a71d9e1c7bc3`，[报告](../artifacts/research/shared-model-eight/shared-model-path-scout.md)。未发现映射/状态/count/feature复用具体问题；指出poll应明确published（已补），指出“NaN只在11destination”则不成立：i==3仍分配output upload并复制到shared D3D12 output；主会话逐行复核+所有RGB/alpha有限检查。没有为误报移除哨兵。所有修改/GPU/验收由主会话执行。

## 下一步和边界

- 接下来需要把已验证NR共享返回直接喂D3D11 HDR恢复，再生成SR HDR输入并交SR共享求值，验证**单一GPU数据链**，中间不依靠文件/CPU整帧搬运，仍在独立probe中与原基线核对。
- 当前D3D11 fixture每帧由CPU初始化，缺真实game proxy/native device身份与资源重用/resize/session适配；不能把fixture入口等同于游戏资源已接。
- 已有pending/stale原图回退由上一阶段的确定性gate测试支持，本轮模型执行只等待完成，不宣称对模型延迟/丢帧后的history/reset恢复已验收。
- poll不阻塞并不能保证同帧可用率。正式预算、超时、drop/reset、生命周期、device lost与卸载仍待实现；独立工具ExitProcess不能进入游戏代码。
- 未测试真实camera cut/透明粒子/长时闪烁、jitter注入、upscale、FG或实时性能；P1暂用，不因NR预期外观变化去限幅输出。
