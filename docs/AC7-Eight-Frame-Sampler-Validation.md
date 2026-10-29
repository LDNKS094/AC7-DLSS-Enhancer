# 固定8帧、两槽有界采样验证

## 当前结果

**后续真实8帧已成功：** 用户关闭NVIDIA自动优化，58000-130125640采样40资源完整、698→705真实动态基本资格通过，allocation恢复5120×1440、7段时钟/矩阵链全部对应。不需再补采；转独立离线8帧模型分析。见[动态八帧资格](AC7-Dynamic-Eight-Frame-Validation.md)。下方首次失败调查保留。

**首次真实八帧失败已定位为外部配置覆盖：** NVIDIA App将主会话新路径的离线Ace7Game.exe重放识别成新游戏，自动把共享配置从无边框/100改成全屏/200，真实采样allocation10240×2880导致两槽/磁盘预算拒绝。直接日志与前后状态已保全；禁止未隔离的进一步AC7重放，不自动代改NVIDIA全局偏好。当前退出后的配置已100/无边框，addon不需更换；需正常启动确认100后再采一次。见[显示设置事件调查](AC7-Eight-Frame-Display-Reset-Incident.md)。先前“无游戏文件改动”应限定直接写入，不能排除已证实的NVIDIA间接改写。

v4已支持一次进程固定2或8个真实Present区间，保留v3实际shader/guide配对、请求门禁、曝光与尺寸/格式/device检查。**硬件合成8帧和AC7捕获重放8份传输已通过，可通知用户进行一次真实8帧补采样。** 当前不运行游戏内NR/SR，采样器不含模型调用或渲染修改；真实8帧仍未取得。

升级包`artifacts/packages/ac7-eight-frame-diagnostic-v4/`仅包含新版addon、Request-Eight-Frames.cmd、说明和SHA256 manifest。现有游戏目录插件经哈希确认仍v3，本轮未自动替换。具体操作见[八帧包说明](../tools/capture-addon/EIGHT-FRAME-README.md)。

## 最小实现与边界

继续复用[TwoFrameSampler](../src/d3d11/two_frame_sampler.h)（保留原类名及默认2帧API，避免无关调用改动），构造参数只接受2/8。实现位于[two_frame_sampler.cpp](../src/d3d11/two_frame_sampler.cpp)。没有将8份全量staging同时保留：

1. 每区间在prewarp复制scene/depth/velocity/View，同区间tonemap复制曝光。
2. Present在该槽全部复制后End独立D3D11 event query，再推进已采帧数。
3. 在Present和下一次scene开始时，按顺序用DONOTFLUSH查询；未完成立即返回，不循环等待GPU。完成后DO_NOT_WAIT Map、按RowPitch写盘并关闭文件，释放该槽的staging/query。
4. 固定只有两个槽，第三个区间若仍无法释放旧槽便失败，不覆盖、排队无限增长或跳帧。达到8帧立即停止复制，排空最后槽后写成功manifest；无成功时INCOMPLETE与failure保留。
5. 输出`frame0`至`frame7`五类文件，共40资源；元数据和trace仍单独保留。成功manifest包括frames=8、total logicalBytes、peakInFlightLogicalBytes、maxInFlightSlots=2及writeMilliseconds，temporalQualified固定false。

资源预算：

- 最大逻辑在途数据256MiB。每个scene预检两个完整槽大小，再逐资源累计；map直接逐行写盘，不创建额外整帧CPU副本。驱动行对齐、对象/命令与原游戏/ReShade内存不计入该逻辑数据界限，不能称整个进程实际工作集≤256MiB。
- 总磁盘1GiB，计入raw头及1MiB元数据预留；检查磁盘可用空间并额外保留64MiB余量。当前每次scene保守地检查足够容纳整个计划的数据量，不仅检查当前帧，低空间可能提前拒绝。
- 当前5120×1440总逻辑943,751,232字节≈900MiB；不是无限录制。推荐磁盘空余至少1.1GiB。
- 最多120次Present回调/10秒观察期限仍保留。没有后续回调时不能实现强制墙钟定时终止，但不会继续分配/采入。
- **写盘是同步的，会影响帧时间。** 记录实际game/real时间，不伪装成固定16.67ms或性能测量。用户视角/模拟必须正常推进；后台暂停状态仍不合格。
- 请求门禁与每进程一次保持；新`AC7-Capture8.request`对应独立5秒延迟命令，与两帧marker冲突时拒绝。启动前旧marker仍归档不arm。2/8两种命令都检查另一种marker，八帧不通过参数任意录制数量。

