# 最小游戏测试路径

> 此文保留为此前ReShade中性原型提案，**当前暂停实施**。用户已决定先比较OptiScaler薄适配与直接NGX自研接入，再确定宿主；完整自研FG暂不考虑。以[最新路线决策](AC7-Integration-Route-Decision.md)和[PLAN](../PLAN.md)为准。下方“第一原型选择”不再是当前实施授权。

目标先取得一次固定尺寸、可关闭、出错保原图的游戏中性回接测试，随后逐项启用NR与同尺寸SR。动态尺寸重建、超分、超宽周边压缩与FG不作为第一次中性测试的前置。

## 第一原型的宿主选择

采用已有ReShade 6.8公开API20 addon入口做中性回接原型。理由是现有诊断插件已在真实游戏完成加载与8帧采样，准确shader签名、D3D11 native device/context和候选场景入口均已取得。无需先增加新的加载器或交换链拦截层。

这项选择只覆盖第一原型的加载、回调与中性写回，不选定完整NR/SR/FG宿主，不把采样addon宣布为模型后端。OptiScaler/Luma/Magpie等既有候选仍保留，后续按真实模型时序与FG条件决定。

## 已可复用的入口

父会话核对 [capture_addon.cpp](../tools/capture-addon/capture_addon.cpp)：

- init/bind pipeline识别精确DXBC size+SHA256，再用PSGetShader核native shader；不使用捕获draw编号作运行时hook。
- draw/draw_indexed回调在原draw之前执行，返回false让原draw继续；候选prewarp颜色是PS SRV0，OM DSV资源、PS CB1/View与viewport配对此前的guide候选。
- guide入口读取PS SRV0深度、SRV3物体MV和CB1 View；最多16组候选并严格匹配，不借用其他视图数据。
- present可取得底层D3D11设备与即时上下文，已有proxy/native identity校验经验；真实运行时仍要复用并落实身份核验。
- 原tonemap入口的VS SRV0可取得曝光，但发生在prewarp之后。采样能事后配对，不等于同帧prewarp模型调用当时已能获得该曝光。

[HdrProxy](../src/d3d11/hdr_proxy.cpp)已用SwapDeviceContextState保护内部compute绑定；独立Convert和整个驱动尚无完整游戏状态保护，不能原样放入原draw前回调。

## 接下来实现顺序

1. **中性回接addon**：单独构建、默认关闭；不加载NGX、不改View/jitter。固定支持当前5120×1440分配、中央2560×1440有效区域。精确签名和资源检查通过后，在原warp draw前做私有GPU复制/中性返回，保持区域外、alpha和原draw不变；不匹配即保原图并停用该次处理。
2. **独立宿主控制验证**：合成D3D11应用验证绑定状态恢复、输入/输出别名防护、copy范围、重复draw/其他view、错尺寸、关闭及资源退休。不得使用未隔离AC7派生重放，也不将测试EXE改名为Ace7Game。
3. **第一次游戏测试**：准备可审阅的包与默认关闭/启用/停用说明后，由用户按已约定方式部署和操作真实游戏。先核加载与原图旁路，再短时开启中性回接；此时不宣称游戏NR/SR启用。
4. **模型接入**：提取固定会话的非致命错误路径/模块生命周期，补同帧View字段与曝光GPU取得方案、全程D3D11状态保护、R11场景写回及区域检查，然后分别NR、NR→SR。

## 模型接入必须解决的时序

当前单步驱动靠宿主重复调用推进NR完成、GPU HDR恢复与SR提交。prewarp draw回调返回后，原warp可能立刻消费scene；在present或下一帧才取得模型结果，已经错过该帧写回点。

因此中性回接成功不代表模型时序已解决。后续需要证明同一帧在原warp消费前可完成整链：评估严格有界的同回调等待或更合适的GPU/回调分段调度，按实际开销选择；任何超时保原图，禁止迟到结果写到下一帧。当前没有足够证据预定方案或FPS预算。

曝光同样不能直接拿下一阶段/上一帧值冒充当前值。可先复用已有资源生产链追踪可用时间与native格式，在不改模型/引擎的窄范围内解决。不重新要求用户盲抓帧；P1假设仍按已授权延后，不混淆为曝光纹理可省略。

## 安全停用与暂缓

- 默认关闭，错尺寸/错格式/缺资源/设备不支持时保持原图，不强行继续模型。
- 停止后不发布迟到结果，GPU资源到退休才释放；不把工具中的ExitProcess、阻塞析构或文件诊断带入游戏。
- 暂不支持resize与热卸载时必须明确限制并安全停用，不能把实际device-loss清理宣布已完成。
- 不修改游戏/NVIDIA偏好，不自动部署/启动，不重试Nsight Next Frame/未隔离重放；不新增用户采样要求。

证据：[固定会话所有权](AC7-Fixed-Runtime-Ownership-Validation.md)、[错误停用](AC7-Chain-Error-Stop-Validation.md)、[单步驱动](AC7-Nonblocking-Chain-Validation.md)。只读Luna入口摘要保留于[scout报告](../artifacts/research/fixed-owner-v1/minimal-game-host-entry.md)，以上宿主范围与优先级由父会话核源后决定。
