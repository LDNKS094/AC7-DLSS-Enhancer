# SR同尺寸Reset单帧：显式假设诊断

> 后续[同尺寸SR离线回接](AC7-SR-Writeback-Validation.md)已完成NR后HDR→SR→原FXAA/后处理/UI，旁路12项逐位，实际SR输出到达最终帧。本文“未回接”指本阶段历史；仍未实时接入/时域验收。P1按用户决定暂用，预曝光调查延后。

> 后续[高光空间响应分析](AC7-SR-Spatial-Response.md)已定位约90%差异来自稀疏海面高光；同参数一次合成控制中平坦166→162.75、单像素160→3.83、全图输出最大185.125，不支持固定HDR上限约103。仅明确空间响应，未验收游戏曝光、时域或高光画质。本页两组真实帧的原始结果保持。

## 这次解决什么

此前把“不能验收AC7原生输入”与“不能进行任何SR图像测试”合成了同一个停止条件。现已分开：**游戏真实pre-exposure仍未辨识，原生时域接入仍未验收；但可以在显式假设的独立输入域中验证SR资源/格式/求值和HDR输出。**

正式SR310.9.16.0的两组D3D12 HDR/DLAA诊断已经Evaluate/GPU完成/读回成功：原始场景HDR，以及此前NR恢复/回写后的HDR。每组全部11,059,200个RGB通道有限，输出保留>1亮度，没有被钳成SDR。两组都不是画质、时域或超分通过，也没有回接SR输出到游戏。

## 明确假设，避免把未知写成事实

诊断模式名为`d3d12-sr-reset-diagnostic`，日志和输入文件都标明：

- 将读回HDR数值视为一个独立诊断输入域，**明确假设PreExposure=1、ExposureScale=1**。这是测试定义，不是声称已测到游戏真实因子1，更不是悄悄使用helper默认回退。
- 2560×1440输入/输出，DLAA模式，Reset=1，每个样本独立进程；没有低输入高输出，没有伪造多帧历史。
- jitter=(0,0)来自该帧实际View CB row114；向后像素速度/硬件depth来自同一捕获的已验证规范化结果。其跨帧语义仍未独立标定，DepthInverted继续作为该样本有依据的测试配置。
- 1×1 R32_FLOAT曝光纹理显式提供捕获实际R增益1.0000050067901611，无AutoExposure掩盖；它在诊断中固定，不能据此证明最终SR插入点的实时曝光时机/中灰标定正确。
- HDR创建标志、尺寸等沿用已验证的公开SR合同。无SDK/模型patch，无新增框架/依赖。

代码在创建后打印上述假设，并将输出单独保存为`sr-output.raw`，与已有`nr-output.raw`区分。正式游戏接入条件仍需单独证明，诊断成功没有“解决”pre-exposure未知项。

## 数据路径与结果

[prepare_sr_diagnostic.py](../tools/nr/prepare_sr_diagnostic.py)从完整Scene210 raw裁中央有效rect，把R11G11B10逐通道**精确转换**成RGBA16F，没有tone map/gamma/颜色尺度变换。准备过程校验有限范围、原格式尺寸/头、FP16精确可表示性，记录源SHA256；guide复用原字节。两个输入分别来自原始捕获及已验证NR回写捕获，没有用CPU代理结果替换GPU NR路径。

既有 [preflight.cpp](../tools/nr/preflight.cpp)/[evaluate_frame.cpp](../tools/nr/evaluate_frame.cpp)扩展一个明确诊断分支，复用上传、资源状态、fence和读回。曝光是额外1×1输入，原NR分支保留4张纹理。非法模式/参数缺失/非法GUID在NGX Init前拒绝。

| 检查 | 原始HDR | NR回写后的HDR |
| --- | --- | --- |
| Init/Create/Evaluate/GPU完成/释放与关闭 | 全成功 | 全成功 |
| 颜色、深度、速度、1×1曝光上传后读回 | 四者逐位一致 | 四者逐位一致 |
| D3D12 debug error | 0 | 0 |
| 有限RGB / 非有限RGB | 11,059,200 / 0 | 11,059,200 / 0 |
| >1的输出RGB通道 | 358,863 | 335,978 |
| 输出RGB范围 | 0.00157833–102.8125 | 0.00202751–102.8125 |
| 输入最大值 | 166 | 166 |
| 与输入RGB不同的像素 | 2,469,726 | 2,476,329 |
| 平均绝对HDR差异 | 0.0706854 | 0.0709410 |
| 最大绝对HDR差异 | 149.2891 | 149.4141 |
| 非有限alpha | 0 | 0 |

