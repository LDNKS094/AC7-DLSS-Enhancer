# OptiScaler 独立接口首轮验证

路线决策已先记录并提交为本地 **`81021a0`**，未push；以下是提交后的新验证，尚未再次提交。

> **后续八帧验证**：[持久历史/Reset报告](AC7-OptiScaler-Sequence-Validation.md)已在FL11.0输入、自有桥＋公开D3D12入口完成16输出逐位/64输入保持/双debug0和完成后清理。本篇保留首轮5次记录；累计OptiScaler NR0/SR21，NR颜色政策、在途停止、FG/性能仍待验。

> **配置勘误与长于30帧测试**：[同通路对照](AC7-Route-Comparison-Validation.md)确认历史OverlayMenu=false是旧菜单模式，不能称overlay已禁用；该模式30帧后有输出状态泄漏。隔离无swapchain测量使用默认true绕开，未修源码。全关log sinks也不保证静默，最终采用LogToFile=true/LogLevel6安装空sink。本报告保留首轮短序列结果和原配置，不覆盖旧证据。

结论：标准发布包可以通过公开NGX导出实际执行D3D12 DLSS SR，同帧输出与直接NGX基线逐位一致。已取得两个限定成功组合：OptiScaler D3D11入口使用FL11.1＋无设备参数Shutdown()；以及**本项目共享桥＋OptiScaler公开D3D12入口**使用FL11.0输入设备，双API debug0并正常清理。其D3D11包装层的FL11.0恢复和Shutdown1问题仍在，不能宣布AC7兼容或选定路线1。

## 来源与构建边界

- 本地参考源码 `6125a75b37f7e2a408e31934f4827e5660429203`，工作区clean，9个子模块未初始化。未改参考源码或安装全套构建依赖。
- 使用同仓库标准预发布 [v0.8.10](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.8.10)，发布元数据的target_commitish与本地提交一致；这不是二进制可复现构建证明。
- 标准ZIP 131,581,067字节，SHA256 `78a8e357b45e849205cb817f79fccc4ccc61dd3251c48e1a428e616159226fec`，与GitHub资产digest一致。未用RTX40 MFG变体、兼容补丁或安装脚本。
- OptiScaler.dll SHA256 `c8c30d1485bfd9747ff0680956e2403b6f8296d5ae6d2da1c1e6cbd0633f9242`；PE静态核出947命名导出，含16个D3D11 NGX导出。不是靠Source.def是否列出名字猜测接口。

原始元数据、包、DLL与日志保存在 [optiscaler-interface-v1](../artifacts/research/optiscaler-interface-v1)。不入Git。

## 实际执行

新增 [独立probe](../tools/optiscaler/probe.cpp) 和 [CMake](../tools/optiscaler/CMakeLists.txt)，只包含公开NGX头，不链接NGX loader。完整路径LoadLibrary加载OptiScaler.dll，经GetProcAddress调用Init/GetCapabilityParameters/Create/Evaluate/Release/Destroy/Shutdown。

测试EXE为 `optiscaler-interface.exe`，不是AC7派生重放；各次运行使用独立目录与INI。明确设置dlss_12，关闭NR、FG、overlay、更新检查、第三方插件、fakenvapi与相关spoofing。发布包本身的进程全局hooks仍会安装，**未把它当作纯独立SDK**；未测试FreeLibrary/热卸载。

输入为已保全八帧链的第0帧SR输入：2560×1440线性RGBA16F HDR、R32 depth、RG16F向后像素MV和R32曝光。它来自既有NR后HDR恢复，不代表本轮执行过NR。使用实际17.477989ms，Reset1、P1未校准、真实jitter0、同尺寸DLAA，无超分/时域验收。

输出预填充half NaN。检查真实结果完全覆盖、RGBA有限、RGB保HDR、四张输入读回不变，再对直接NGX输出整个raw逐位核对。后端日志还明确记录dlss_12、Creating new DLSS w/Dx12 feature、310.9.16、D3D12互操作设备/队列、NgxDltss真实求值与NR disabled，不以Success单独验收。

## 五次测试结果

| 运行 | D3D11条件/关闭入口 | 输出 | 结果 |
|---|---|---|---|
| probe-v1 | 默认创建FL11.0，Shutdown1尚未执行 | 有限/输入不变/对NGX逐位 | 112条UAV错误，拒绝，保留INCOMPLETE |
| probe-fl111 | 显式FL11.1，Shutdown1 | 同上，求值读回后debug0 | 在Shutdown1异常退出，未正常完成 |
| probe-shutdown-base | FL11.1，公开Shutdown() | 同上，求值读回后debug0 | Release/Destroy/Shutdown均Success，进程exit0，完成标记 |
| probe-device-shutdown-trace | FL11.1，Shutdown1＋只观察异常 | 同上，求值读回后debug0 | 复现0xc0000005，拒绝，保留INCOMPLETE |
| probe-native12 | FL11.0输入＋自有共享桥＋公开D3D12入口 | 同上，两API清理后debug0 | Release12/Destroy12/Shutdown12均Success，exit0 |