## 检查与证据

单只读Luna检查原测试/约束清单，workflow`19429db0-e32c-4bfe-940b-6c62a1a73d6c`、child`cd363ff3-1a11-426e-8fd7-c2fbae18e87e`；[报告保留](../artifacts/research/eight-frame-sampler/subagent-checklist.md)。主会话实现、运行和核验，未由child修改或执行GPU。

[validate_two_frame_sampler.cpp](../tests/d3d11/validate_two_frame_sampler.cpp)追加`--eight`/`--eight-gap`，使用不同frame-specific输入验证槽循环，保留原overwrite/missing/duplicate/rect/deferred/device/padding/budget、guide配对/歧义/上限与stale/new request检查。

| 检查 | 结果 |
| --- | --- |
| 默认2帧回归 | 全10份资源有效分量精确，原状态/拒绝/debug0 |
| 合成8帧 | 40份资源各自匹配源字节；depth32+stencil8有效分量匹配，不比较X24填充；无frame8，GPUdebug0 |
| 合成计数断档 | 所有实际快照保留，candidateCounterStep1=false、temporalQualified=false，不把缺计数伪装连续 |
| 中途缺场景/曝光 | failed、INCOMPLETE/failure保留、无capture.json，不跳过补帧 |
| 写盘失败 | 用同名目录阻止frame0 raw写入，错误为write failed，保留INCOMPLETE、无成功manifest |
| 八帧磁盘预算 | 4096×2047在两槽GPU逻辑预算内、但8帧超过磁盘含reserve限制，staging分配前拒绝 |
| AC7派生重放（12次上限） | v4读取fresh8请求，固定采8份后自动完成，24个候选事件 |
| 重放资源匹配 | 32纹理raw对原检查点逐位、8个View对原blob123逐位；原15检查点（含final/UI）逐位不变 |
| 重放峰值 | 逻辑staging117,968,904字节≈112.5MiB，最多2槽；总写盘约409.223ms，仅该次离线实测 |
| 重放相邻判断 | 8个计数均995，counter/time=false；独立audit正确判非相邻，不冒充真实8帧 |
| 延迟触发 | 独立目录约5238ms后生成8请求；已有8请求/已有2请求均拒绝，不覆盖 |

证据`artifacts/research/eight-frame-sampler/`：[硬件8帧](../artifacts/research/eight-frame-sampler/eight-v2.log)、[断档控制](../artifacts/research/eight-frame-sampler/eight-gap-v1.log)、[重放校验](../artifacts/research/eight-frame-sampler/replay-checks.json)、[重放序列审计](../artifacts/research/eight-frame-sampler/replay-sequence-audit.json)、[触发校验](../artifacts/research/eight-frame-sampler/trigger-checks.json)。原输出在ignored `build/sampler-addon-v4-eight-replay/`。

非完成GPU时两槽满分支通过静态检查与顺序/峰值/独立快照测试覆盖，没有确定性模拟驱动长期不返回query的故障；实机若出现“Two staging slots still busy”会安全失败，不能为了通过改成跳帧或去掉内存边界。也未人为填满游戏磁盘或对原游戏制造device lost。

## 实际补测与后续

现在需要的是**一次正常飞行的真实8帧**：退出游戏→备份/替换addon→复制八帧cmd→移走2/8遗留请求→重启飞行→运行八帧cmd立刻切回游戏→继续约10秒→退出并提供新`AC7-eight-frame-*`路径。不要运行旧两帧cmd，不使用Nsight/F11/Next Frame；不同时部署其他addon或改显示设置。

拿到序列后运行新[check-live-sequence.cjs](../tools/nsight/check-live-sequence.cjs)：40资源头/长度/hash、View/metadata一致、8个counter/7段game-real链/六矩阵链、实际jitter/曝光与trace顺序。传输成功与动态输入资格分开；任意断档或模拟保持如实报告，不对数据补帧。然后把已通过的NR→HDR→SR离线执行扩到8帧，检查长期趋势；仍不由8帧自动验收全动态画质。

本轮无新游戏/Nsight/NGX启动、无模型求值、无游戏文件改动/提交。游戏内模型实时桥、同步、回接、FG/超分/jitter均未因采样器升级而完成。
