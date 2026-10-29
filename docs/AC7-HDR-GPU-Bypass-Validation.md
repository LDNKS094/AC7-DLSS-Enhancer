# GPU HDR颜色往返与AC7离线回接

> 本页保留早期中性往返与CPU对照阶段。后续已完成 [GPU代理实际NR求值及非中性回写](AC7-NR-GPU-Writeback-Validation.md)：同一输入逐位核验、原曝光/后处理和UI检查已做；仍是离线文件衔接，无实时桥。

## 当前结果

用户接受的候选顺序已记录为：

`游戏场景HDR → SDR代理 → NR(SDR) → 恢复场景HDR → SR(HDR) → 原游戏后处理/最终tonemap → FG最终颜色、引导、UI与呈现`。

该顺序尚未全链路实测；本轮验证的是最窄的两个GPU颜色pass与**中性离线回接**。无需修改SDK/模型或UE4整套调色工具链。D3D11硬件验证中性往返原R11G11B10逐位一致；插入已有AC7捕获后，场景、曝光和最终图像七个检查点均逐位一致。

**没有执行新NR调用、SR或FG，没有实时hook或跨API桥。** 恢复非中性结果的独立GPU对照使用上一轮已读回的真实NR结果，不能写成GPU准备→NR→GPU恢复已经连续联通。

## 决策与边界

- SDR/HDR看插入位置的资源语义，不看显示器模式。AC7的SDR显示输出不消除tonemap前的场景HDR。
- 当前NR310.9.16.0需要SDR兼容代理；HDR直入零强度也钳制到[0,1]，不再探索隐藏HDR开关。
- 在候选Pre-SR链中，先恢复场景HDR，再交给具有HDR输入合同的SR；不预定NR/SR都在SDR域后统一恢复。SR具体曝光/格式/尺度仍待验证。
- FG应取得最终阶段所需颜色/引导/UI及正确呈现条件，不能把NR临时SDR代理当最终图像。中间有代理不自动阻止FG；闪烁、恢复伪影、坐标/帧对应和GPU耗时会间接影响它，目前未验收。
- 用户限定不深入修改工具链继续有效。本次只加局部计算模块和离线测试，沿用公开D3D11 API与既有状态隔离；未改NGX/SDK、模型、驱动或游戏。

## 最小实现范围

[HdrProxy](../src/d3d11/hdr_proxy.h)沿用既有D3D11.1私有context state方式，在序列化immediate context上记录两个pass：

1. `Prepare`读取明确view rect及实际曝光，生成RGBA16F的ACES/gamma2.2代理。
2. `Restore`保留原HDR，只转移NR相对**本次实际FP16代理**的改变量，应用既有高光mask，写回R11G11B10格式。非法模型像素回退原HDR。

代码：[hdr_proxy.cpp](../src/d3d11/hdr_proxy.cpp)、[HLSL](../src/d3d11/hdr_proxy.hlsl)。公式和来源继承 [CPU参考及授权通知](../tools/nr/hdr_reference.cpp)，没有引入另一套颜色算子、增益调节或完整框架。调用者仍需确认输入场景的有限非负语义及实际曝光；模块不会从浮点格式推断颜色primaries/pre-exposure。

输入设备、格式、view范围、资源别名及曝光界限在状态切换前验证。两次状态交换之间没有会抛异常的调用。模块不替调用者提交/同步或管理模型历史。

`neural=nullptr`用真正写入的FP16代理作中性输出，这是诊断恒等路径；真实NR关闭仍应直接旁路。不能称为模型Enabled=0测试。

## 独立GPU与CPU参考对照

[validate_hdr.cpp](../tests/d3d11/validate_hdr.cpp)使用真实裁剪HDR、实际曝光1.0000050067901611、上一轮真实NR SDR输出及CPU恢复结果。硬件D3D11调试设备执行，读回后断言：

