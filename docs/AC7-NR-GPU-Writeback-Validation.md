# 同一GPU代理的NR求值、HDR恢复与AC7非中性离线回接

> 后续[SR同尺寸离线回接](AC7-SR-Writeback-Validation.md)也已完成：复用本页同一NR结果，再接匹配SR输出及原FXAA/后处理/UI，最终帧改变且UI逐位；原FXAA暂保留待旁路对照。P1暂用，预曝光研究延后；仍是文件staging、非实时或连续时域验收。

## 当前结果

已将**真实D3D11 GPU生成的SDR代理 → 正式NR310.9.16.0的D3D12求值 → D3D11 GPU恢复HDR → 原AC7后处理/UI合成 → 最终帧**按同一输入配对验证通过。不是复用CPU代理对应的模型结果。

所有跨阶段数据仍通过raw文件/CPU staging传递；**没有实时D3D11↔D3D12资源桥，也不是同进程连续GPU管线**。本轮只证明该捕获单个Reset帧的局部输入、执行和原后处理回接。SR/FG、最终Pre-SR点和时域尚未验收。

## 同一输入的身份与拒绝检查

本次NR输入直接复制上一轮真实GPU prepared.raw，其SHA256：

`b304e2954d4c8a8be0ad34d112d2ffd2a2ea2d7608e5487c918926f7357cefdf`。

同一帧CPU代理的hash不同，因此新执行了一次正式NR，不把旧输出冒充本次结果。深度/速度仍是同一正常捕获的规范化引导；尺寸2560×1440，Reset=1，其他参数沿用已验证配置，运行时SHA256复核一致。

派生捕获在Draw467后重新生成GPU代理，读回并与**本次实际NR输入文件全部字节比较**，一致才允许把NR结果恢复/写回场景。故本轮既验证模型消费GPU代理，也验证捕获现场能重现该代理。

负向测试故意指定旧CPU代理为expected input：重放退出1，明确报`Replay GPU proxy differs from actual NR input; refusing writeback`，没有产生写回后的场景dump。这是预期拒绝，不是设备故障；检查阶段在场景CopySubresourceRegion之前。

[GPU代理输入来源](../artifacts/research/official-nr/frame-20261028-gpu-proxy/input.json)、[错误代理拒绝记录](../artifacts/research/official-nr/hdr-proxy-mismatch-refusal/checks.json)。此核验保护离线实验配对，不替代实时帧身份/资源寿命管理。

## 模型与GPU结果

- NR Init、Create、Evaluate、GPU fence完成、释放/关闭全部成功；D3D12 debug error=0。
- 模型颜色、深度、速度上传/读回逐位一致；11,059,200个输出RGB通道全部有限、没有NaN哨兵，输出非恒等。
- 真正GPU代理对应的NR输出，再由既有HdrProxy读取同一GPU prepared image做差分恢复，避免CPU/GPU代理码位差被当成模型修改。
- 原HDR场景仅中央有效rect写回，原游戏后处理与UI绘制源码不修改。

[NR原始调用](../artifacts/research/official-nr/evaluate-20261028-gpu-proxy.json)、[NR读回检查](../artifacts/research/official-nr/evaluate-20261028-gpu-proxy/checks.json)。本轮只运行NR一次；后面的重放反复使用这个对应的输出，不是重新运行模型或伪造连续帧。

## 回接与后处理

同一个派生EXE分别以中性模式、匹配NR输出模式重放，均1帧退出0。新manifest增加原后处理及UI资源读回。

| 检查 | 结果 |
| --- | --- |
| 中性模式：9项场景/曝光/后处理/最终raw | 与原始捕获基线全部逐位一致 |
| NR模式：写回HDR变化像素 | 3,605,343 / 3,686,400有效场景像素 |
| 写回越出有效rect | **0** |
| 非有限HDR通道 | **0** |
| HDR最小/最大值 | 0.001220703125 / **166** |
| 完全高光保护像素 / 其中变化像素 | **20,566 / 0** |
| 最大绝对HDR变化 | 1.03125（不是ground-truth误差） |
| 最终帧变化像素 | 3,571,859，全部在有效rect内 |
| 最终帧相对基线平均RGB变化 | 全5120×1440分配平均约1.0348/255；不是画质评分 |
| 合成后最后一draw修改像素 | 0 |

原场景后续的tonemap、AA后效果、UI合成和最终帧均有对应变化，说明修改确实经过原后处理进入最终图像，没有在中途被覆盖。

[完整HDR/图像/曝光检查](../artifacts/research/official-nr/hdr-gpu-proxy-active-ui2-replay/writeback-checks.json)。中央有效rect仍为2560×1440，这不是最终补丁启用后的全幅超宽屏资格。

## 曝光反馈：当前算术已解释，动态仍未知

修改后原游戏的80×23亮度统计发生变化，说明回写进入原曝光测量链。使用捕获实际CB blob744复算：

