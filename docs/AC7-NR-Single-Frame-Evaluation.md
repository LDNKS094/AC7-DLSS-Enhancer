# AC7真实单帧：正式NR的D3D12图像求值

> 本页保留首次使用游戏post-tonemap场景的求值证据。后续 [HDR往返参考验证](AC7-HDR-NR-Roundtrip.md)已完成CPU HDR准备/恢复+GPU NR、中性原格式逐位一致及高光检查；仍未完成GPU颜色pass、真实回接或时域验收。

## 当前结论

在RTX5080、驱动617.42上，官方NR310.9.16.0通过公开NGX loader、自建CUSTOM Project ID，已对**正常飞行捕获中的真实无HUD场景及规范化深度/速度**完成一次2560×1440、Reset=1的独立D3D12 Evaluate，提交、GPU完成、读回和清理通过。

D3D12调试层启用，存储的error/corruption消息为0；未启用GPU-based validation。三张输入纹理上传并在求值后读回，与准备文件逐位一致。输出先初始化为NaN哨兵，最终所有RGB通道有限，没有哨兵残留，而且不是输入的恒等拷贝。

这推进了NR执行验证，**尚未证明最终Pre-SR位置、HDR往返、多帧稳定性、性能、SR/FG或AC7实时接入成立**。

## 输入与正式契约依据

新增官方源码检查固定在Remix提交`890740c237e72cf9df97438605e9582a8f8820f9`：

