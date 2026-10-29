# AC7 独立整链等待预算、迟到退休与历史恢复验证

本阶段在独立工具完成真实 NR/SR 的 post-submit 等待预算故障注入；不是整帧实时性能验收，也未接游戏。

`SharedTextureBridge::abandon()` 标记当前事务永不发布，保留在途状态直到 GPU fence 完成。此后即使用原 frame token 调用 poll，也只退休并返回 discarded，不等待/复制到 D3D11 destination。新 upload 清除此标记，busy 时仍拒绝复用。

`gpu-chain` 内 Model 改为 begin/poll/finish；begin 在 allocator Reset 前检查 busy，提交期间持有四个输入 ComPtr，终态才释放；poll 检查提交后等待预算，到期 abandon。finish 只是独立验证的退休 watchdog，超时 ExitProcess 保护不能进入游戏代码。非故障模式仍以原参数、实际 dt 和 Reset1→0 执行。

运行 `gpu-chain NR_RUNTIME SR_RUNTIME AUDITED_LIVE8 FILE_BASELINE NEW_OUTPUT --deadline-test` 使用正式 310.9.16.0 runtime 与原真实八帧。额外 D3D12 queue gate 确定性阻塞真实模型，等待预算 2ms：

| 样本序号 | 行为 | NR/SR Reset |
|---|---|---|
| 0 | 正常发布，原七阶段基线逐位 | 1/1 |
| 1 | NR 在途超时，保原图 | NR0，SR不求值 |
| 2 | 上一事务仍 busy；原图返回，不提交新模型；随后释放 gate、用旧 token 退休 | 不求值 |
| 3 | 两特性恢复，与新建参考特性逐位 | 1/1 |
| 4 | 继续历史，与对应参考序列逐位 | 0/0 |
| 5 | NR 已推进，SR 在途超时，保原图 | 0/0 |
| 6 | busy 原图回退；随后旧 SR 同 token 退休 | 不求值 |
| 7 | 两特性恢复，与另一组新建参考特性逐位 | 1/1 |

四份 fallback-scene 与对应完整原始场景 raw 逐位；两个迟到 destination 在退休前后逐位且原 scene 退休后不变。过期时四输入仍持有，busy begin 拒绝 allocator/resource 复用，退休才释放。三个恢复/继续帧各 NR/SR 共六份输出与独立参考逐位；所有发布 RGB/alpha 有限，SR 保持 HDR。主特性与共享纹理身份持续，参考特性只在恢复点重建。两 API debug0，全部 feature/parameter 释放及 NGX shutdown 成功。

证据目录：[deadline-chain-eight](../artifacts/research/deadline-chain-eight)。`fault-v1` 和修正 after 快照保存为实际读回后的 `fault-v2` 均通过；最终验收为 [fault-v2-checks.json](../artifacts/research/deadline-chain-eight/fault-v2-checks.json)。检查器 [check_deadline_chain.cjs](../tools/nr/check_deadline_chain.cjs) 验证日志、求值次数、Reset/身份、原图与迟到前后 raw、参考输出、finite/HDR 和清理；将 frame7 Reset 日志改0的负对照被拒绝且未产生验收 JSON。覆盖已有输出在 Init 前拒绝。

无故障八帧回归全部56阶段与保全文件链逐位：[normal-regression-checks.json](../artifacts/research/deadline-chain-eight/normal-regression-checks.json)。共享桥独立硬件回归32输入/8输出逐位、pending/stale 原图/debug0，新增无在途 abandon 前后拒绝通过。只读 Luna scout 未发现该限定路径的 phase/lifetime/reset/fallback 缺陷，父会话用实际硬件/模型证据验收，报告保留于证据目录。

实际求值计数：fault-v1 NR9/SR8、正常回归 NR8/SR8、fault-v2 NR9/SR8，总 **NR26/SR24 = 50 Evaluate**。每份故障集含主 NR6/SR5 与参考 NR3/SR3。检查器负对照、覆盖拒绝和 transport regression 无模型求值。

限制：预算从 upload+submit 返回后开始，不含转换、录制、NGX CPU 调用或诊断 Map/文件时间；现有 default 10秒用于独立诊断，不是可接受帧预算。故障驱动按固定样本序号注入，resetNext 由测试调度置位，尚非通用运行时调度器。fixture 每帧原场景是稳定的独立对象，ComPtr 只保证对象生存，不保证引擎复用纹理的内容不被覆盖。busy 样本先保原图再用 watchdog 同步退休，不证明游戏回调不阻塞。没有游戏 scene 写回、host/proxy-native identity、resize/device-loss/unload/session 重建、超分/FG/长期画质/性能验收；P1 暂用、实际 jitter0 保持。

下一步：抽离不含诊断读回/文件的有界调度入口，验证真正整帧截止时间、输入内容与资源退休、resize/新 session/设备失效及安全卸载，再选择 host 并接原 scene/warp/post/UI。不需用户补采或换插件。本轮未启动游戏或任何 AC7 派生重放，未写游戏/NVIDIA 配置、未部署或提交；未隔离重放禁令与 standalone ExitProcess 不得进游戏的限制继续有效。
