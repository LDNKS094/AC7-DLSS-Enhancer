# OptiScaler 公开D3D12持久SR八帧验证

结论：自有D3D11 NT共享桥＋标准OptiScaler公开D3D12入口，在FL11.0输入设备上完成真实八帧历史与逐帧Reset两组测试。16份输出分别与已合格的直接NGX基线全文件逐位一致，64次原输入读回不变，双API清理后debug0。每组只创建一个特性和一套共享桥，完成后正常Release/Destroy/Shutdown12。

本轮实际 **NR0/SR16**；加上[首轮接口验证](AC7-OptiScaler-Interface-Validation.md)的SR5，OptiScaler累计NR0/SR21。输入来自之前已完成NR/HDR恢复的真实数据，不能把它写成OptiScaler已运行NR或整链已接通。路线1/2继续保留。

## 输入、控制与结果

- 原输入复用`live-nr-sr-eight/sr-input`，对sequence-contract的40个输入文件SHA逐一核对。
- 独立核对原采样View：真实计数698–705、八个实际dt、七段双时钟连续链和实际jitter0。没有重复单帧或伪造jitter。
- 同尺寸2560×1440 RGBA16F HDR/DLAA，真实depth/MV/exposure，P1未校准；保留之前诊断合同。
- 标准v0.8.10包、SR310.9.16.0与禁NR/FG/overlay/更新/插件的配置沿用首轮。发布DLL未改，参考源码未改；本轮不构建OptiScaler源码或初始化其子模块。

| 组 | Reset序列 | 特性创建/求值 | 原输入保持 | 对直接NGX逐位 | 完成后清理 |
|---|---|---|---|---|---|
| history | 1,0,0,0,0,0,0,0 | 1 / 8 | 32 / 32 | 8 / 8 | Release12/Destroy12/Shutdown12成功，双debug0 |
| reset | 1,1,1,1,1,1,1,1 | 1 / 8 | 32 / 32 | 8 / 8 | 同上 |

每组内通过日志核对同一特性、同一共享color/output资源地址；其余共享资源由同一个桥持有，输入D3D11纹理也未重建，后续帧仅UpdateSubresource。每帧用NaN重填共享输出作为未执行哨兵，所有RGBA分量均检查有限。首帧两组输出一致，后续存在历史/Reset差异；这确认公开入口确有跨帧状态效果，不是每帧隐式重建或始终只用单帧。

真实dt毫秒：17.4779891968、15.9268379211、16.6687965393、32.8516960144、88.5906219482、23.5915184021、90.1775360107、18.3629989624。采样停顿未改写成固定帧率。

负对照移除末帧frame-time.bin：程序在创建设备、加载OptiScaler DLL和创建输出目录之前拒绝，0次模型求值。检查器还以对调history/reset参数确认错误Reset标签会被拒绝，并对复制日志的负对照fixture修改frame3输出一个最低位，确认全raw基线不符会被拒绝；都不生成成功报告，负对照未运行GPU。

## NR颜色合同的源码核对

固定参考提交仍为`6125a75b37f7e2a408e31934f4827e5660429203`。本段是源码事实，不是发布包NR执行结果。**格式勘误**：本项目HdrProxy和八帧NR输入始终为RGBA16F（DXGI10/bpp8）；早版将其写为R8，属于文档误写，已修正。FP16格式不是两者的差异。后续[D3D12颜色等价验证](AC7-D3D12-Color-Validation.md)已通过同HLSL双参考对照。

| 环节 | 本项目既有合格路径 | OptiScaler内建路径（此次FP16输入情形） |
|---|---|---|
| SDR代理 | ACES拟合曲线＋pow(1/2.2)，RGBA16F实际量化代理 | 默认SoftKnee＋分段sRGB；也有Neutwo/Hybrid，proxy继承FP16格式 |
| 曝光 | 已记录的实际exposure与P1诊断合同 | WhitePointScale/WhitePointSource可选手动、游戏曝光或自测量，具体白点需另验 |
| HDR恢复 | 相对于实际量化代理的编辑差分，已有neutral/native格式逐位验证 | 白点归一化、亮度比和颜色组合；默认MaxRatio2在compose模式限制明暗比例 |
| 替换模式 | 没有在本轮改策略 | ReversibleMode2/4直接逆变换模型答案，绕过前面的ratio结果；会改变代理/恢复含义，不能当作原合同等价开关 |

源码位置：`Config.h:333–347,387–397`定义默认值；`DlssNr_Dx12_Encode.cpp:96–170,250–275`接曝光与resolve参数；`DlssNr_Dx12_Models.cpp:14,55–57`建立格式；`dlssnr.hlsl:268–288,755–770,950–978`给出代理、guard和replace覆盖。项目对照为[src/d3d11/hdr_proxy.hlsl](../src/d3d11/hdr_proxy.hlsl)。引用源码的SHA记录在证据中的source-review.json。

两种政策的画质优劣仍待实际NR与可见对照，现有基线只负责等价控制。后续NR输入必须从原始prewarp HDR准备，不能把此次已经经过NR的SR fixture再当作原始场景。

没有找到能通过现有INI直接等价为“ACES＋2.2＋FP16实际量化＋现有差分恢复”的组合。因此下一步先验证D3D12中的颜色通路与既有代理/恢复合同，再接NR或比较另一套明确标记的候选策略。不会把MaxRatio默认2、换白点或切replace模式悄悄当成已验收的AC7政策，也不会仅凭NR改变亮度/材质/细节来削弱它。

## 证据与边界

- [证据目录](../artifacts/research/optiscaler-sequence-v1)：两组process/backend日志、exit、raw、缺末帧时间负对照、源预检查及阶段指纹。
- [最终检查](../artifacts/research/optiscaler-sequence-v1/checks-v2.json)：独立核源、实际后端、特性身份、Reset/dt、退出/清理和16份直接NGX基线。
- [执行工具](../tools/optiscaler/probe.cpp)的`--fl11 --native-eight`与`--reset-control`，以及[检查器](../tools/optiscaler/check_sequence.py)。工具不链接NGX loader、不启动AC7。

两组为两个新进程，各持久八帧；不等于同进程重复Init/Shutdown。停止只在最后一帧GPU和读回完成后进行，未验证OptiScaler在途停止/迟到丢弃，不继承自有整链的deadline资格。仍有独立工具的ExitProcess保护、轮询、NaN上传、CPU读回与磁盘输出，不能复制进游戏或拿总耗时作FPS。

未运行OptiScaler NR/FG、周边压缩、真正超分、游戏状态恢复、设备丢失、resize或热卸载；没有启动/部署游戏或运行AC7派生重放。游戏与NVIDIA配置哈希和22个AC7登记路径复核不变；一次提交仍为81021a0，后续验证未追加提交。