- [rtx_context.cpp](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/890740c237e72cf9df97438605e9582a8f8820f9/src/dxvk/rtx_render/rtx_context.cpp#L1914)明确注释“DLSS-NR operates in SDR space”，在NR前快速tone map，成功后inverse tone map恢复HDR。
- [NR颜色资源](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/890740c237e72cf9df97438605e9582a8f8820f9/src/dxvk/rtx_render/rtx_resources.cpp#L1589)使用RGBA16F。正式快速tone-map shader调用linearToGamma，inverse shader执行gammaToLinear和HDR重建。
- 正式NR参数头、Vulkan helper及Remix wrapper明确color/depth/MV/output、各自subrect、Reset、DepthInverted、MVecScale、强度和jitter设置。D3D专用helper仍缺失；本轮通过公开D3D12 API和正式参数名实测，不混用Vulkan资源结构或snippet ABI。

因此这次使用**AC7自身已经tonemap的无HUD场景**，而不是把最高166的线性HDR值直接送进NR，或加一个猜测曝光的转换器。

| 输入 | 本轮数据与处理 | 证据边界 |
| --- | --- | --- |
| Color | Draw525后真实post-tonemap资源，5120×1440 BGRA8 typeless；裁中央[1280,0,2560,1440]，按既有UNORM颜色解释转换RGBA16F，alpha设1 | 无新增gamma或曝光；完整颜色空间尚未独立标定；不是最终HDR/Pre-SR回接 |
| Depth | 同帧D3D11规范化GPU输出R32_FLOAT，2560×1440，原样上传 | DepthInverted=1对应当前反向Z候选，未据此声称通用相机标定完成 |
| Motion | 同帧D3D11规范化GPU输出RG32_FLOAT，current-minus-previous NDC；转为previous-minus-current屏幕像素(-x×1280,y×720)，存RG16F | MVecScale=(1,1)；最大分量约3.06724548px，FP16量化误差最大0.0009765625px。独立相邻帧时序验证仍缺失 |
| History/jitter | Reset=1；Jitter=(0,0)来自该帧实际常量 | 没有复制单帧冒充连续帧，未建立稳定历史 |

本轮固定NR设置：Enabled=1、Intensity=1、Style=0、local tone=.3、structure=.7、skin structure=.5（数值参考官方Remix默认）；AutoMask=0、MaskBlendMode=0、UICorrection=0。没有HUD、control mask或distortion资源，相关可选资源显式为空。所有四张主要纹理同尺寸、subrect原点0；没有空间周边压缩或超分。

预览使用原始SDR码值到8-bit的显示转换；统计直接来自未钳制FP16输出，不从预览倒推。

## 结果

| 检查 | 结果 |
| --- | --- |
| Init / capability / CreateFeature | 全部Success，NR可用，feature非空 |
| EvaluateFeature | **0x1 Success** |
| 创建和求值的GPU fence | 均completed=1 |
| 输入颜色/深度/速度上传后读回 | **三者逐位一致** |
| D3D12 debug error/corruption | **0** |
| 有限输出RGB通道 | **11,059,200 / 11,059,200** |
| NaN/Inf RGB / 残留NaN哨兵 | **0 / 0** |
| 输出RGB范围 | **[0,1]**，没有负数或>1通道 |
| 非有限alpha | 0 |
| RGB变化像素 | **3,686,232 / 3,686,400** |
| 相对输入的平均绝对变化 | 0.007586102735518512（SDR码值） |
| 相对输入的RMS变化 / 最大变化 | 0.011612540965981836 / 0.216796875 |
| ReleaseFeature / DestroyParameters / Shutdown | 全部Success |

变化量不是相对正确答案的误差，更不是画质评分。助手检查两张读回预览：场景、飞机、海岸和云层布局保留，没有明显黑屏、整体错位或HUD混入；这只是单帧可视检查，不能据此称为最终画质通过。没有记录性能，调试/同步读回的用时也不作为实时性能。

## 复现与证据

代码只扩展独立探针，未接游戏：

- [prepare_frame.py](../tools/nr/prepare_frame.py)：校验既有raw头/长度、有限深度/速度，裁场景、转换FP16并记录源SHA256；要求新目录避免覆盖既有证据。
- [evaluate_frame.cpp](../tools/nr/evaluate_frame.cpp)：上传/状态转换、正式参数、一次Evaluate、fence、全部输入/输出读回及debug检查。
- [preflight.cpp](../tools/nr/preflight.cpp)：复用已有初始化/创建过程，新增`d3d12-evaluate`模式。
- [check_frame.py](../tools/nr/check_frame.py)：未钳制浮点统计、非恒等/未写入检查、SDR预览；无需第三方Python依赖。

```text
python tools/nr/prepare_frame.py --self-test
python tools/nr/prepare_frame.py <现有AC7正常帧artifact目录> <新的准备目录>
preflight-nr.exe d3d12-evaluate <正式NR目录> <新的求值日志目录> <自建GUID> <准备目录>
python tools/nr/check_frame.py <准备目录> <求值日志目录>
```

独立CMake构建入口不变；SDK头/库不纳入源码。GPU提交后必须完成才释放资源；超时/设备移除时停止该独立进程，禁止以未完成资源继续测试。输出文件已存在时拒绝覆盖。

- [输入来源、转换与限制](../artifacts/research/official-nr/frame-20261028-sdr/input.json)
- [调用与原始NGX日志](../artifacts/research/official-nr/evaluate-20261028-sdr.json)
- [浮点检查及输入/输出SHA256](../artifacts/research/official-nr/evaluate-20261028-sdr/checks.json)
- [输入预览](../artifacts/research/official-nr/evaluate-20261028-sdr/input-preview.png) / [NR输出预览](../artifacts/research/official-nr/evaluate-20261028-sdr/nr-preview.png)

官方追加源码和下载文件的Git blob身份核对见 [源码身份记录](../artifacts/research/official-nr/evaluate-source-identity.json)。原始capture/readback未修改，运行时保持原文件。没有Git提交或游戏文件部署。NGX初始化自身会维护ProgramData模型缓存/启动官方更新器，其已知副作用见 [初始化记录](AC7-NR-Runtime-Preflight.md#ngx本身的副作用)。

## 当前下一步

NR正式运行时的D3D12独立执行已成立，不能再把“缺NR DLL/尚无Evaluate”列为阻塞。HDR参考及GPU中性阶段见页首记录；后续 [同一GPU代理的NR实际非中性回接](AC7-NR-GPU-Writeback-Validation.md)已经完成，原曝光当前算术、后处理/UI和最终帧已检查。下一步核实SR HDR合同及真正插入/高输出回接，仍无实时桥、动态曝光/时域或SR/FG验收，不深入改工具链。

时域验证需要真实相邻帧，当前单帧不能补出jitter/history/reset、透明动态覆盖或运动方向的独立证据。最终5120×1440超宽屏投影、周边压缩、SR输入/输出尺度、FG帧节奏也各自未验收；不因此反复启动游戏、重启抓帧工具或修改系统设置。
