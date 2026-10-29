# 公开 OptiScaler NR 转发与 D3D12 整链验证

> 后续[原始HDR共享整链](AC7-OptiScaler-Shared-Chain-Validation.md)已通过D11→12→11单桥及整链期限/双Reset/停止/D11复制退休，当前Opti累计NR47/SR67。本篇保留此前纯D12文件输入资格和计数；在线guide/游戏回调及生产性能仍未验。

结论：标准v0.8.10发布DLL可通过公开D3D12 NGX入口独立创建/执行/释放NR feature18。自有颜色模块→实际NR→HDR恢复→实际SR，已在同一调用方D3D12队列/命令列表上完成真实八帧历史和SR逐帧Reset对照，两组80份阶段输出全部与直接NGX/原颜色基线逐位一致，64次源输入保持，双API debug0，完成后清理正常。

本轮实际NR17/SR16：独立NR首帧1次＋两组各NR8/SR8；加此前OptiScaler SR21，累计 **NR17/SR37**。没有模型内部补丁、参考源码修改、游戏启动/部署或追加提交；仍81021a0。

## 公开合同与独立NR

固定参考源码`6125a75b37f7e2a408e31934f4827e5660429203`的`inputs/NVNGX_DLSS_Dx12.cpp`：

- GetCapabilityParameters在原生NGX初始化后返回其参数表；CreateFeature对非SR/RR特性走原生转发（784–807）。
- 原生句柄Evaluate直接转发（1174–1188）；Release按原生句柄转发（844–872）。未使用私有ABI或自定义NR导出。
- 实测available1、feature18创建/求值/释放、DestroyParameters及公开无参Shutdown12成功。NaN输出哨兵完全被覆盖，所有RGBA half有限、RGB在[0,1]，四原输入读回不变，输出whole raw与直接NGX首帧逐位。

标准OptiScaler.dll摘要沿用首轮，官方NR/SR均310.9.16.0。测试目录补入原官方NR DLL，核摘要与项目原件一致。INI的`[DlssNr] Enabled=false`关闭OptiScaler内建NR流水线，仅允许此次调用方显式feature18转发；NR模型参数Enabled=1，与INI含义不同。

因此本次复用的是**公开NR转发和SR后端**，不是OptiScaler内建代理/恢复、周边压缩或完整FG栈已经满足AC7。颜色继续使用本项目ACES/2.2/FP16差分政策，不套MaxRatio2。

## 同一D3D12链与控制

原始prewarp HDR（5120×1440 R11，rect1280,0,2560,1440）＋真实曝光＋已规范化depth/MV → HdrProxy12.prepare → NR → HdrProxy12.restore/FP16转换 → SR。

- 源场景从原捕获读取，不使用已经NR的SR颜色fixture当输入。NR/SR共用[configureCapturedEvaluation](../tools/nr/evaluation_parameters.cpp)的原参数；P1未校准、实际jitter0、真实dt保持。
- 每组单次Init、一个NR特性和一个SR特性，各复用八帧；代理、模型输出及引导资源不重建。
- 每帧在同一调用方DIRECT list上录制prepare、NR、restore、SR以及阶段/输入读回复制；一次提交后等待、检查并存证。模型间无CPU图像反馈、文件回灌、D3D11 HDR恢复或额外互操作桥。
- 两模型输出每帧先填NaN，40阶段逐位要求覆盖整份数据。官方NGX内部队列/调度不在本次可见范围，“单队列”只指调用方。

| 组 | NR Reset | SR Reset | 阶段逐位 | 原输入保持 | 完成后清理 |
|---|---|---|---|---|---|
| history-v1 | 1,0,0,0,0,0,0,0 | 同左 | 40/40 | 32/32 | 两Release/两Destroy/Shutdown成功，debug0 |
| sr-reset-v1 | 1,0,0,0,0,0,0,0 | 全1 | 40/40 | 32/32 | 同上 |

五阶段为proxy、NR输出、R11恢复、SR输入和SR输出。两组proxy/NR/恢复/SR输入全帧一致；SR首帧一致、后续有历史差异。独立检查器复核原采样provenance、计数698–705、真实dt/曝光、特性/资源身份、后端原生NR与SR日志及所有raw。

第二组日志记录实际加载的`nvngx_dlssnr.dll`和`nvngx_dlss.dll`完整路径，独立核摘要匹配官方文件。第一组没有此新增路径日志，但backend/结果/相同配置及包文件核验保留；不能把第二组日志称第一组实测路径。

## 工具与证据

- [probe.cpp](../tools/optiscaler/probe.cpp)新增`--native-nr`单帧模式；仍复用自有共享桥，不链接NGX loader。
- [color_probe.cpp](../tools/optiscaler/color_probe.cpp)增加显式DLL参数模式，[model_chain.inl](../tools/optiscaler/model_chain.inl)实现此次诊断链，复用已有固定布局I/O和颜色模块。默认无DLL参数时仍为颜色测试。
- [check_model_chain.py](../tools/optiscaler/check_model_chain.py)独立核输出、Reset/真实参数、配置/模型摘要和清理；[工具说明](../tools/optiscaler/README.md)含命令。
- [独立NR证据](../artifacts/research/optiscaler-independent-nr-v1)和[整链证据](../artifacts/research/optiscaler-gpu-chain-v1)，最终[independent-checks.json](../artifacts/research/optiscaler-gpu-chain-v1/independent-checks.json)。

## 对路线选择的意义与下一步

路线1现在有一个不修改OptiScaler源码的可执行组合：自有AC7适配/共享输入＋自有颜色模块＋公开原生NR转发＋OptiScaler SR。路线2可复用同一颜色模块直接调用NGX；需要在相同数据通路下比较接口、生命周期、复制同步和测量开销，不能用旧多往返链的耗时断言框架快慢。

下一步优先将原始HDR与引导从D3D11转交这套D3D12链、输出回D3D11，并移植既有绝对deadline、迟到丢弃/双Reset、忙帧保原图与在途停止/退休。此文件输入链不能直接当游戏桥，也不替代已有共享桥的单独资格。

只验证完成后清理，未验证在途停止/异常设备状态。整链未开启GPU-based validation（颜色模块上轮单独开启并通过），保留D3D11/D3D12 debug。工具仍有阻塞等待、读回、日志与ExitProcess保护，不能放进游戏。未验真正超分、FG、周边压缩、热卸载、画质或生产帧预算。游戏/NVIDIA配置哈希与22个AC7登记路径复核不变；配置副作用限制继续有效。
