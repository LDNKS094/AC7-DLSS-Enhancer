# 整链错误停用与退休保护

固定尺寸独立驱动新增显式错误停用：保留第一条错误原因、隐藏输出、拒绝新帧；健康的已提交工作继续退休，不再因为绝对截止时间提交入口抛异常就直接退出进程。真正的设备失效清理与资源所有权重组尚未完成。

## 修改

[Model / NonblockingChain](../tools/nr/gpu_chain.cpp)：

- `Model::beginUntil` 移除原 catch/ExitProcess，向驱动传播录制/上传/提交异常。旧同步测试的 begin/retire、初始化与错误析构保护仍属于独立工具，不能进游戏。
- `reportFailure(cause)` 锁存首次错误、停止接收输入、隐藏输出；重复报错不替换首次原因。健康已提交的 bridge 被 abandon，仍保留资源等待终态。
- `advance` 捕获推进异常并转入停用路径；输入准备录制失败与 stop 失败也走该路径。若GPU提交已完成而策略尚未更新，按实际在途bridge协调策略；若结果已退休但后续操作失败，结束该策略阶段，不把它误认为仍可轮询。
- [SharedTextureBridge](../src/d3d11/shared_texture_bridge.h) 增加 `canPoll()`，区分“健康且已提交”与“上传中断/未提交/失效”。无法正常退休的事务置为 `unretirable`，`drained()`保持false、输出不可见、advance返回unretirable；外层需接管设备清理。代码不会凭错误就释放未知在途资源。
- `drained` 在检查设备或D3D11退休fence失败时锁存错误并要求外层清理，不宣称已安全退出。

这些处理仍依赖调用方保留驱动、模型和pass，停止后推进至退休再销毁。**本轮没有把模型/pass所有权改为由会话对象统一持有**。实际设备失效的外层清理也未实现；unretirable是停止正常路径的显式状态，不是已完成恢复。

## 验证

新增 `gpu-chain ... --driver-error-test`，复用原八帧停止/重建序列，在NR在途、空闲及SR在途三处通过公开 `reportFailure` 注入调用方处理错误。每处立即报告第二条不同错误，并断言首次原因未变、输出不可见、新输入被拒、健康工作没有错误进入device-teardown状态。

结果：[error-checks.json](../artifacts/research/chain-error-stop-v1/error-checks.json)、[模型日志](../artifacts/research/chain-error-stop-v1/error-run-process.log)。

- NR/SR各16次gate阻塞的pending推进保持资源在途；错误停用后放开gate，两个destination退休前后逐位一致。
- 四份完整原图回退逐位；驱动重建后的双Reset及历史继续共六输出对独立原始输入参考逐位。三驱动实例保持原模型/纹理身份，64位token不变。
- 所有发布输出有限，SR保HDR，两API debug0；全部feature/parameter释放、NGX shutdown正常。
- 正常八帧回归：[normal-checks.json](../artifacts/research/chain-error-stop-v1/normal-checks.json)，56份阶段raw保持原基线逐位，NR8/SR8。
- 共享桥硬件回归：[transport/checks.json](../artifacts/research/chain-error-stop-v1/transport/checks.json)，32原输入/8返回、16覆盖前快照逐位，deadline/pending/原图检查通过；新增idle不可poll、已提交可poll、退休后不可poll断言通过。
- [检查器](../tools/nr/check_nonblocking_chain.cjs) 的 `--errors` 模式要求三条指定错误事件；把首次原因保持标志改false或把pending时drained改true的两份日志负对照均拒绝，未生成成功JSON。
- 本轮实际求值 **NR17/SR16 = 33 Evaluate**：error-run 9/8（主6/5+参考3/3），normal 8/8。transport和负日志检查0求值。

## 未覆盖与下一步

注入的是调用方处理错误，不是真正NGX失败、GPU提交API失败或设备移除。因此自动catch分支、上传中断协调以及unretirable隔离目前只有实现检查，尚无硬件故障注入验收。未故意移除真实GPU设备，也未运行可能影响游戏配置的AC7派生重放。

下一步用统一会话所有权管理模型/pass/驱动，并在独立环境验证部分初始化/提交失败下的保留与清理边界；随后实现实际尺寸资源重建与设备失效停用。不能将当前错尺寸拒绝、驱动重建或unretirable状态称为模型resize/设备恢复已完成。

本轮未启动游戏、改配置、部署或提交。P1未校准延后、真实jitter0与未隔离重放禁令保持；无需用户补采或换插件。性能、最终host和原scene/warp/post/UI回接仍未验收。