差异不是对ground truth的误差或画质评分；DLAA有重建/采样行为，本就不保证恒等或逐像素峰值保持。另一方面，峰值166→102.8和大的局部差异不能忽略，**本轮没有据API成功验收高光/画质**，也不能把差异直接归因于pre-exposure。

[check_sr_diagnostic.py](../tools/nr/check_sr_diagnostic.py)检查完整未钳制FP16数据；用NaN初始化输出，以有限性防止“未写入被当成黑色”。没有NR那样的SDR[0,1]输出断言，实际验证了HDR输出通道。

原始结果：

- [原始HDR输入合同](../artifacts/research/official-sr/reset-diagnostic-baseline-input/diagnostic-contract.json)、[原始HDR运行](../artifacts/research/official-sr/reset-diagnostic-baseline-run.json)、[原始HDR检查](../artifacts/research/official-sr/reset-diagnostic-baseline-output/checks.json)
- [NR后HDR输入合同](../artifacts/research/official-sr/reset-diagnostic-after-nr-input/diagnostic-contract.json)、[NR后HDR运行](../artifacts/research/official-sr/reset-diagnostic-after-nr-run.json)、[NR后HDR检查](../artifacts/research/official-sr/reset-diagnostic-after-nr-output/checks.json)
- [两组输入/输出对照预览](../artifacts/research/official-sr/reset-diagnostic-comparison.png)、[阶段断言和委派引用](../artifacts/research/official-sr/reset-diagnostic-stage-checks.json)

预览对原始HDR使用测试ACES/gamma2.2曲线，不是AC7原tonemap/LUT，也没有HUD。助手检查了场景/飞机/海岸布局，无明显黑屏或整体错位；静态缩略图不能验收细节/高光和时域。生成全尺寸附加输入预览时，CPU检查超过20秒中断；原GPU raw和已成功统计未改，没有重跑模型。随后用降采样输入和既有输出预览生成对照，不改变完整raw统计标准。

## 扩大的生产链检查

单个只读`magpie/auto/gpt-6-luna` scout限16个shader、最多两层生产关系返回摘要，主会话复核统计：Scene210有**156个直接RTV writer、33个唯一PS**。不是只检查了此前8个shader，也没有声称遍历了所有33个PS。

最后链路为：Scene210→Draw466/PS50261（同时采样辅助资源）→同尺寸HDR中间资源→Draw467/PS50263 copy回Scene210。Draw467本身不额外做曝光乘除。共享CB和多纹理运算提供候选，但仍没有识别出能证明独立pre-exposure的因果配对。不能以有限覆盖中未识别反推它不存在。

[有界生产链摘要](../artifacts/research/official-sr/hdr-producer-chain-audit.md)，run ID及工具托管原始outputReference在阶段断言中。该child没有修改代码或运行GPU，主会话保留诊断假设、模型调用和最终结论。限额中断恢复后仅新建这一个有界核查；没有盲目重复旧GPU测试。

## 当前边界与下一步

已确认的进展：SR不再只有创建结果；**在显式诊断域下已完成同尺寸HDR图像求值，且用过NR恢复后的真实HDR输入**。尚不能把它命名为原生NR→SR整链路验收，因为两段通过离线文件衔接，SR假设尚未匹配游戏实际输入语义。

后续已用已有raw空间统计和一个合成Reset控制区分空间响应与固定HDR上限，详见页首；海面高光衰减仍未验收。下一门槛为**可信相邻帧序列及实际曝光/view语义**，制定明确导出方案后才请求用户操作；不把重复当前帧当成历史。输入合同、SR回接/低输入高输出采样映射、FG呈现及性能仍独立未验收。诊断默认不写回游戏，未知项不会因本次成功自动转为“已知”。

暂不需要用户补普通截图或重抓同一帧。若现有证据无法定位语义来源，会提出具体的最小运行观测/相邻帧采样要求；不会要求用户反复碰运气抓帧。若必须改SDK/模型或深入重写UE4调色工具链，按用户限制停止该路线。

原始capture、游戏/配置、SDK和模型未修改，没有Git提交；正式NGX初始化仍有已记录的系统模型缓存/更新器行为。
