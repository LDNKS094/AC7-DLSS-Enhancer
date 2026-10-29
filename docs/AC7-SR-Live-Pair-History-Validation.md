# 真实相邻两帧SR：首Reset、次保留历史执行验证

## 结果与范围

已使用动态样本1123→1124，在同一个NGX SR feature及同一组GPU纹理上执行`Reset=1→0`，并完成同输入`Reset=1→1`对照。**两帧执行/GPU完成/输入读回/有限HDR输出通过，第二帧呈现历史设置相关差异。** 这不是动态画质、长期稳定性或完整NR→SR链时域验收。

为隔离SR历史，此次输入为**原始prewarp HDR**，没有叠加NR。已有候选NR SDR代理→恢复HDR→SR顺序保持；下一阶段才把真实连续NR结果接入这个已验证的SR执行路径。实际jitter=0、PreExposure=1/ExposureScale=1继续显式标为未校准假设，不因本次成功改写为游戏事实。

## 真实输入与来源

源数据：[动态两帧资格](AC7-Dynamic-Live-Pair-Validation.md)，保全目录`artifacts/captures/live-pair-70932-125580640/`。新[prepare_live_sr_pair.py](../tools/nr/prepare_live_sr_pair.py)首先核验原始audit及SHA256、相邻计数/模拟推进资格，拒绝覆盖；每帧独立读取HDR/depth/velocity/View/曝光，未复用旧捕获guide。

- 有效rect(1280,0,2560,1440)裁切后，R11 HDR精确转RGBA16F，转换后逐通道反查相等。
- 硬件depth32原字节保留，忽略stencil/X24。
- motion沿此前已核shader公式：object编码R>0解码，否则由该帧depth和实际View row110–113相机回退，按实际rect推导像素中心NDC；先舍入到float32 NDC，再转NGX向后像素FP16。是CPU shader参考，不冒充本次新增GPU规范化验证；透明/遮挡独立覆盖仍未验收。
- 两帧最大合成运动约2.51527/2.98419px，FP16最大分量误差0.000976175/0.000976533px；实际曝光R均1.000001311302185。
- 实际View jitter均0，不伪造子像素样本。

输入与合同保存在`artifacts/research/sr-live-history/input/frame0/`、`frame1/`及[sequence-contract.json](../artifacts/research/sr-live-history/input/sequence-contract.json)。

## 执行改动

扩展既有[preflight.cpp](../tools/nr/preflight.cpp)及[evaluate_frame.cpp](../tools/nr/evaluate_frame.cpp)，复用公开NGX loader/正式SR310.9.16.0、D3D12上传/屏障/fence/读回和清理代码，不增加新后端：

```text
d3d12-sr-pair-history         frame0 Reset1 → frame1 Reset0
d3d12-sr-pair-reset-control   frame0 Reset1 → frame1 Reset1
```

每个进程只初始化/创建一次feature，分配一次5张texture（color/depth/MV/output/exposure）、upload/readback；第二帧更新原texture，第一帧提交完成后才继续，状态CopySource→CopyDest→NPSR/UAV→CopySource对应。每帧output重新填NaN sentinel。参数和feature持续，保留NGX内部history；不把第一帧输出当第二帧颜色。

每帧使用单独短命令列表/allocator/fence，但模型feature及texture身份不变。日志逐帧记录feature、color/output地址及Reset；其余texture由同一持久vector持有。每帧GPU提交后等待最多10秒，完成才读回、继续或释放；失败不跳帧。输出frame0/frame1已存在或必需输入缺失时，NGX初始化前拒绝；具体文件格式/内容仍在求值前校验。

两帧仍2560×1440 HDR/DLAA、HDR/MVLowRes/DepthInverted创建flags11，MVScale1、曝光纹理、P1/ExposureScale1。没有图像shift、实际jitter注入、实时资源桥或游戏写回。

## 实测证据

完整文件在`artifacts/research/sr-live-history/`；[checks.json](../artifacts/research/sr-live-history/checks.json)由[check_sr_pair.cjs](../tools/nr/check_sr_pair.cjs)从raw和日志重新核验。两主实验各2次Evaluate，加旧单帧回归1次，合计5次SR求值，0次NR。

| 项目 | 历史组 | Reset对照 |
| --- | --- | --- |
| 调用 | Reset1→0 | Reset1→1 |
| feature/texture | 同进程两帧身份不变 | 同进程两帧身份不变 |
| Create/Evaluate | Success，2次Evaluate | Success，2次Evaluate |
| GPU完成/原输入 | 每帧fence完成，4输入逐位读回 | 每帧fence完成，4输入逐位读回 |
| debug | 两帧D3D12 error0 | 两帧D3D12 error0 |
| 输出RGB | 每帧11,059,200通道全有限，无负值、保留HDR>1 | 同左 |
| 第一帧HDR峰 | 109.3125 | 109.3125 |
| 第二帧HDR峰 | 96.5 | 92.5625 |
| Release/parameters/shutdown | 全Success | 全Success |

第一帧两组输出**逐位一致**，SHA256 `4c1e7d76730637a3b944b4b8e1a1e543015b11ef26cd3a43b5ea0f4bd6dbf0b0`。第二帧差异：

- 1,849,323个RGB像素不同。
- HDR绝对差均值0.0177312646、最大64.3125。
- history输出SHA256 `1967f462e6a5d00e0f321eb70cac9fa228c66990d57449366f5322dfb6205a59`。
- reset输出SHA256 `450377f5305f9eed74344924c4ef6eaa3a87736e0a219ee9ccf700c0cffa3ba3`。

第一帧一致、相同第二帧输入和feature资源合同下仅Reset切换，支持历史设置影响输出。差异不是错误或质量分数；不能据像素数、均值或峰值单独判历史组更好/更差，遵循[画质决策](AC7-Image-Quality-Evaluation.md)。没有读取私有历史内容或宣称内部模型状态真值。

## 回归与限制

旧`d3d12-sr-reset-diagnostic`入口对同一frame1单独求值，输出与双帧Reset对照的frame1逐位一致。覆盖已有history目录和缺少输入两种负向都在Init前拒绝，GUID自检通过，见[regression-checks.json](../artifacts/research/sr-live-history/regression-checks.json)。

当前通过的是**真实动态输入下，SR同feature跨两帧的执行合同与历史敏感输出**。尚未验收：NR连续输出输入、GPU guide规范化对本样本误差、物体/透明/反遮挡完整性、8–16帧长期稳定、实际渲染jitter和低高输出、原游戏后处理回接及实时性能。P调查仍延后，不以未知P或诊断差异单独阻塞后续；必要时根据具体可见/曝光相关异常再排查。

下一步优先把这份真实两帧送入原GPU规范化/SDR代理及正式NR，再恢复HDR、复用本次持久SR路径，补足连续NR→SR离线链；之后再请求受限8–16帧观察。当前无需用户重新采这对帧。原始捕获/实机数据/游戏目录未修改，未提交或推送。
