# AC7颜色/曝光证据与后端接口门槛

最新 [SR Reset诊断](AC7-SR-Reset-Diagnostic.md)：在显式独立输入域假设P=1下，原始/NR后HDR同尺寸求值、GPU读回成功，输出HDR范围保留但高光/局部变化未验收。当前游戏真实pre-exposure仍未辨识，诊断假设不改写事实；原生接入和时域标定门槛保持，单帧API/格式诊断不再一概暂停。下一步解释重建变化并定位真实曝光/view来源，不深改工具链。

## 本轮结果

在2026-10-28正常飞行捕获中，已追踪并读回曝光统计、上一值、新值及色调映射实际使用的资源；按观察到的shader复算曝光逻辑，和GPU结果的最大差8.60e-8。所有新增读回完成后，最终输出仍与基线逐位相同。

这补足的是**当前帧曝光应用点和通道含义**，不是所有颜色/时域契约已成立。后端仍未选定；OptiScaler分支资源桥/Pre-SR接口属于可复用对象，不预定为完整宿主或立即实施路线。

## 曝光链：来源和实际应用点

捕获与资源身份见 [正常飞行证据](AC7-Nsight-Normal-Flight-20261028.md)。以下Draw均是生成源码序号：

| 阶段 | 证据 |
| --- | --- |
| 467后 | 5120×1440 R11G11B10_FLOAT场景210，有效rect x1280,y0,w2560,h1440 |
| 469–481 | 场景降采样、编码亮度到RGBA16F的alpha，再逐级降采样至80×23 Texture2D2019 |
| 482 | PS379读取2019及1×1旧曝光204，生成1×1 R32G32_FLOAT Texture2D2020 |
| 525 | VS117的t0绑定2020，Load(0,0).R→TEXCOORD1.x；PS320将场景和bloom等合成后乘这个值，再进入颜色LUT |

仅跟踪PS SRV会遗漏曝光；源码索引已新增VS资源绑定，读回证实525使用的是新2020，不是旧204。

### 曝光shader的两个通道

缩小图的alpha储存经参数编码的log2亮度，不是透明度。PS379按CB0[21]指定区域及中心权重求加权均值，然后：

```text
weightedEncodedMean = weighted mean of alpha
rawTarget = 6.25 * exp2((mean - cb0[20].y) / cb0[20].x)
target = clamp(rawTarget, cb0[18].z, cb0[18].w)
previousAdapted = cb0[19].x / (oldExposure.R != 0 ? oldExposure.R : 1)
blend = 1 - exp2(-selectedSpeed * cb0[19].y)
adapted = clamp(previousAdapted + (target - previousAdapted) * blend, min, max)
newExposure.R = scale / max(adapted, 0.0001)
newExposure.G = scale / max(target, 0.0001)
```

R是随时间适应后的实际应用增益，G是当前目标增益。不是把两个通道随意解释为exposure/pre-exposure。负/非有限值和异常曝光仍需在适配边界验证。

本帧数值：

- stats参数blob744；采样矩形[20,0,60,22)，不是把80×23全部像素连同padding平均。
- encodedMean约0.5550466825。
- 未钳制目标亮度约2.4698375967；参数范围[0.5,1]使目标为1。
- 旧R/G：`[1.0000050067901611,1]`。
- 适应blend约0.0171799486。
- CPU预测新R/G：`[1.0000049207733401,1]`。
- GPU新R/G：`[1.0000050067901611,1]`，R差约8.60e-8。
- 到525实际使用时数值未变化，R约+0.00000722 stops，接近unity。

[结果JSON](../artifacts/captures/nsight-flight-20261028/color/analysis.json)；[复算脚本](../tools/nsight/analyze-exposure-20261028.cjs)含格式、尺寸、blob边界、数值容差及最终图像不变断言。读取检查点由[清单](../tools/nsight/checkpoints-color-20261028.json)固定到本捕获；原始捕获未修改。