五份输出全文件SHA256相同：`9eb4fd49115790ffe0ada5e807e55ca6253355da245b35b29cfbd0272e3b312c`，与直接NGX基线逐位一致。每份11,059,200 RGB通道，最大值12408；连同alpha的14,745,600 half分量全部有限。20份输入读回逐位不变。

### FL11.0问题

固定源码 `IFeature_Dx11wDx12.cpp:421/567` 无条件遍历64个UAV槽保存/恢复；FL11.0只支持8个compute UAV槽。实际调试错误明确指出CSSetUnorderedAccessViews对slot8–63的访问非法，112条错误没有忽略。

既有AC7 Nsight捕获的 `Resources00.cpp:48–50` 重建请求为FL11.0，说明此限制与已有游戏证据相关；这不是本轮重新读取真实游戏设备的GetFeatureLevel值。**请求FL11.1只用于隔离测试，不能作为游戏设备兼容性修复。** 未修改OptiScaler源码、驱动或游戏设备创建。

### Shutdown1问题

公开vectored异常观察器不吞异常。复现记录为 `0xc0000005`，地址位于已安装驱动NGX core `_nvngx.dll + 0x44e54`，发生在ReleaseFeature和DestroyParameters返回成功后、Shutdown1未返回时。记录异常模块不证明根因在驱动；OptiScaler的初始化/双API清理顺序与调用合同仍待定位。

另一个公开无设备参数Shutdown()实际完成Success；这不是跳过清理，也不是私有ABI调用。不把它外推为重复初始化、长时间运行或模块热卸载已安全。

## 复用自有桥的公开D3D12入口

最后一项以`--fl11 --native-dx12`运行。保留本项目已合格的D3D11创建NT共享纹理/fence桥，在同适配器创建D3D12设备/queue，由公开D3D12 Init/Create/Evaluate录制SR，最终回D3D11读回。只用这一套桥，未进入OptiScaler的IFeature_Dx11wDx12路径，后端日志确认选择Dx12Upscaler=dlss与310.9.16。

共享输出也预填NaN；D3D12上传仅用于输出哨兵，不用于模型输入。模型输入从D3D11共享纹理传入；所有资源返回COMMON后退休。结果与直接NGX基线逐位一致，D3D11/D3D12调试消息在Release/Destroy/Shutdown12后统计均0。该模式仍有独立测试轮询和读回，不证明游戏同帧时序、完整状态保持或性能。

这证明路线1可继续评估“薄AC7适配＋已验证共享桥＋OptiScaler D3D12后端”，无需先修改其FL11.0 D3D11包装层；尚未实测其中的NR/HDR处理与FG，也不代表已经选择该组合。

## Pre-SR NR合同：本轮仅源码核对

目标分支已有D3D12 `PrepareDlssNrInput`，基于HDR flag/格式、曝光与PreExposure生成代理，NR后resolve再交给SR，结构上可把处理留在同一个D3D12链，减少本项目当前两次跨API往返。

但它不等于本项目已验证的颜色政策：源码提供soft-knee、Neutwo、hybrid与compose/replace模式，默认ReversibleMode0、WhitePointSource0、Transfer1，组合模式默认MaxRatio2并同时约束亮暗比。不能为了接入方便直接套用这些默认值，也不能把NR重建差异当缺陷。应单独验证中性HDR恢复、实际曝光绑定及不额外削弱NR的配置/窄适配范围。

本轮 **NR0/SR5次Evaluate**，每个进程一次Reset求值。两次本地编译问题（缺string头、当前SDK未声明旧Shutdown符号）已按公开源码签名修正并保留日志，均0Evaluate。未执行OptiScaler NR、周边压缩、FG或性能对比。

## 验收与后续

[check_interface.py](../tools/optiscaler/check_interface.py)核对包/DLL摘要、真实导出、退出状态、后端日志、输出与清理标记；初始[checks.json](../artifacts/research/optiscaler-interface-v1/checks.json)保留前四项，最终[checks-v2.json](../artifacts/research/optiscaler-interface-v1/checks-v2.json)加入公开D3D12路径。错把SR输入当目标基线的负对照被拒，未产生成功JSON。

下一步保留路线1/2：

1. 以已通过的**自有D3D11共享桥＋OptiScaler公开D3D12 SR入口**继续验证持久特性/真实序列与停止清理；独立确认OptiScaler自身FL11.0/Shutdown1问题的影响范围，不能宣称已修复。
2. 验证Pre-SR NR的实际调用/输出与HDR政策，检查是否静默旁路，再比较资源交接和改动量。
3. 其后再决定游戏接入，保持路线3完整自研FG暂缓、ReShade专用回接暂停。

本轮无游戏/未隔离AC7重放启动、无插件部署、无游戏/NVIDIA配置写入、无额外提交。尚未证明实时性能、完整状态保持、跨帧历史、真实超分或游戏兼容。
