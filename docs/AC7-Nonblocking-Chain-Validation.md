# 单步整链驱动、停止与资源退休验证

固定尺寸独立原型已经可以通过 `start / advance / stop / drained` 推进真实 NR→HDR恢复→SR：单次 advance 只提交一个阶段或轮询一次 GPU 状态，没有内部等待循环。已验证源覆盖、NR/SR在途停止、驱动重建后的双 Reset，以及超过32位的帧 token。未实现真实 resize、设备丢失恢复或游戏卸载。

## 实现与调用合同

代码位于 [gpu_chain.cpp](../tools/nr/gpu_chain.cpp) 的 `NonblockingChain`，通过 `--driver-test` 验证，仍属独立工具原型：

- `start` 验证固定纹理布局/设备/矩阵/曝光/时间，沿用 `ChainSchedule` 的绝对截止时间与历史策略。原场景与曝光复制到自有 GPU 纹理，guide/proxy 在同一即时上下文中先行录制。调用方随后有序覆盖源纹理时，稍后的 HDR 恢复仍使用本帧场景快照。
- `advance` 每次处理 prepare、NR poll、HDR恢复与SR提交、SR poll 中的一项，未完成返回 pending。正常推进路径不调用旧的 `retire / finish` 等待函数，不进行 Map/文件操作。
- `stop` 幂等：拒绝新输入、隐藏输出、放弃在途结果，但不释放资源。NR/SR完成后仍须继续 advance，使状态退休。
- `drained` 只有在已停止、策略idle、两模型bridge均idle，以及自有D3D11退休fence完成时才为true。该fence排在准备、共享返回复制等D3D11工作之后；不能只凭D3D12模型fence完成就销毁快照。
- 调用方必须串行使用同一D3D11即时上下文，并在停止/退休完成前保持驱动、共享pass与模型存活。驱动活动期间独占normalizer/codec/converter/model；空闲后的参考对照按同上下文顺序执行。此合同尚未通过生产所有权类型强制执行。
- 模型入口token统一改为 `uint64_t`。测试从 2^32 开始；重建驱动继续使用全局单调token，未允许将捕获计数直接重新从0用作旧bridge token。

自有快照的逻辑负载为原场景5120×1440×4加1×1 R32曝光，即29,491,204字节（约28.125MiB），不含驱动分配开销、共享桥、模型或诊断资源；不是整链显存测量。

## 真实模型验证

使用同一保全动态八帧，表中为工具样本序号：

| 序号 | 行为 | 结果 |
|---|---|---|
| 0 | 正常调用后覆盖四张源纹理 | 七阶段仍与原基线逐位 |
| 1 | NR被gate阻塞，调用stop两次 | 连续16次advance返回pending，drained为false，输出不可见，保原图 |
| 2 | 已停止时新帧到达 | 拒绝输入保原图；随后释放gate、旧NR丢弃，D3D11完成后销毁驱动 |
| 3 | 新驱动复用旧NR/SR特性 | 双Reset；源覆盖后的两模型结果与原始输入重建的新参考逐位 |
| 4 | 连续历史 | 双Reset0；源覆盖后仍与参考相同历史逐位 |
| 5 | 不支持的尺寸输入 | 工作提交前拒绝，保原图；停止/退休后重建相同固定尺寸驱动 |
| 6 | 第三驱动实例恢复 | 双Reset；源覆盖后与另一组新参考逐位 |
| 7 | SR被gate阻塞后stop | 连续16次pending；释放gate后迟到SR丢弃，D3D11/模型均退休才销毁 |

四份完整原图回退逐位；两个停止场景的返回destination在退休前后逐位不变；三组恢复/历史共六份模型结果与独立重建参考逐位。主特性与共享纹理身份持续。四次源覆盖都读回确认实际改变，含depth32与stencil8有效字节；仅跳过未定义X24填充。发布输出全部有限、SR保HDR，两APIdebug0，特性/参数与NGX清理成功。

驱动受阻时返回pending证明其没有等待该gate完成；不证明每个NGX/driver调用都有硬实时上界。代码仍调用现有独立Model，其错误保护包含ExitProcess，尚不能作为游戏运行时直接部署。测试宿主的回调循环、1ms sleep、readback、日志、参考求值和失败保护均保留在驱动外。

## 失败与回归

- 首次 `run-v1` 在第一帧发布后，源覆盖测试因对原生depth未定义X24字节要求等于0x5a而失败。独立保护退出保留INCOMPLETE，已求值NR1/SR1，无正常清理；不得将该目录当成功证据。初始错误输出被保护遮住，随后补充异常日志。
- `run-v2` 只核有效depth/stencil字节；每个覆盖帧记录有22,118,400个padding字节与0x5a不同，95846404个有效输入字节覆盖检查全部通过。未改源数据、模型或有效分量比较。
- 最终 [checks.json](../artifacts/research/nonblocking-chain-v1/checks.json) 与 [过程日志](../artifacts/research/nonblocking-chain-v1/run-v2-process.log) 保留。
- [check_nonblocking_chain.cjs](../tools/nr/check_nonblocking_chain.cjs) 核对64位token、模型/资源identity、Reset、事件、停止/退休标志、原图和destination、原输入参考、finite/HDR及清理；篡改pending时的drained为true、移除新驱动Reset的两项负向日志均拒绝，不生成验收JSON。
- 无故障八帧回归 [normal-checks.json](../artifacts/research/nonblocking-chain-v1/normal-checks.json)：56阶段全部逐位、NR8/SR8、debug0/cleanup。纯策略self-test继续通过。
- 只读Luna限定审读未发现支持路径内具体生命周期/停止缺陷，指出实际resize/device-loss与调用方所有权仍未实现。其报告保留于[driver-lifetime-audit.md](../artifacts/research/nonblocking-chain-v1/driver-lifetime-audit.md)，父会话以源代码和实际GPU结果验收。
- 本轮实际求值：失败run-v1 NR1/SR1；成功run-v2 NR9/SR8（主6/5、参考3/3）；normal NR8/SR8，合计 **NR18/SR17 = 35 Evaluate**。负日志与纯策略检查0求值。

## 当前边界与下一步

本轮是三个**驱动实例**重建，NR/SR特性与NGX会话保持，不是设备/模型重初始化。尺寸测试只证明不匹配布局拒绝与安全停止；尚无真正新尺寸模型、guide、proxy、bridge重建。设备丢失、真实引擎多线程资源、游戏原scene写回/warp/post/UI、最终host与卸载均未验证。P1未校准但延后，实际jitter0，不作性能/画质验收。

后续已补充调用方处理错误的停用/退休，见[错误停用报告](AC7-Chain-Error-Stop-Validation.md)；统一所有权、实际API失败和设备清理仍未完成。以下为本报告当时的后续计划。

下一步先把固定布局原型的资源所有权与错误路径整理为可安全停用的会话边界，明确无法继续轮询的设备失效处理；再验证实际尺寸变化的有界重建，而不是把当前拒绝视为resize已完成。不需补采或换插件。

本轮无游戏或AC7派生重放启动、无游戏/NVIDIA配置写入、无插件部署或提交。未隔离重放禁令与独立ExitProcess不能进入游戏的要求继续保持。
