# D3D11↔D3D12共享资源与定点时序验证

## 本轮结果

**后续：** 真实NR/SR已分别绑定共享纹理并回D3D11，最终8+8+8份raw与原文件模型基线逐位，输入/状态/有限值/两边debug0通过。见[共享模型验证](AC7-Shared-Model-Validation.md)。该后续仍不是NR→GPU恢复HDR→SR无文件整链；下方“仅复制/未接模型”描述本页传输原型阶段。

**独立共享资源桥原型通过硬件验证。** 在RTX5080同adapter上，用真实八帧已规范化输入验证D3D11→D3D12→D3D11：32份输入读回逐位、8份颜色输出往返逐位，共用五份固定共享纹理；两边debug0。刻意延迟D3D12队列时poll返回pending，不发布输出；完成时若帧身份已过期则discard，原图逐位保持。

这一步仅验证GPU传输/访问顺序/帧身份回退，不包含模型。既有[八帧模型链](AC7-Live-Eight-NR-SR-Validation.md)仍是文件/staging路径，**尚未将NR/SR接到这条共享资源桥上**，也没有游戏内回接或最终宿主选择。用户不需补采或部署。

## 最小实现

[shared_texture_bridge.h](../src/d3d11/shared_texture_bridge.h)、[shared_texture_bridge.cpp](../src/d3d11/shared_texture_bridge.cpp)，CMake新增ac7_shared_bridge/validate-shared-bridge；无新增依赖。

- 构造时核D3D11/D3D12 adapter LUID、queue所属device和DIRECT类型，要求公开ID3D11Device5/ID3D11DeviceContext4；只使用同adapter。
- 由D3D11创建5份DEFAULT共享纹理：color/output RGBA16F、depth R32F、motion RG16F、exposure1×1 R32F。SHARED|SHARED_NTHANDLE，SRV|UAV；通过IDXGIResource1::CreateSharedHandle→D3D12 OpenSharedHandle打开。每个临时NT句柄在打开后立即CloseHandle。
- 只共享规范化后的模型输入，不让native深度/stencil或原始整幅allocation强行满足共享格式限制；不改原生资源。当前2560×1440共88,473,604逻辑字节≈84.375MiB共享payload，驱动分配/测试readback/游戏内存另计。
- 单一公开shared fence由D3D12创建并D3D11 OpenSharedFence，严格递增交替ownership：D3D11四份CopyResource→Signal odd→Flush；D3D12 queue Wait odd→caller已关闭list中的work→Signal even。caller负责将五纹理返回COMMON。
- `poll(frame,destination)`先查健康/完成值，不进行CPU等待。未完成返回pending，完全不排D3D11对未完成模型工作的Wait/Copy；完成但frame不匹配则discard，不修改destination。
- 只有同frame完成才在D3D11排已经完成的fence Wait和输出CopyResource。下一次upload的Signal位于该复制之后，保护12端不在11读取输出时复用共享资源。
- 一个在途transaction：busy/重复或倒退token/重复submit/错误phase拒绝；全部四个输入通过device/layout校验后才排复制。维度限定非零≤5120×1440，1mip/1array/1sample，D3D11资源device必须与构造入口一致。类不可复制，避免复制状态/ownership。

帧token是调用者持有的单调uint64身份，不等同于可回绕的原生uint32候选计数；正式宿主需定义新session、切镜头及wrap/reset策略。本次698–705只用于独立探针。

## 硬件证据与失败

[validate_shared_bridge.cpp](../tests/d3d11/validate_shared_bridge.cpp)加载保存的八帧SR输入（NR恢复后HDR/depth/motion/exposure），D3D12 list把输入复制到验证readback，并把color复制到output。CPU文件加载和readback仅为测试fixture/断言；11→12→11生产传输本身没有CPU Map/文件交换。

最终成功证据：[hardware-v4/checks.json](../artifacts/research/shared-bridge-v1/hardware-v4/checks.json)、[hardware-v4.log](../artifacts/research/shared-bridge-v1/hardware-v4.log)。

| 检查 | 结果 |
| --- | --- |
| 8真实帧四输入 | 32个D3D12读回逐位 |
| 输出回11 | 8个RGBA16F逐位；五份共享资源identity始终不变 |
| 非完成队列 | 外加未signal CPU gate挡住12队列，poll确定返回pending，busy拒绝复用 |
| 过期帧 | gate放开后用新frame token poll，确定discard，11 destination保持原sentinel逐位 |
| 无效输入/调用 | 缺曝光、错误格式、WARP→NVIDIA不同LUID、重复token/submit、错误destination全拒绝 |
| 原D3D11状态 | 每次预绑定的PS slot7 SRV在upload/poll前后保持同identity |
| 调试/设备 | D3D11/12 error0、两设备健康；模型Evaluate0、游戏/重放启动0 |

