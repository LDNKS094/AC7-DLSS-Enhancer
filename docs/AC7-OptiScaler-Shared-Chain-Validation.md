# OptiScaler 原始 HDR 共享整链与发布退休验证

> **后续同通路比较**：[路线对照](AC7-Route-Comparison-Validation.md)已完成。配置勘误：历史OverlayMenu=false选择旧菜单，并非关overlay；该路径在第30帧后可能使无RT标志的SR输出留COPY_SOURCE。此前本报告八帧资格未触发，不代表长运行兼容。新隔离host默认true/no swapchain绕开，未改Opti源码；最终两路线计时均debug0。累计OptiNR198/SR218。

结论：原始D3D11 R11场景＋规范化depth/MV/exposure经**一套共享桥**进入D3D12，在同一调用方list完成代理→实际NR→HDR恢复→实际SR，再回D3D11私有输出。最终正常八帧40阶段和8份返回输出逐位一致；故障控制通过提交前过期、在途过期、完成后过晚首次轮询、忙帧拒绝、停止拒新帧、双Reset恢复及D3D11最终复制退休。

本阶段保存normal-v1/faults-v1/normal-v2/faults-v2四次硬件模型运行，实际 **NR30/SR30**；OptiScaler累计 **NR47/SR67**。所有目录和编译失败日志保留，没有新增commit，仍81021a0。未启动或部署游戏。

## 数据通路与代码范围

[SharedTextureBridge](../src/d3d11/shared_texture_bridge.h)新增唯一固定ColorInput::ac7NativeHdr：输入0为5120×1440 R11，输出和depth/MV仍2560×1440，exposure1×1。默认FP16布局不变；构造拒绝其他native尺寸，upload在任何复制前拒绝错误颜色输入。原NT-handle、同适配器、11→12 fence和COMMON状态规则保持。

[color-interface](../tools/optiscaler/model_chain.inl)的`--shared-chain`模式先把真实源HDR及已有规范化引导装入D3D11纹理，再由共享桥复制/排序；模型中间数据没有回D3D11或CPU图像回灌。最终输出落在D3D11私有纹理，绝不直接使用游戏scene。

本轮引导来自既有合格fixture，不包含本轮在线运行GuideNormalizer或游戏取数。现有D3D11规范化资格继续有效，但尚未组成游戏回调中的全路径。单进程、同适配器，不代表已选择最终宿主。

## 发布包装层

新增[ChainPublication](../src/d3d12/chain_publication.h)，引用桥与原FrameSubmission，单次poll只推进/查询一次：

- begin携带从本帧GPU准备/record之前起算的同一个绝对deadline；提交前过期不上传、不执行模型。
- gap/drop/busy/stop使下一次成功输入双Reset。调用方record时必须把resetFor(token)用于两模型，begin校验提交声明，错误声明拒绝；不从闭合命令表逆推参数。
- 在途stop/busy调用abandon，不取消GPU、不释放资源。stop后拒新帧，必须正常drain；新包装实例在相同持久模型/桥上强制Reset。
- D3D12退休后若可用，桥仅复制到**私有**D3D11目标，包装层再用D3D11 fence确认复制完成；直到此时才返回published或discarded。调用方必须持有整套模型/allocator/list/颜色资源直至drained，不能在bridge.busy变false时提前销毁。
- deadline在D3D12轮询与D3D11退休之后均检查。若最后复制阶段才超时/停止，私有scratch可能已改变，但返回discarded，禁止消费。不是“迟到后已写游戏图再撤销”。

包装层不等待、不读取图像、不自发启动线程。独立工具为了测试含轮询/1ms sleep、阻塞读回、fence gate、ExitProcess隔离保护和日志，不可复制进游戏。真实API/设备失败后的隔离销毁仍未验；本轮只测健康GPU的超时/停止。

## 最终正常控制

最终normal-v2使用两个持久特性/一套共享资源，帧token为2^33＋index，真实输入698–705、实际dt、P1未校准/jitter0：

