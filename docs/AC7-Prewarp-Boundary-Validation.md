# NR/SR候选位置前移：在原颜色扭曲之前

> 后续[新prewarp输入的真实NR→SR链](AC7-Prewarp-NR-SR-Validation.md)已完成：各一次新Evaluate、GPU恢复与配对写回，原warp/后处理/UI保留，旧结果错配拒绝。本文“尚未新跑”保留为边界阶段历史；真实时域/高输出/实时仍未验收。

**本轮确认后续模型实验应优先放在Draw465之后、466之前，让原游戏的颜色扭曲在模型之后执行。** 这是同尺寸离线候选边界，尚非实时hook或全引导/时域验收；本轮仅做真实读回和中性HDR往返，没有新增NR/SR求值。

## 为什么要前移

前轮NR/SR在Draw467后取得颜色，已经通过Draw466的屏幕坐标扭曲。新读回证明：

- 原扭曲实际改变有效区88,621个颜色像素，区外0。
- 32位硬件深度值逐位不变，物体速度纹理逐位不变。因此扭曲后的颜色与原投影guide未做相同变换，不能忽略为微小偏差。
- 深度/stencil资源整体并非不变：stencil有227,425个像素发生变化。**深度值与stencil必须分开判断**；不能拿整块深度资源比较失败就断言深度变了，也不能漏报stencil用途。

相比给depth/MV另写扭曲/反扭曲和历史变换，优先把模型放到原扭曲前更窄：NR/SR处理未被这一pass改变坐标的颜色，让游戏原pass保留扭曲和stencil语义。没有因此宣称所有透明、水面、云或更早屏幕偏移都正确。

## 资源与边界

| Draw | 实际作用 |
| --- | --- |
| 460 / PS58593 | Scene210/uid1645在扭曲应用前最后一个已观察到的直接writer |
| 461 / PS58218 | 写BGRA8 distortion/uid55815 |
| 462–465 / PS55818 | 继续写同一distortion，不写Scene210 |
| **465后，466前** | **本轮新候选点**：颜色已结束这组场景写入，原warp尚未应用 |
| 466 / PS50261 | t0=Scene210、t1=distortion；UV加`(t1.xy-t1.zw)*0.25`，边界检查后取色，写临时R11HDR/uid55820 |
| 467 / PS50263 | 把临时HDR复制回Scene210 |

主会话检查原生成源码465→466区间：该区间是绑定状态/CB更新和draw，不包含额外Dispatch/Copy/Clear写入；这只是该局部区间核对，不把draw索引当完整资源hazard解释器。实际Scene460与Scene465读回**整图逐位一致**，进一步确认distortion生产期间未改变场景颜色。

一个只读Luna scout复核draw/资源关系；它没有实际复核shader正文，扭曲公式由主会话既有逐行证据及本轮检查承担。[摘要](../artifacts/research/prewarp-boundary/prewarp-boundary-audit.md)已保留，run`50191839-1d37-4de2-a44d-713ac49c830d`，原托管outputReference记录在阶段JSON。没有把child覆盖范围扩大成完整compute证明。

## 本帧扭曲场范围

从BGRA8 raw按shader实际RGBA通道解释，计算texel中心的未裁剪位移：

```text
deltaUV = (R-B, G-A) / 255 * 0.25
deltaInputPixels = deltaUV * (5120,1440)
```

| 指标 | 本帧数值 |
| --- | --- |
| 有效区非零位移texel | 86,342，约2.342% |
| 工作rect内包围范围（含端点） | x1126–1586，y910–1366 |
| 最大abs X / Y位移 | 25.098 / 21.176输入像素 |
| 最大位移向量长度 | 31.092输入像素 |
| 非零texel平均 / P95长度 | 9.084 / 18.012输入像素 |
| warp后实际颜色变化 | 88,621有效区像素 |

包围范围位于机体/尾流附近，与热扰动一类效果相容；**没有据位置给shader未经验证的语义命名**。统计是纹理texel中心值，原pass还包含双线性采样和UV边界回退，不能声称每个最终像素都恰好偏移上述值，亦未复现整套滤波核。

## 派生重放与中性验证

新增[检查点清单](../tools/nsight/checkpoints-prewarp-20261028.json)、[生成工具](../tools/nsight/prepare-prewarp-replay.cjs)、[检查工具](../tools/nsight/check-prewarp.cjs)。只生成新的`build/prewarp-replay-source`/EXE，原capture不改。

两次单帧重放：baseline只读回；neutral在465后调用已有`HdrProxy`无模型中性Prepare/Restore，并把结果写回中央rect。显式`AC7_PREWARP_NEUTRAL=1`启用；拒绝混入旧NR输出或非法模式。曝光输入仍是明确的捕获增益，P1策略不变，预曝光调查继续延后。

**15项raw全部逐位一致：**

- 460场景、465场景、distortion、465深度/stencil及速度；
- 467场景、深度/stencil及速度；
- 曝光、tonemap后颜色、后处理后颜色、三张UI及最终帧。

同时baseline的postwarp场景与最早正常捕获scene-hdr逐位一致，最终帧也与原捕获读回逐位一致，说明新增读回没有改变既有结果。

初次检查以整块深度/stencil资源断言不变，发现失败；按格式分量复核后确认只有第4字节stencil发生变化，前4字节深度完全相同。失败日志[check.log](../artifacts/research/prewarp-boundary/check.log)保留，修正检查[checks.json](../artifacts/research/prewarp-boundary/checks.json)明确记录stencil变化；没有忽略差异或修改raw使断言通过。

[baseline运行](../artifacts/research/prewarp-boundary/baseline/run.json)、[neutral运行](../artifacts/research/prewarp-boundary/neutral/run.json)。本轮2个重放，0新模型求值、0真实游戏启动；不宣称新的debug-layer0、时域或性能结果。

## 不复用错输入与下一步

新prewarp整图SHA256：`d0cd6268b380cafb764bcf001430251092d38c39118a0f4d6bab7b1c23574e31`。

旧postwarp整图SHA256：`48971bda56ec2469f58258a9533128c471488dfb096d7cd39176628fa95571b1`。

二者真实不同。旧NR/SR输出对应旧postwarp输入，只保留作已结束的颜色/FXAA离线回接证据，**不能拿来测试新边界**。

下一步用本轮`scene-prewarp.raw`重新生成GPU代理、对齐此位置depth/MV，单帧NR→恢复HDR→SR(P1)后回写新边界，让原466/467扭曲、曝光、tonemap/UI继续执行；FXAA RGB旁路保持为独立控制。原depth32和物体MV跨本warp保持一致，是来源核对的一部分，不等于透明/水面guide和运动标定已通过。随后再制定真实连续帧、jitter/history/reset输入方案。

本阶段仍保持2560×1440同尺寸，有效区中央1280起；低输入高输出会改变warp坐标/分辨率语义，需要单独验证。暂不改游戏/SDK/模型、不选最终桥或宿主、不提交Git。
