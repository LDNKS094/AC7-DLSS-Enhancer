# 整链共用截止时间与历史策略验证

独立工具已将输入准备、NR、HDR 恢复和 SR 放在同一个绝对截止时间下，并由统一状态策略决定丢帧与双 Reset。真实八帧故障序列与正常整链回归通过；尚未游戏接入或性能验收。

## 本轮实现

- [ChainSchedule](../src/d3d11/chain_schedule.h)：纯状态策略，包含 prepare → NR 在途 → HDR restore → SR 在途 → idle。它不持有 GPU 资源，不等待、不读图、不写文件。每帧只接受一个 deadline；新帧在忙时到达会使旧帧不可用，并锁存下一帧双 Reset。NR/SR 在途时禁止直接 drop，必须先完成资源退休。
- [gpu_chain.cpp](../tools/nr/gpu_chain.cpp) 新增 `--scheduled-test` 与 `--self-test-schedule`。Model 复用同一录制代码，新增 `beginUntil`，在录制前检查绝对时间，再交给既有 FrameSubmission 在上传前检查一次；两特性均接收 `schedule.deadline()`，不各自重新计时。
- 预算从本帧原生 GPU 输入准备就绪后、guide/proxy 转换之前开始，覆盖转换录制、NGX CPU 录制、上传、提交、轮询、HDR 恢复与 SR，直到私有输出的发布决定。输入文件读取、fixture 纹理创建、设备/特性初始化在窗口前；阶段图像读回、文件比较/保存、参考特性执行在该帧终止后。
- 状态策略给 NR/SR 同一 Reset 值：首帧、token 间断、过期、未成功发布或忙帧拒绝后均重置；只有完整成功且连续时才继续历史。

## 固定八帧验证

使用原真实动态样本 698–705；下表是工具内序号，不是新的游戏采样。

| 序号 | 故障或行为 | NR/SR 行为 |
|---|---|---|
| 0 | 正常发布 | 两特性 Reset1，七阶段与原基线逐位 |
| 1 | 输入准备前已过期 | 两模型均不调用，保完整原图 |
| 2 | 恢复 | 两特性 Reset1，与独立新特性逐位 |
| 3 | 连续成功 | 两特性 Reset0，与参考的相同历史逐位 |
| 4 | NR 已完成，HDR 恢复后故意耗尽时间 | NR0；不再提交 SR，保原图 |
| 5 | 恢复 | 两特性 Reset1，与另一组新特性逐位 |
| 6 | SR 被 gate 阻塞直至整帧截止时间 | NR0/SR0，保原图；资源仍在途 |
| 7 | 旧工作尚未退休，新帧到达 | 不调用新模型，保原图；随后旧 SR 同 token 退休丢弃 |

四份完整原图 raw 与捕获逐位一致；第6帧 SR destination 退休前后逐位不变，原 scene 退休后仍精确。三组 NR/SR 恢复或继续历史共六输出与独立参考逐位一致。主 feature 与共享资源身份持续；所有发布输出 RGB/alpha 有限、SR 保持 HDR；两 API debug0，全部特性/参数释放和 shutdown 成功。

本轮最后一个在途 SR 丢弃后没有额外真实恢复帧；其下一次双 Reset 要求由纯策略测试覆盖。此前真实 SR 丢弃后的恢复证据见 [等待预算验证](AC7-Deadline-Retirement-Validation.md)，不能把本轮第7帧称为恢复求值。

## 截止时间与测试边界

正常测试预算为5秒；两个人工耗尽场景为1秒；输入前拒绝场景为0秒。这些是确定性测试边界，**不是目标帧预算**。日志记录原始 steady_clock 起点、deadline、结束点及 clock period；检查器核对两个模型使用同一个绝对值，且发布决定发生在期限内。

工具的 retire 仍有1ms CPU轮询与独立超时退出保护，NGX/调试层/stdout 开销也在窗口内；没有实现生产环境的非阻塞回调驱动，更没有包含游戏输入捕获、原场景写回或 Present 的耗时。输出时间只属于这次诊断墙钟，不能当 GPU 时间、游戏帧率或实时性能验收。独立 ExitProcess 保护不得进入游戏。

## 检查与证据

- [最终结果](../artifacts/research/chain-schedule-v1/checks.json)、[最终模型日志](../artifacts/research/chain-schedule-v1/run-accepted-process.log)。最初 `run-v1` 保留；最终 `run-accepted` 补充明确时钟周期日志。
- [check_chain_schedule.cjs](../tools/nr/check_chain_schedule.cjs) 检查事件顺序、共享 deadline、Reset、主特性/资源身份、模型次数、恢复参考、完整原图、迟到 destination、finite/HDR 与清理；将 SR deadline 改1个tick或移除恢复 Reset 的两种日志负对照均拒绝，不生成验收 JSON。
- `--self-test-schedule` 不运行 GPU/NGX，覆盖 deadline 等号、忙帧使旧帧失效、禁止在途 drop、NR/SR 失败后的 Reset、跳 token、重复 token 与非法阶段。
- 无故障八帧回归：[normal-checks.json](../artifacts/research/chain-schedule-v1/normal-checks.json)，全部56阶段与保全文件链逐位、NR8/SR8、debug0/正常清理。
- 实际求值：首次 scheduled NR9/SR8，最终 scheduled NR9/SR8（每组均含参考3/3），正常回归 NR8/SR8；合计 **NR26/SR24 = 50 Evaluate**。纯状态测试和检查器负对照0次求值。

## 下一步

后续固定尺寸单步驱动、stop/drain与源快照现已验证，见[单步驱动报告](AC7-Nonblocking-Chain-Validation.md)；实际resize/设备失效/游戏卸载仍待处理。以下保留本报告当时的后续计划。

共用截止时间和统一历史策略的独立验证已完成。接下来实现单次调用即可推进的非阻塞整链驱动，并处理会话更换、resize、停止接收新帧后的资源退休和安全退出；设备失效需单独处理。已有同即时上下文的源覆盖证据可复用，不外推多上下文无序写入。

尚未选择最终宿主、写游戏 scene 或接原 warp/post/UI，超分/FG 与长期画质未验收。P=1 未校准但延后非阻塞，实际 jitter0 保持。本轮无游戏/AC7派生重放启动，无配置写入、插件部署或提交；无需用户补采或换插件。
