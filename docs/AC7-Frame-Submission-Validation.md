# 有界帧提交与源纹理覆盖验证

本轮从独立整链提取了可复用的提交/轮询接口，并在同适配器 D3D11/D3D12 硬件上验证复制后覆盖源纹理。未接游戏，也未完成通用整链运行时调度或整帧性能验收。

## 实现

- [FrameSubmission](../src/d3d11/frame_submission.h) 接收调用方的绝对 `steady_clock` 截止时间、帧 token、四张输入和已关闭的 DIRECT 命令列表。`begin` 在已过期时返回 false，不上传、不提交、不消耗 token；busy 或缺命令列表则拒绝。成功后持有输入引用，到 `poll` 的终态才释放。
- 接口内部没有日志、文件、Map、等待或轮询循环。它复用 [SharedTextureBridge](../src/d3d11/shared_texture_bridge.cpp) 的复制/共享 fence，并向 `poll` 传入绝对截止时间。桥读取完成 fence 后仍检查时钟，过期即使同 token、GPU 已完成也不复制结果。
- 调用方仍必须保持桥、模型、allocator/list 和目的纹理存活至退休，并保证所有源写入在同一 D3D11 即时上下文中有序排队。接口不拥有模型，也不自动处理设备丢失或卸载。
- `gpu-chain` 的 Model 已改为复用 FrameSubmission；模型参数、颜色/guide 转换和 NR/SR Reset 政策保持原实现。诊断适配器仍在模型录制完成后，为每个阶段创建相对预算转换成绝对截止时间，**尚未把同一个整帧截止时间贯穿转换、NR、HDR 恢复和 SR**。与前一版相比，该诊断阶段预算现在从 upload 前开始，而不是 submit 返回后开始。

## 硬件验证

扩展现有 [validate_shared_bridge.cpp](../tests/d3d11/validate_shared_bridge.cpp)，复用已保全的八帧 SR 输入，不运行游戏或模型：

| 检查 | 结果 |
|---|---|
| 原八帧传输 | 32 输入 / 8 返回与原数据逐位一致 |
| 同组源纹理重复使用 | 四次事务使用相同四个源对象，每次先恢复原数据，再复制后覆盖 |
| 复制后源覆盖 | color/depth/motion/exposure 全部改写为不同的 0x5a 字节，并读回确认覆盖实际发生 |
| D3D12 消费端 | 四次事务共 16 输入读回仍等于复制前原数据，逐位一致 |
| 正常返回 | 源已被覆盖，返回的旧帧颜色仍精确匹配原数据 |
| 在途过期 | gate 阻塞时 poll 保持 pending，原目的图不变，完成后 discarded |
| 已完成才首次轮询 | 显式确认后置 D3D12 fence 完成，首次 poll 仍按过期丢弃，目的图不变 |
| 提交前已过期 | 无事务、无输入残留、不改变目的图；相同 token 随后可成功提交 |
| 非法调用 | busy、缺列表、错布局/缺输入、重复 token/submit、错误目的图、异适配器拒绝 |
| 生命周期与状态 | pending 持输入、终态释放；共享资源 identity 和 PS 绑定保持；两 API debug0 |

源覆盖验证的过期测试使用 2ms 截止时间和确定性 queue gate。测试还含诊断读回，目的在验证状态与数据隔离，不用于测量 GPU 性能或证明 2ms 能完成模型。

证据：[最终硬件 checks](../artifacts/research/frame-submission-v1/hardware-final/checks.json)、[过程日志](../artifacts/research/frame-submission-v1/hardware-final-process.log)。首版 hardware 与最终 hardware-final 均通过，旧目录未覆盖。

## 真实模型回归

- 无故障 `gpu-chain` 八帧，NR8/SR8；56 阶段 raw 与原文件基线逐位一致，debug0、正常释放与 shutdown。[检查结果](../artifacts/research/frame-submission-v1/normal-checks.json)。
- 原固定故障序列再次通过，主 NR6/SR5 加参考 NR3/SR3，共 NR9/SR8；四原图回退、两同 token 迟到 destination 不变、两次双 Reset 恢复和一次历史继续共六输出与独立参考逐位一致。[最终检查](../artifacts/research/frame-submission-v1/fault-checks-v2.json)。
- 检查器不再把预算起点硬编码为 post-submit；它验证日志状态、输出及 Reset，不证明时钟起点/实际时长。最初 fault-checks.json 保留为旧检查器产物，以本报告和 fault-checks-v2.json 为准。
- 本轮实际模型求值总计 **NR17 / SR16 = 33 Evaluate**。两次 transport 硬件测试、构建和检查器重新检查均为 0 次模型求值。

## 边界与下一步

已证明：同一即时上下文内，CopyResource 后排队的源写入不会改变自有共享输入中的旧帧快照。源对象随后可复用；自有共享资源仍需等待模型 fence 退休后复用。

未证明：多线程/其他上下文的无序写入、所有引擎资源池行为、真实游戏帧预算、完整 guide 语义、device-loss/resize/session 重建、安全卸载或模型输出游戏回接。持有 ComPtr 本身仍不能冻结纹理内容。

后续共用整链截止时间与统一历史策略现已验证，见[整链调度报告](AC7-Chain-Schedule-Validation.md)；仍未完成生产非阻塞驱动与会话安全退出。以下为本报告当时的计划。

下一步把绝对整帧截止时间与丢帧/双 Reset 策略贯穿 NR→HDR恢复→SR 调度，随后补会话切换、resize 与安全退出；设备失效需独立处理。当前诊断 `finish` 中的等待与 ExitProcess 仍只属于独立工具，不能进入游戏。

本轮无游戏或 AC7 派生重放启动，无插件部署或游戏/NVIDIA 配置写入，无提交。继续保留 P=1 未校准、真实 jitter0、未隔离重放禁令；不需用户补采或换插件。
