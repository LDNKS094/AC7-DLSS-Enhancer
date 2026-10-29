# D3D12 HDR代理与恢复等价验证

> 后续[公开NR与同D3D12整链](AC7-OptiScaler-GPU-Chain-Validation.md)已通过，80阶段逐位、64输入保持及完成后清理/debug0；本报告保留颜色单独资格，不代表原始D11输入桥或在途停止已接入。

结论：直接复用原D3D11 HLSL，在D3D12完成Prepare、Restore与R11→FP16转换。真实八帧的中性/实际NR样本两种模式共48次阶段对照，均与本轮D3D11执行及原有文件基线逐位一致；最终40份正常raw和1份非法模型控制输出由独立检查器再核。32次源输入读回不变，GPU-based validation开启，双API debug0，完成后释放正常。

本轮只执行颜色shader，**NR0/SR0**。非中性使用之前已取得的真实NR输出，不是本轮运行NR；OptiScaler累计模型计数仍NR0/SR21。路线1/2仍保留，没有游戏部署或新提交。

## 格式勘误

上轮源码核对报告把本项目代理误写为R8。实际`HdrProxy`创建的prepared纹理、原八帧NR输入和本轮D3D12代理均为 **RGBA16F，DXGI10/bpp8**；R11恢复输出为DXGI26/bpp4。原HLSL与既有数据未因此更改。

“相对实际量化代理恢复”指FP16量化。OptiScaler此次FP16输入下的内建proxy也继承FP16，因此格式不是二者差异；ACES/2.2与SoftKnee/sRGB、恢复/默认MaxRatio等政策仍有差别。这是合同差异，不是画质优劣结论。早期证据中的source-review.json保留原样，见本轮format-erratum.json更正。

## 最小实现与数据路径

新增[HdrProxy12](../src/d3d12/hdr_proxy.h)及[实现](../src/d3d12/hdr_proxy.cpp)，保持原[src/d3d11/hdr_proxy.hlsl](../src/d3d11/hdr_proxy.hlsl)不变、相同cs_5_0严格编译选项：

原始5120×1440 R11 HDR（有效rect1280,0,2560,1440）＋真实曝光 → FP16代理 → 外部NR结果 → R11恢复 → 线性FP16颜色。

- `prepare`与`restore`使用不重叠的描述符槽，可在同一DIRECT命令链中分开调用；调用方可在两者之间插入模型。分段调用本身不要求CPU等待或图像回读。
- 输出进出COMMON，外部HDR/NR输入保持NON_PIXEL_SHADER_RESOURCE；调用方对模型访问和读回自行正确转换状态。
- 校验同设备、格式、尺寸、区域、曝光、输出别名，以及prepare/restore的源对象/区域/曝光配对；拒绝重复未完成的prepare。原HDR内容须保留到恢复和GPU退休，COM指针存活不能替代内容快照。
- 一帧一位调用方独占；上一帧GPU必须退休后才复用描述符与scratch，模块不自建调度器或声称已接入原deadline协议。对象不可复制。
- R11→FP16仅搬运原线性值并写alpha1；未增加tonemap、ratio限制或NR强度调整。

测试工具[color-interface](../tools/optiscaler/color_probe.cpp)独立创建同适配器D3D11 FL11.0与D3D12设备，无NGX loader或OptiScaler DLL。文件仅初始化源和已有NR样本、读取最终证据；prepare→restore→转换之间无CPU图像反馈。

## 最终控制矩阵

| 控制 | 结果 |
|---|---|
| 真实698–705，每帧中性与非中性各3阶段 | 48次阶段逐位一致 |
| 每帧proxy共用，仅保存一次；中性/非中性R11及FP16各存一份 | 40份正常raw独立核对 |
| 中性R11对原始有效区；中性FP16对原CPU展开参考 | 全8帧逐位 |
| 非中性R11对原gpu-restore有效区；FP16对原sr-input | 全8帧逐位 |
| 原始完整HDR与NR输入GPU读回 | 32次逐位，含HDR区域外 |
| null list、非有限/零曝光、越界rect、错误neural格式、输出别名 | 6项拒绝 |
| 未prepare先restore、错曝光restore、重复未完成prepare | 3项拒绝，随后正常分段流程仍通过 |
| NR的NaN、负值、>1三个RGB像素 | 对应原R11像素逐位保留，其余输出对原非中性参考逐位 |
| GPU-based validation与两API info queue | 错误0，设备正常 |
| 已存在输出目录 | 设备创建前拒绝，证据未覆盖 |

首版run-v1已通过；为支持中间插NR，将接口分段并加入配对控制后run-v2重新通过。两版均保留。初次编译的ComPtr取地址重载错误及修复后的build日志也保留；失败编译没有GPU执行。

## 证据、使用与下一步

- [证据目录](../artifacts/research/d3d12-color-v1)：run-v1/run-v2、对应exe/退出/日志、源审计、编译及阶段指纹。
- [最终独立检查](../artifacts/research/d3d12-color-v1/independent-checks.json)，由[check_color.py](../tools/optiscaler/check_color.py)核原采样provenance与全部输出；最终run-v2共958464939字节、42文件（41raw＋checks）。检查器负对照仅改frame2/restored.raw一个最低位，复制日志仍被完整字节比较拒绝，未生成成功报告，也未运行GPU。
- [工具说明](../tools/optiscaler/README.md)包含构建及运行命令。所有产物拒绝覆盖，原始采样和模型/参考源码不改。

下一步将实际NR插入这套prepare/restore，再连接已验证的公开D3D12 SR入口，检查同一D3D12队列、历史/Reset与退休，比较路线1/2所需接口与同步；不再把颜色恢复放回D3D11作为默认结构。NR输入须用原始prewarp HDR，不将此前已NR的SR fixture重复处理。P校准仍延后。

当前是颜色模块等价通过，不是OptiScaler内建NR被替换、全链已通或速度更快。尚未验证跨API原场景转交、与模型同命令链运行、在途停止、设备失效/resize、游戏状态、真正超分/FG/周边压缩或画质。独立工具含阻塞GPU等待、读回和ExitProcess保护，不能复制进游戏；模块本身仅录制命令。游戏/NVIDIA配置哈希和22个AC7登记路径复核不变，提交仍81021a0。