- 40份proxy/NR/R11恢复/SR输入/SR输出对既有基线逐位；8份returned11对最终SR逐位。
- 32次D3D12输入快照逐位保持原始数据。32次D3D11源读回核内容：frame2在共享输入copy排队后，对**同四个源对象覆零**，D3D12仍使用原内容，D3D11读回确认覆零；其余帧不变。
- native错误尺寸和把FP16输出当R11源均拒绝，随后同token正常提交不受影响。
- 释放两特性/参数、公开Shutdown12正常，双API debug0。

旧默认FP16 `validate-shared-bridge`已重新编译并实际回归：32基线输入/8输出、16被覆写源快照、pending/completed期限与pre-expired拒绝均通过，无模型求值，debug0。

## 最终故障控制

最终faults-v2保持两特性不重建，只有发布包装实例在停止退休后重建：

| 帧索引 | 控制 | 结果 |
|---|---|---|
| 0 | 正常首帧、错双Reset声明先拒绝 | 正确提交随后通过 |
| 1 | 提交前已过deadline | 0模型调用、无upload，原图回退 |
| 2 | D3D12 gate阻塞到过期 | 仍pending且不能drain；放行后丢弃，D11目标NaN哨兵逐位保持 |
| 3 | 丢帧后恢复 | NR/SR均Reset1；NR、SR输入、SR输出对独立参考逐位 |
| 4 | 持续历史＋D11复制gate | 两Reset0；8次D11退休poll保持pending、禁止销毁，放行后参考逐位 |
| 5 | 整链在途，下一帧busy拒绝；stop再拒新帧 | 资源保留；放行后丢弃，私有目标不变 |
| 6 | drain后替换发布实例，原模型继续保留 | 双Reset1；NR、SR输入、SR输出对独立参考逐位 |
| 7 | 完整D12队列完成后，超过deadline才首次poll | 同token也丢弃，私有目标不变 |

四个原图回退索引1/2/5/7，三个迟到/停止私有目标不变；恢复/历史的6份模型输出及3份SR输入分别对`nonblocking-chain-v1/run-v2/frame{3,4,6}`逐位。最终故障组实际NR7/SR7、28个D12与28个D11输入读回，清理正常/debug0。

200ms/5s是故障与独立工具诊断预算，不是目标帧预算。本次整条NR→SR已一次录制提交，过期会丢弃结果，**不会阻止已提交的SR GPU工作**；若要在NR后根据剩余预算跳过SR，需要另行权衡分段提交与同步成本，不据此声称已省下超时模型计算。

## 证据与后续

- [证据目录](../artifacts/research/optiscaler-shared-chain-v1)：4次运行/退出/backend、两次最终raw、历史exe、源/模型/实现摘要与原FP16 transport回归。
- [独立检查器](../tools/optiscaler/check_shared_chain.py)与[independent-checks.json](../artifacts/research/optiscaler-shared-chain-v1/independent-checks.json)核原采样provenance、全部正常输出、故障Reset/参考/不变目标、实际模型路径与所有运行计数。
- 最终构建/语法/链接/diff及环境指纹见stage-checks.json。负对照更改SR Reset标签被拒，原正常/故障目录不改。

当前可开始在同一数据通路下比较路线1（OptiScaler公开NR转发＋SR）与路线2（直接NGX）的实际接口成本；随后准备默认关闭的固定尺寸游戏中性回接，解决实时host/proxy身份、全状态保持、同帧曝光和原warp消费前完成。性能选择必须同条件、移除读回/磁盘等诊断干扰，不能以旧多次往返链的总时间代替。

仍未验在线guide连接、真正超分、FG/周边压缩、实际设备/API失败、resize、热卸载、游戏画质/帧预算。本轮整链未启GPU-based validation（此前颜色模块单测已开启通过）。配置/NVIDIA autoOPS0和22条AC7登记路径复核不变，无AC7派生重放。
