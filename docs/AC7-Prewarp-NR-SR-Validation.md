# 扭曲前新输入：NR→恢复HDR→SR→原扭曲/后处理/UI

> 评估决策已更新：下文暗部增亮倍率、变化像素数、高光与FXAA差异仅为诊断线索，**不是已确认缺陷**。NR本来预期改变画面，不要求非中性恒等；不因倍率单独限幅、削弱NR或阻塞推进。质量需具体可见/连续帧对照及用户观感，见[评估决策](AC7-Image-Quality-Evaluation.md)。

**本轮已使用Draw465后/466前的新颜色，重新完成正式NR与SR各一次Reset求值，并配对回接到同一位置。** 模型后的原466/467颜色扭曲、曝光、tonemap、后处理和UI正常继续执行；FXAA保留/有效区RGB旁路两组均完成。所有输入均为新prewarp数据，没有把旧postwarp模型结果搬过来。

这是**2560×1440同尺寸、单Reset帧、文件/CPU staging衔接**，不是连续GPU桥、实时适配器或时域/超分/FG验收。预曝光按用户决定继续暂用P=1，不在本轮重开调查。

## 实际数据链与来源

```text
原捕获Scene210（465后，warp前）
  → D3D11 GPU SDR代理
  → 正式D3D12 NR（新输入，Reset1）
  → 重放现场重算代理并逐位核验
  → D3D11 GPU恢复HDR
  → 裁有效区、精确R11→RGBA16F格式转换
  → 正式D3D12 SR（HDR/DLAA，Reset1，P1）
  → 重新量化回R11，完整NR后源场景逐位核验
  → 写回465后同一rect
  → 原466/467warp → 原曝光/tonemap
  → FXAA保留或有效区RGB旁路 → 原后处理/UI → 最终帧
```

| 数据 | SHA256 |
| --- | --- |
| 原始prewarp整图 | `d0cd6268b380cafb764bcf001430251092d38c39118a0f4d6bab7b1c23574e31` |
| 新GPU代理 / NR输入 | `9b8088765efa699bc3620f3f91b05a8d5a1bc434d997b3f768ac9654768f0b9f` |
| 新NR输出 | `f6cd3ebd9246f7c65a7825300baacdfd003bfb6d52d988a0fadb254b614ab3cc` |
| 新GPU恢复的整图HDR / SR源 | `f66911e6f81c40748302efa7d3fa9ad22fac039d2b64680aca86167030b458bb` |
| 新SR RGBA16F输入 | `3e3b8cadf3ecf559ca96360bc345fdce77251d7dc2c5d0fbcf71b4a82fdb09d1` |
| 新SR输出 | `dde71633d88fc25f7139097698bf94749d75c9e02d5788dfe6139907cdc0ca85` |
| 新SR packed R11 | `82b23223729522e4d696c36f126afacee7a2ff2d309f2d2507c63ce52f0afc35` |

原NR/SR runtime SHA256在运行前重新核对，与之前正式310.9.16.0一致；各模型保留公共loader Init/Create/Evaluate/fence/清理的完整日志。没有SDK/模型patch，NGX自身模型缓存/更新器行为沿用已记录限制。

## 当前边界的guide核对

[prepare_prewarp_nr.py](../tools/nr/prepare_prewarp_nr.py)先验证代理有限且在SDR范围，**再核验新位置depth32/velocity与已有GPU规范化的实际源一致**：

- 新prewarp深度/stencil raw逐像素比较前32bit深度，与此前源完全相同；stencil不混进R32深度。
- 新prewarp速度整图与此前UNORM速度源逐位相同。
- 已有规范化depth精确匹配本轮提供的R32输入，已有规范化NDC速度重新按向后像素公式打包，逐项匹配RG16F输入。
- 因此可以复用已验证的guide字节，**不是复用旧模型图像输出**。透明/水/云、物体遮挡与真实相邻帧的guide语义仍未验收。

NR/SR使用同一实际帧jitter=(0,0)、Reset1、reversed-depth诊断配置。没有把同一帧重放当相邻帧，也没有注入虚构时间或伪造历史。

## 改动范围

- [replay_hdr.cpp](../tests/d3d11/replay_hdr.cpp)增加可选`AC7_HDR_DUMP_PREPARED`，读回实际GPU prepared纹理；拒绝覆盖旧证据。原有代理逐位校验与恢复逻辑保持，没有改公式。
- [prepare-prewarp-chain.cjs](../tools/nsight/prepare-prewarp-chain.cjs)复用既有prewarp检查点生成器，在465处串接HDR处理与可选SR；另存原始输入、NR后HDR，沿用既有SR完整源校验与FXAA RGB旁路。
- [prepare_prewarp_nr.py](../tools/nr/prepare_prewarp_nr.py)新增本位置guide来源验证和新代理输入准备；SR输入/packed转换复用已有工具。
- [check-prewarp-chain.cjs](../tools/nsight/check-prewarp-chain.cjs)复核完整哈希链、旁路、实际GPU目标、所有guide/扭曲/UI、不越rect、曝光、有限HDR与高光保护。

所有派生源码/工程位于新`build/prewarp-chain-source`/`build/prewarp-chain`。原捕获、游戏、SDK/模型及游戏设置未改，没有新增依赖或实时hook。

## 两次正式模型运行

