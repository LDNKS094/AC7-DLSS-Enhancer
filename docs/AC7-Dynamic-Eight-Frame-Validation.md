# 首次真实动态8帧输入资格

## 结论

用户提供`F:/SteamLibrary/steamapps/common/ACE COMBAT 7/AC7-eight-frame-58000-130125640`，并已关闭NVIDIA App自动优化。主会话只读确认`EnableAutomaticApplyOPS=0`；本轮没有启动AC7本体、Nsight或任何AC7派生重放。

**8帧传输完整，698→705真实连续动态输入基本资格通过。** 不需用户再次补采。原manifest中temporalQualified=false保留，输入基本资格不等于完整guide/时域模型画质验收。后续以此序列推进独立NR→HDR→SR文件链，不回接游戏。

## 保全与资源

全部原始文件已复制到`artifacts/captures/live-eight-58000-130125640/`，逐文件SHA256与原路径核对；共943,757,633字节（包含元数据，不含另存audit/log），原文件未修改。包含8×5资源、8份scene metadata、24候选事件与manifest；另外保存ReShade.log。[独立审计](../artifacts/captures/live-eight-58000-130125640/audit.json)包含每份文件哈希、每段时间和六组矩阵链。

- allocation5120×1440，active rect(1280,0,2560,1440)：恢复此前100%规模，没有200%场景分配。
- 两槽逻辑staging峰235,937,808字节≈225MiB，低于256MiB；总逻辑943,751,232字节≈900MiB。不是整个进程工作集上限。
- 同步写盘记录349.575ms，7个渲染时间差累计286.169ms；该写盘总量包含多个回调中的写入及序列端点之外的排空，不能当本区间GPU时间或FPS结论。
- source R11G11B10、depth R32G8X24有效depth32/stencil8、R16G16 motion、4096B View、1×1 RG32F曝光头/长度全部正确。未比较未定义depth X24填充来作运动判定。

## 连续性与运动

所有7段计数严格+1，game/real前值串接残差均0；每段六组current→previous矩阵链均逐位精确，相机矩阵每段均变化。

| 帧 | 候选计数 | 到下一帧game差ms | real差ms |
| --- | --- | --- | --- |
| 0 | 698 | 15.926838 | 15.926361 |
| 1 | 699 | 16.668797 | 16.668320 |
| 2 | 700 | 32.851696 | 32.851219 |
| 3 | 701 | 88.590622 | 88.590622 |
| 4 | 702 | 23.591518 | 23.591995 |
| 5 | 703 | 90.177536 | 90.177536 |
| 6 | 704 | 18.362999 | 18.362999 |
| 7 | 705 | — | — |

[逐像素原始运动审计](../artifacts/captures/live-eight-58000-130125640/pixel-motion-audit.json)：每段scene有效区变化2,810,124–3,621,079像素，depth/stencil有效分量1,592,229–1,673,488像素，原始velocity248,260–255,150像素；所有区外变化0。既有相机变化也有物体纹理变化，排除此前后台simulation-held那种仅颜色改变。

GPU GuideNormalizer按每帧实际View/depth/velocity重建，8帧validity只有1camera/2object，无invalid。camera约343.7–343.9万像素、object246,946–249,642像素，所有像素合成后位移>0.1px；每帧max motion约20.36/18.64/19.60/38.76/105.10/28.50/107.77/22.24px，非有限0。数据见[GPU运动统计](../artifacts/research/live-nr-sr-eight/gpu-motion-stats.json)。这只是既有shader公式执行/数值资格，不是遮挡、透明、粒子或所有几何的光流真值。

实际jitter八帧均0。曝光R从1.0000016689300537至1.000001072883606，G始终1；P=1仍为已接受暂用假设，不新增预曝光阻塞。

## 后续与限制

这是一段约0.286秒、包含写盘卡顿的真实连续输入，不能取代长场景稳定性、切镜头/reset/动态曝光/透明遮挡评估。零jitter只能支持当前同尺寸SR诊断，不能宣称完整SR重建质量或高输出/FG已验收。

本轮进一步复用现有GPU代理/GuideNormalizer与公共NGX工具扩展固定8帧离线链；不拼接两帧、不重复输入冒充序列，不改模型/SDK/游戏配置或采样预算。[既有两帧链](AC7-Live-Pair-NR-SR-Validation.md)仍作为回归证据。