这帧目标被钳制，不能据此独立验证快速明暗变化下的动态曝光。也不能由R接近1推断游戏所有上游阶段都没有pre-exposure；pre-exposure仍须从生产shader/其他帧核实。

## 场景颜色域的已知和未知

R11G11B10_FLOAT已在GPU读回后解码检查有效区域全部11059200个RGB通道：

- 非有限通道0，最小值约0.00234985，最大166。
- 353271个通道大于1；不能先saturate再作为NR输入。
- 抽样亮度中位数约0.35635，P95约0.85458。统计在scene210、色调映射前，不是最终截图。
- Tonemap PS320的CB0[34].x整数为0，执行log→3D LUT输入分支；没有走shader中mode>=3的PQ输入分支。
- 场景与bloom/镜头纹理、暗角、随机项等共同影响最终颜色。LUT中可能包含色调曲线/颜色分级/编码，当前没有把它完整逆推出某个标准色域。

因此：

1. 可以明确NR不能直接吃未经准备的场景HDR，也不能将其当普通sRGB纹理。
2. Overglaze的压缩/sRGB输入及比值回填可作候选，不能不验证就作为AC7颜色契约。
3. 主输出BGRA8和10-bit swapchain格式本身都不证明HDR/PQ；本帧shader模式才是分支证据，实际色域仍待核实。
4. SR若消费tone-map前线性工作色，输出必须回到相同工作域，不能把NR display-domain结果直接写回浮点scene供游戏再次tonemap。

## 依据当前AC7事实对照候选接口

| 实际约束 | OptiScaler NR分支 | Luma通用UE路径 | Overglaze |
| --- | --- | --- | --- |
| D3D11输入 | 有D3D11→D3D12资源缓存/同步和输出复制回D3D11实现 | SR接口原生接收D3D11资源 | 游戏NR仅D3D12，无现成桥 |
| 无原生DLSS调用 | 需AC7适配器主动提供feature/参数并驱动，不会安装后自动识别scene | 依赖通用TAA识别，AC7当前已确认空间AA不能直接触发其TAA路径 | 依赖已有NGX/Streamline SR/RR，不适用原样入口 |
| Pre-SR NR→SR | 同一D3D12处理链存在before-upscale调用，可继续核实interop路由 | 通用UE当前设置DLAA-only，NR需额外接入 | Post-SR NR，没有SR |
| 中心保护/周边压缩 | 有pack/unpack与guide端点变换代码 | 没有这条现成完整链 | 只有矩形裁剪/线性尺度重采样 |
| 非零view原点 | PreSrColorExtent和CanRunBeforeUpscale拒绝非零color/output base；需主动裁剪到原点0，再回写明确rect | 必须验证通用检测/输出尺寸条件，不能假定中央view自动支持 | NGX route拒绝非零base，亦需原点0 |
| 速度 | 需提供已规范化速度、尺度和方向，不能传AC7原始UNORM/默认scale | 也需确认DLSS单位和方向 | float MV输入不接受本模块RG32_FLOAT原样，需明确转换/接口支持 |
| 深度 | 输入状态/格式需桥中核实，建议传独立R32_FLOAT | D3D11深度契约需核对 | 能将双平面转R32_FLOAT，但我们已有D3D11规范化 |
| 曝光 | parameter块有ExposureTexture和PreExposure入口，代码对缺失/非法值有默认回退；不能用回退掩盖AC7未知项 | SettingsData支持auto_exposure，仍需决定实际契约 | 可读取游戏曝光/测光回退，但不解决AC7 preExposure未知 |
| FG | 存在完整FG接线参考，但camera/FOV/near/far/HUD默认或启发式不是AC7证据 | 不能仅凭已有SR宣称完整FG | 本身不补FG，与原生FG组合尚非系统验收 |

### OptiScaler：下一项验证对象，不是已选后端

固定commit`6125a75b37f7e2a408e31934f4827e5660429203`：

