# 首次动态相邻两帧：输入采集资格

## 结论

用户目录`F:/SteamLibrary/steamapps/common/ACE COMBAT 7/AC7-two-frame-70932-125580640`已完整采到**游戏模拟和相机正常推进的相邻渲染两帧**，通过当前路径的基本连续输入资格。它补齐上一份77324样本游戏时间保持的缺口；不等于运动引导全部像素、透明/遮挡、长期历史、真实jitter或SR画质已验收。

原始2×5资源、manifest、metadata、候选trace及ReShade日志已复制到[项目证据](../artifacts/captures/live-pair-70932-125580640/)。原文件不改；独立[审计](../artifacts/captures/live-pair-70932-125580640/audit.json)通过现有[check-live-pair.cjs](../tools/nsight/check-live-pair.cjs)产生。当前不需要用户重复两帧采样。

## 证据

| 项目 | 实测 |
| --- | --- |
| 传输 | v3，2帧，235,937,808逻辑字节，无INCOMPLETE；10份资源头和长度正确 |
| 场景 | 分配5120×1440，有效rect(1280,0,2560,1440)，View尺寸和raw一致 |
| 候选链 | 每区间guides→prewarp→tonemap，共6事件，1个guide候选，viewport一致 |
| 帧计数 | 1123→1124，步长1 |
| 游戏时间推进 | 0.016980648040771484秒 |
| 实时时间推进 | 0.016981124877929688秒 |
| game/real前后链 | B前时间分别精确等于A当前时间，残差均0 |
| 矩阵链 | 6组A当前/B前矩阵逐位对应，相机矩阵块在两帧间发生变化 |
| 颜色变化 | 有效区1,411,575像素，区外0 |
| depth/stencil变化 | 有效区1,598,651像素，区外0；这项合并统计不把X24未定义填充当深度 |
| 原始物体MV | 两帧完整raw相同；frame1有效编码278,070像素，解码幅度最大约0.00085062px，>0.1px为0 |
| jitter | 两帧实际View row114均0；不代表已有完整DLSS子像素采样 |
| 曝光 | 两帧均[1.000001311302185,1]；PreExposure=1继续暂用，不宣称已校准 |

旧审计输出`dynamicTemporalQualified:false`为保守的完整动态质量标志，不妨碍本样本通过**动态输入采集资格**；原manifest和审计字段保持，不改成“SR已通过”。与上一份相比，`simulationAdvanceObserved:true`且`cameraMatrixBlockUnchanged:false`，不再是静止/模拟保持样本。

## 为什么原始MV相同仍有相机运动

AC7的已知着色器合同是：编码R>0取object路径，其余从depth和实际ClipToPrevClip重投影，最后合成有效motion。不能仅凭object texture恒等判整个motion为0，也不能将它未经解码直接当NGX motion。

对frame1未编码的3,408,330像素，以实际View row110–113和当前depth重建相机回退，结果在[camera-motion.json](../artifacts/captures/live-pair-70932-125580640/camera-motion.json)：

- 非有限运动0；超过0.1px为1,372,198像素。
- 最大长度2.9841907649px；抽样P95为1.86204px、P99为2.45209px。
- X范围[-2.60505,2.60530]px，Y范围[-0.00167021,1.45527]px；732像素重投影位置超出前帧rect，是边缘/无历史候选，不能强行当有效历史。

公式复用此前已按PS反汇编核对的currentNDC−previousNDC；像素中心按实测rect推导`cx=(x−2559.5)/1280`、`cy=(y−719.5)/−720`。当前插件未单独保存motion pass的screen参数CB，因此这里明确为已验证rect推导与shader公式参考，不冒充每个旧CB均有本次原始快照。未做光流真值、反遮挡判别、透明层或模型重投影质量验收。

## 下一步

当前两帧够用于把实际新数据接入已有规范化/HDR代理工具，并准备**持久feature、首帧Reset/次帧保留历史**的受限执行检查。先利用已存数据，不继续让用户重复同样的采样；后续需要8–16帧稳定性观察时再扩展固定上限采样器。不能把多次独立两帧包拼成连续8–16帧。

保持原NR SDR代理→恢复HDR→SR HDR→原后处理/FG目标，P1延后、实际jitter0不伪造、无深改SDK/模型/引擎。两帧不能判长期闪烁/拖影或完整SR超分画质；模型history执行和原游戏回接仍要各自验证。

本轮只有文件/CPU审计，没有启动游戏、Nsight、GPU模型或修改游戏文件，没有新提交。