| 检查 | NR | SR |
| --- | --- | --- |
| Init/Create/Evaluate、GPU完成、释放/关闭 | 全成功 | 全成功 |
| 输入上传/读回 | 颜色、深度、速度逐位 | 颜色、深度、速度、曝光逐位 |
| D3D12 debug error | 0 | 0 |
| 有限RGB / 非有限RGB | 11,059,200 / 0 | 11,059,200 / 0 |
| 输入/输出非恒等RGB像素 | 3,686,165 | 2,478,248 |
| 输出最大RGB | 1（SDR代理） | 102.8125（HDR） |
| >1输出通道 | 0 | 336,123 |

[NR输入合同](../artifacts/research/prewarp-chain/nr-input/input.json)、[NR运行](../artifacts/research/prewarp-chain/nr-run.json)、[NR输出检查](../artifacts/research/prewarp-chain/nr-output/checks.json)；[SR输入合同](../artifacts/research/prewarp-chain/sr-input/diagnostic-contract.json)、[SR运行](../artifacts/research/prewarp-chain/sr-run.json)、[SR输出检查](../artifacts/research/prewarp-chain/sr-output/checks.json)。模型工具中的“独立诊断/no reintegration”限制描述该求值子阶段；后续回接证据单独由本页重放提供，不靠改写历史日志宣布成功。

## 新边界回接检查

五次成功重放：GPU代理/中性、NR恢复、NR后恒等SR、实际SR+原FXAA、实际SR+FXAA RGB旁路。每次重算代理一致后才用NR输出；每次SR回写前整图核验NR后源。既有原扭曲与其stencil运算继续运行。

| 检查 | 结果 |
| --- | --- |
| GPU代理/中性组 vs 原prewarp基线 | 15项raw逐位一致 |
| SR恒等组 vs 本轮NR回接组 | 18项raw逐位一致 |
| SR前颜色实际目标 vs packed SR | 中央每行逐位一致 |
| NR改动有效区prewarp HDR像素 | 3,598,369；区外0 |
| SR再改变prewarp HDR像素（相对NR） | 418,546；区外0 |
| SR再改变postwarp HDR像素 | 422,203；区外0 |
| SR再改变最终帧（原FXAA保留，相对NR） | 896,097；区外0 |
| FXAA旁路相对保留的最终变化 | 78,372；区外0 |
| 原warp作用于SR结果 | 改变88,880个有效区像素；区外0 |
| 扭曲场、各阶段depth/stencil、速度、三张UI | 与各自原阶段基线逐位一致 |
| FXAA旁路 | 有效区RGB等于tonemap结果，原alpha/区外字节保留 |
| 曝光after | 所有组逐位一致，R1.00000500679/G1 |

**“各阶段depth/stencil不变”是各组对同阶段基线不变**；prewarp与postwarp之间stencil仍按原流程变化，不重复早先整块深度误判。原warp位于SR之后，消除了“把已被这一pass偏移的颜色直接配原guide”的已知位置问题；不能因此宣称全场景guide/历史已校准。

GPU恢复的NR HDR最大166，20,478个本轮完全保护高光像素逐位不变；**11个暗部通道增亮超过10倍，最大约17.10倍**。这是本轮NR输出/恢复后的局部改动定位线索，**并非已确认缺陷或独立阻塞项**；未加clamp掩盖，也没有因整体图正常就验收质量。SR后packed最大103来自102.8125量化，与此前空间响应观察一致；细高光衰减仍待连续帧/画质检查。

[完整链检查v2](../artifacts/research/prewarp-chain/checks-v2.json)，v1保留首次完整检查，v2只增加NR暗部相对增益统计，没有重新调用模型。

## 不混用旧结果的负向验证

1. 将当前NR代理身份换成旧postwarp代理：重放退出1，报代理与实际NR输入不符，**在NR恢复写回前拒绝**，没有`scene-nr.raw`。
2. 将SR预期源换成旧postwarp NR恢复整图：重放退出1，报`SR expected full scene mismatch; refusing writeback`，**在SR写回前拒绝**，没有模型后`scene-prewarp.raw`。

[旧代理拒绝](../artifacts/research/prewarp-chain/reject-old-proxy/checks.json)、[旧SR源拒绝](../artifacts/research/prewarp-chain/reject-old-sr-source/checks.json)。本轮总计7次派生重放（5成功/2预期拒绝），新模型Evaluate恰好2次，没有参数轮询或重试模型。

## 最终画面和下一步

[四组最终帧与机体局部对照](../artifacts/research/prewarp-chain/comparison.png)：左上原始无NR/SR，右上NR，左下NR+SR保留FXAA，右下NR+SR旁路FXAA。整帧按真实R10G10B10输出解码，局部取原tonemap/后处理后、UI之前的BGRA；不是代理ACES预览。飞机、海岸、云、绿色HUD布局保留，没有明显黑屏/整体错位；静态缩略图不替代高光/暗部/时域细节验收。

**当前完成到“扭曲前的新配对NR/SR单帧输出能经过原扭曲及后处理到达最终帧”。** 下一步应从单Reset进入真实连续帧条件：确认如何取得相邻颜色/depth/MV、帧身份/时间、jitter和history/reset，再测试曝光变化、遮挡/透明、水面云层。FXAA菜单没有TAA选项，当前jitter0只证明本帧值，不证明DLSS所需时域采样已具备。重复重放同一帧不能替代真实序列。

这一步需要先制定窄范围采样/抖动方案；若必须深入修改UE4渲染器则按用户边界停止该路线。当前无需用户盲目重复F11或重抓普通截图。低输入高输出、实时跨API桥、FG呈现及性能仍独立未实现；P1继续暂用，预曝光调查延后。没有Git提交或游戏部署。