| 检查 | 结果 |
| --- | --- |
| GPU中性往返R11G11B10 | **全部3,686,400像素逐位一致** |
| 视口状态恢复 | 通过 |
| 越界view、NaN曝光、错误NR格式 | 三项拒绝，未进入记录 |
| 人工NaN模型像素 | 回退为原HDR，原格式逐位一致 |
| D3D11 debug error/corruption、设备健康 | **0 / S_OK** |
| GPU代理与CPU代理最大差 | **0.00048828125**（FP16约一个码位） |
| GPU代理与CPU代理不同RGB通道 | **6,129,104 / 11,059,200**；不声称同一输入文件 |
| 非中性恢复与CPU FP16参考最大绝对差 | **0.125**（原HDR尺度） |
| 非中性结果与CPU再打包不同像素 | 3,118,240；不声称逐位等价 |

非中性对照允许`max(0.002, 目标通道×0.035)`误差，覆盖R11G11B10尤其蓝通道的低精度与CPU/GPU运算/二次量化差异。这个容差是数值检查，不是画质标准；它不能替代细节/暗部和时域验收。

**重要限制：** 上一轮NR读回的输入是CPU代理，本轮生成的GPU代理存在上述FP16码位差。因此这里只比较局部公式数值，不把这组非中性结果当成已连续联通的NR管线。下一阶段必须让模型实际消费GPU生成的代理，再恢复/回接。

[独立GPU结果](../artifacts/research/official-nr/gpu-hdr-validation-guard/checks.json)，[执行日志](../artifacts/research/official-nr/hdr-gpu-validation-guard.log)。初版独立检查也保留，没有覆盖原始证据；后一次运行用于新增的非法模型像素回退检查，不重复跑模型。

## AC7捕获中的中性回接

派生源码由 [prepare-hdr-replay.cjs](../tools/nsight/prepare-hdr-replay.cjs)生成，调用 [replay_hdr.cpp](../tests/d3d11/replay_hdr.cpp)，原始完整capture不修改。

在**生成源码Draw467之后**读取当时HDR场景、裁中央2560×1440，用同一HdrProxy执行中性GPU往返，只把有效区域写回原5120×1440分配，再让原捕获继续执行。场景之外的区域不动。Draw序号仅是该捕获源码定位，不是通用runtime识别规则或已确认最终Pre-SR点。

曝光显式从先前实际读回文件提供，这是**离线诊断已知量**，不是在该插入点已经取得实时同帧曝光的证据。因为模型中性，不能用本轮不改变曝光证明NR开启时没有曝光反馈。

独立重放1帧退出0，以下完整raw（包含头及全部分配）均与原颜色分析基线逐位一致：

1. 场景HDR（写回后立即检查）；
2. 前一曝光；
3. 降采样亮度统计；
4. 适应后曝光；
5. tonemap实际使用曝光；
6. tonemap后场景；
7. 最终帧（含原后处理/UI）。

[七项逐位检查](../artifacts/research/official-nr/hdr-neutral-replay/checks.json)、[重放运行记录](../artifacts/research/official-nr/hdr-neutral-replay-run.json)。本轮没有启用捕获外的D3D debug设备，不借独立调试测试代称重放也经过debug验收。没有性能结论：同步读回和离线重建不适合用于实时耗时判断。

## 构建、下一步和停止条件

根CMake新增`ac7_hdr`和`validate-hdr-gpu`，未新增外部依赖；沿用既有Windows SDK/MSVC/D3DCompiler。shader从项目路径编译，部署/打包尚不在本轮范围。

```text
cmake --build build/native-validation --config Release --parallel 4
validate-hdr-gpu.exe <HDR准备目录> <已有NR求值目录> <新输出目录>
node tools/nsight/prepare-hdr-replay.cjs <原捕获目录> <新的派生源码目录>
```

重放需原capture cwd、显式`AC7_REPLAY_DUMP_DIR`与`AC7_HDR_EXPOSURE`，与既有重放约定一致；程序是派生capture EXE，不是实际游戏。

当前下一步见页首最新记录：同一GPU代理→NR→GPU恢复及非中性回接已经完成，曝光当前帧算术/三张UI输入已核对。随后核实正式SR的HDR/曝光/速度/尺度合同、真正Pre-SR插入与高输出回接，再决定最窄跨API资源衔接。仍需真实相邻帧验收jitter、历史、动态曝光，FG呈现及全幅压缩另验。

若需要改模型/SDK/core、重写UE4或整套tonemap工具链就停止该路线。原始捕获、游戏/补丁保持原状，无Git提交；本轮没有NGX初始化，因此没有新增由本轮模型执行触发的缓存更新行为。