最初缺少test的`<string>`显式include导致编译失败，修正并保留build-v1.log。初版尝试D3D12创建shared committed texture再11 OpenSharedResource1，在此硬件/此descriptor组合返回E_INVALIDARG；stage标签和debug日志保留hardware-v1/v2.log。不据此宣称12→11共享普遍不支持。改为11创建NT共享→12打开后hardware-v3通过，追加调用/状态检查后hardware-v4通过。没有增加私有ABI、模型补丁或driver workaround。

一只只读Luna核现有OptiScaler窄文件的queue/fence/resource生命周期，原报告[shared-bridge-contract-scout.md](../artifacts/research/shared-bridge-v1/shared-bridge-contract-scout.md)，workflow21b6a75b-4077-46bd-a803-b09008037146 / child903a3a87-6dfa-43d7-9286-9542a9b90bb6。该范围把实际shared texture创建委托给其他helper，未给出具体flags；主会话不把它当flags证据，依据公开API并用硬件核验，不复制其大型cache/backend架构。

公开依据：[IDXGIResource1 CreateSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle)、[D3D12 OpenSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-opensharedhandle)、[D3D11 OpenSharedFence](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11device5-opensharedfence)、[D3D11 Signal](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-signal)、[D3D11 Wait](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11devicecontext4-wait)、[misc flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag)。Wait文档明确适用11/12互操作且只用于immediate contexts。

## 定点动态检查

新增[analyze_eight_temporal.cjs](../tools/nr/analyze_eight_temporal.cjs)，只分析既有8帧，无新模型求值。每行列顺序：原HDR/NR恢复HDR/NR+SR history/NR+SR reset，统一ACES/gamma诊断变换；八行698–705。固定飞机/海面crop，太阳crop按本帧原始高光中心平移，超图像边界处clamp仅用于显示，未改变原raw或坐标统计。

- [太阳](../artifacts/research/live-nr-sr-eight/temporal-detail-v2/sun-details.png)
- [飞机边缘](../artifacts/research/live-nr-sr-eight/temporal-detail-v2/aircraft-details.png)
- [海面](../artifacts/research/live-nr-sr-eight/temporal-detail-v2/sea-details.png)
- [数值与精确坐标](../artifacts/research/live-nr-sr-eight/temporal-detail-v2/checks.json)

阈值>1000的原始太阳斑质心，用本帧已核GPU向后pixel motion重投影到上一帧，7段误差.0303/.0263/.0680/.0130/.0286/.0720/.0853px；反号对照24.47/25.74/51.68/146.99/38.72/152.53/30.32px。这是一个独立于公式互相复算的实际亮点对应，支持天空/相机回退的方向与尺度，不证明飞机物体motion或透明遮挡真值。

当前原图高光mask外扩2px后，history和reset超过1000的域外像素均0；前帧高光旧足迹、当前input<1且history>16而reset≤1的候选也全部0。该结果限定阈值/区域，不能扩写“全场景零拖影”。主会话查看三组crop，未见太阳复制出第二个亮斑或飞机的大面积位置冻结；飞机材质、阴影、海面反射细节随NR/SR变化仍按模型预期变化对待。静态contact sheet不能验收细小闪烁/混叠/真实显示观感，不用残差均值给画质评分。

## 尚未验证的边界与下一步

- 新桥只做复制；需将公开NR/SR Evaluate真正绑定到这些共享资源，验证真实model写入后11消费/按原proxy恢复、状态回到COMMON、同feature资源持续身份，并与既有文件结果核对。
- bridge不自动重建device、取消已提交GPU工作或重建模型history。caller必须保持bridge、allocator/list和模型资源存活直到retirement；丢弃帧后应在下次模型求值前按实际状态Reset，当前transport不会擅自假装模型历史连续。
- submit要求caller提供所属device的**已关闭且不再修改**的DIRECT list，并正确返回COMMON；API无法查询list是否已关闭，此责任仍由adapter封装/调用者承担。未人为触发TDR/device removal；遇健康/fence错误会拒绝后续操作，但正式宿主的失效资源保留/卸载流程尚未实现。
- pending时保留原图是已验fallback；同帧可用率/等待预算、模型工作何时排入、实时性能均未验收。不能仅凭nonblocking poll和离线测试就称能稳定每帧生效。
- 宿主代理/native device身份、回接原warp/postprocess、颜色写回alpha/UI/rect、跨场景reset、jitter注入、高输出和FG均未因此完成。最终进程/宿主/backend仍未选择，无部署包升级。

本轮无新模型调用、无AC7/重放/Nsight启动、无游戏/NVIDIA配置修改、无提交。P1暂用与jitter0保持；不需用户新采样。