- [IFeature_Dx11wDx12.cpp:110](../../research/OptiScaler-DLSSNR-PreSR-Multipass/OptiScaler/upscalers/IFeature_Dx11wDx12.cpp)通过PrepareUpscalerResources处理Color/MV/Depth/Output（可选Exposure），维护资源缓存与allocator fence，再用CopyUpscalerOutputToDx11回写。Evaluate时参数暂换成D3D12资源，随后恢复为D3D11指针，null可选输入也恢复。
- [IFeature_Dx12.cpp:128](../../research/OptiScaler-DLSSNR-PreSR-Multipass/OptiScaler/upscalers/IFeature_Dx12.cpp)里的nrBeforeUpscale条件本身没有`!interop`限制，调用PrepareDlssNrInput后替换SR的Color；反之denoiseFirst专用路径有`!interop`。不能把这两种schedule混淆，亦不能从条件可达就宣称D3D11实测通过。
- [DlssNr_Pipeline_Dx12.cpp](../../research/OptiScaler-DLSSNR-PreSR-Multipass/OptiScaler/dlssnr/DlssNr_Pipeline_Dx12.cpp)明确读取render/output、MV scale、reset、frame time和曝光；非零color/output原点被拒绝，说明我们的中央有效区域需先压到原点0。
- [dlssnr_spatial.hlsl](../../research/OptiScaler-DLSSNR-PreSR-Multipass/OptiScaler/shaders/dlssnr/precompile/dlssnr_spatial.hlsl)在guide pack中先将MV转为native像素位移，再算`Pack(p+motion)-Pack(p)`，保留非线性端点几何，不能用统一scale替代。它的正负方向假设必须和AC7规范化字段对齐后才可复用。
- FG输入实现存在配置cameraNear/Far/FOV回退；AC7无限远反向Z候选不能靠伪造有限far来标记通过。

现有文件与接口较接近目标链，优先核实**已有资源桥的独立可复用边界、状态/资源寿命，以及前置NR能否在真实D3D11输入上工作**。本轮只读核对，没有构建/运行整个OptiScaler、NGX或NR，不改变此前“选型待定”。GPLv3仍是实际代码复用边界。

Luma固定commit`dc471fa...`的SuperResolutionImpl接收D3D11资源，适合比较SR接口成本；通用UE main.cpp在该版本明确`sr_render_resolution_scale=1.0f // DLAA only`。Overglaze的MIT模块价值见[专项报告](AC7-Overglaze-Reference.md)。若最终只复用其中小模块，应记录来源/许可，不把全部宿主一起移入。

## 后续门槛与资源桥的条件

D3D12 NR已完成真实单帧SDR求值及CPU HDR准备/恢复参考，见页首；仍缺GPU颜色pass、原后处理回接、时域及动态曝光。核实这些条件和SR/FG运行时、具体复用范围后，再进行**模型无关的D3D11↔D3D12往返**：传原点0的有效场景/规范化深度/速度，D3D12上只做恒等处理，回写D3D11并比较数值和最终图像。预期是格式、区域、同步/资源寿命和HUD均不变；不得只以API返回成功判定通过。

若现有桥紧耦合到全局配置/feature等，先准确列出所需窄接口，不为了“复用”拉入整个注入/菜单/FG系统。若模型无关往返失败，保留具体格式/API/同步错误再修，不启用模型覆盖故障。

在调用NR/SR前，仍需相邻帧方向/jitter、曝光/pre-exposure、镜头/重置信号、透明动态覆盖的证据。当前AA后同尺寸旁路不是Pre-SR插入证明；真正降低场景渲染尺寸后，高分辨率SR结果接回哪一级HDR后处理及其rect/常量更新仍是关键门槛。下一次需要用户补采时，会给出单一明确场景和预期变化，当前不要求立刻重抓。

本轮未改游戏、驱动、补丁或原始捕获，未调用模型，未提交。所有新增结果在忽略的artifacts下，源码检查与实际GPU读回明确分开。