| 数值 | 中性基线 | NR回写 |
| --- | --- | --- |
| 加权亮度编码均值 | 0.5550466825 | 0.5540672752 |
| 限幅前目标亮度 | 2.4698375967 | 2.4497988468 |
| 受实际范围[0.5,1]限制后的目标 | **1** | **1** |
| GPU实际曝光R增益 | 1.0000050067901611 | 同值 |
| CPU复算增益与GPU误差 | 8.60e-8 | 8.60e-8 |

所以本帧曝光不变有明确算术原因，不能说“NR没有进入曝光链”或“动态曝光永远不受影响”。

准备NR代理的gain仍由**离线已知的原始捕获文件**显式提供，不是证明在这个插入点已取得正确实时曝光；本帧新旧gain相同不消除未来的反馈/时序问题。真实亮暗切换和闭环曝光稳定性仍需相邻帧验证，不复制单帧充当历史。

## UI资源与预览

Draw545合成读取的3张资源在中性/NR回写两个重放中逐位一致：

- Texture219：5120×1440 RGBA8 typeless，HUD/UI图层；
- Texture849：1280×360，同格式的合成辅助输入；
- Texture850：640×180，同格式的合成辅助输入。

这些是该捕获具体资源的证据，不自动等于通用HUD hook已实现。最终合成像素会随背景变化，因此不要求最终HUD覆盖区域RGB和基线一模一样；资源本身一致比最终图的粗略compositor mask更有鉴别力。

第一次新增UI读回在中性重放退出1，因为旧读回工具只允许BGRA8 typeless，尚未允许UI的RGBA8 typeless。只在共享Dump格式白名单补了RGBA8这一项，仍按4bytes/pixel读回，保留未知格式拒绝；失败日志位于`artifacts/research/official-nr/hdr-gpu-proxy-neutral-ui-replay/replay.log`。没有改游戏/SDK或系统工具。随后使用新输出目录重跑中性和NR读回，均成功；此前未新增UI的两次重放输出也保留。不同重放不混为同一次。

[UI逐位检查](../artifacts/research/official-nr/hdr-gpu-proxy-active-ui2-replay/ui-resource-checks.json)、[最终画面对照](../artifacts/research/official-nr/hdr-gpu-proxy-active-ui2-replay/comparison.png)、[NR回写后的最终帧有效区域](../artifacts/research/official-nr/hdr-gpu-proxy-active-ui2-replay/nr-final.png)。

预览直接转换原游戏最终R10码值并裁有效rect，没有加本项目的显示tone curve。助手检查了飞机、海岸、云和绿色HUD，没有明显黑屏、整体错位或文字丢失；局部细节/明暗有变化。单帧外观检查不是艺术/画质验收，暗部增亮与时域伪影仍需检查。

## 改动与复现

- [replay_hdr.cpp](../tests/d3d11/replay_hdr.cpp)：中性默认不变，新增显式预计算NR输出和expected代理；原尺寸/格式/设备验证保持。
- [prepare-hdr-replay.cjs](../tools/nsight/prepare-hdr-replay.cjs)与[新manifest](../tools/nsight/checkpoints-hdr-writeback-20261028.json)：仅派生源码增加9检查点及3 UI读回，不修改原capture。
- [check-hdr-writeback.cjs](../tools/nsight/check-hdr-writeback.cjs)：中性逐位、HDR高光/范围、有效rect、后处理/最终变化、UI资源及实际曝光CB复算。
- [replay-checkpoints.cpp](../tools/nsight/replay-checkpoints.cpp)：只新增RGBA8 typeless读回白名单。

重放沿用原capture cwd，`AC7_REPLAY_DUMP_DIR`和`AC7_HDR_EXPOSURE`显式设置。非中性模式额外指定`AC7_HDR_NEURAL=<nr-output.raw>`及`AC7_HDR_EXPECTED_PROXY=<实际NR输入color.raw>`；没有后者拒绝加载模型输出。模型执行仍使用原有`preflight-nr d3d12-evaluate`，没有模型/ABI修改。

## 接下来的门槛

**这条单帧离线NR回接已成立，不再把GPU代理/真实NR配对回接列为未做。** 后续SR合同/创建及[显式独立域Reset图像诊断](AC7-SR-Reset-Diagnostic.md)已完成；P=1仅为诊断假设，游戏真实pre-exposure/画质/时域未验收，SR输出未回接。当前先解释SR高光/重建变化并定位真实曝光/view语义，再推进原生回接和高输出。Draw467仍只是已验证离线检查点，不是通用hook或最终位置。

时域、动态曝光、完整颜色primaries/pre-exposure、补丁全幅/周边压缩、SR低输入高输出、FG资源/呈现/节奏与性能仍需独立验证。若必须修改SDK/NGX/模型或重写UE4调色工具链则停止该路线；本轮没有做这些改动，也未部署游戏或Git提交。正式NGX初始化仍有此前已知的模型缓存维护/官方更新器副作用。
