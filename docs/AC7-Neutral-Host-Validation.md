# 独立 D3D11 中性入口与合成宿主验证

> **普通设备后续已通过**：[复制路径修复](AC7-Neutral-ASI-Ordinary-Fix.md)以私有deferred list替代callback内直接copy，普通/debug完整ASI矩阵通过；本篇保留初始直接调用的中性宿主资格。现在应继续真实View/guide/token与loader，不重复旧普通设备阻塞。

> **ASI实测更新**：[ASI验证与阻塞](AC7-Neutral-ASI-Validation.md)已有debug宿主真实加载/hook/原Draw继续/中性/停止资格；普通设备hook未通过，无可部署游戏包。普通无hook核心测试通过，下一步先解决稳定拦截，不以清模组或debug单项成功绕过。

结论：[NeutralEntry](../src/d3d11/neutral_entry.h)已实现默认关闭、无模型的固定尺寸中性入口。在独立D3D11 FL11.0合成宿主中，使用真实prewarp shader字节码和原始场景fixture，全5120×1440 R11场景与中央2560×1440快照逐位一致；管线getter快照、隐藏CS UAV counter及原compute继续执行通过，6个绑定/时序拒绝、8次pending保留、stop/drain/禁止重启通过，D3D11 debug0。

本轮NR0/SR0，没有安装hook、加载ASI或启动游戏。用户最新方向是先按最直接需要实现，不以其它模组兼容为前置；后续确有冲突时再列具体临时清理清单。没有移动、删除或改写任何游戏模组。

## 入口行为

[neutral_entry.cpp](../src/d3d11/neutral_entry.cpp)仅执行D3D11标准操作，不依赖ReShade/NGX/OptiScaler/UE4私有ABI：

- 创建时默认关闭。宿主在真实CreatePixelShader返回后提供**同次创建**的对象和bytecode注册；仅接受628字节/SHA256 af1247e3a473861934047c3cabe849dae6577dc61189bc56ba225f3351294ec0。对象必须属于本设备，启用/在途/停止时不允许重登记。
- 调用beforeDraw前，宿主仍需从真实帧身份提供uint64 token。只接受同设备serialized immediate context、已登记PS对象、固定viewport(1280,0,2560,1440,0,1)、PS SRV0全mip0 R11场景、固定R32G8X24 depth/DSV以及PS CB1的4096-byte View形状。
- 参数不符、非匹配shader、重复token、默认关闭或停止均不写场景。每token最多一次；不使用draw ordinal当身份。
- SwapDeviceContextState切换私有状态，复制有效区到R11 scratch，再原样复制回相同有效区；Signal自己的D11退休fence后立即恢复原state。两次copy之间没有模型、曝光变换或CPU图像回灌。
- 不自行Flush、等待、轮询循环或启动线程；busy时返回pending。stop拒新输入，但仍要求宿主继续驱动context并等待drained。owner和相关对象不得在途销毁。
- Signal失败会停止并抛出，要求外层保留到设备处理；这不是已经验证的device-loss恢复。最终ASI回调必须捕获异常并保原图，不能把独立工具ExitProcess搬进去。

此接口是**形状和绑定检查**，并未检查View内容、camera/main-view语义或与guide候选关联，因此不能以单个shader+格式直接认定实时主场景。registerShader也无法从D3D11 shader对象反查bytecode，真实hook必须保证对象与bytecode来自同次创建。真实游戏接入前还需加入已验证的GuideCandidates/View身份关联。

## 合成宿主控制

[validate_neutral_entry.cpp](../tests/d3d11/validate_neutral_entry.cpp)独立创建设备与资源，不运行AC7或AC7派生replay。

| 控制 | 结果 |
|---|---|
| 默认关闭、错hash、非目标shader | 不应用，不改变绑定 |
| 非immediate context、viewport越界、缺SRV0、缺View、缺DSV、重复token | 6拒绝，拒绝前后状态快照一致 |
| 精确shader＋正确绑定，uint64 token>2^32 | 应用一次，场景全幅逐位一致，scratch为准确中央crop |
| VS/PS/GS/HS/DS/CS shader/class/SRV/sampler/CB与CB range getter | 前后指针/值快照一致 |
| IA layout/VB stride-offset/IB/topology，RS viewport/scissor/state，OM RTV/DSV/UAV/blend/depth，SO targets/predication | getter快照一致 |
| 非空CS UAV COUNTER初始值5 | 中性调用后仍5；原CS Dispatch继续后变6 |
| GPU fence gate阻塞后再应用 | 8次drained=false且下一调用pending，状态恢复保持 |
| 在途stop | 拒新、保留资源；gate放行后drain；enable也不能复活已停止实例 |
| 全部资源与原CS继续执行 | 无D3D11 error/corruption，设备正常 |

快照覆盖所有getter槽但并非填满每一类非空状态；没有实际非空SO offset/隐藏写计数、动态shader class实例或全部UAV隐藏counter资格。测试没有调用真实原warp draw、执行game UI/post或建立游戏hook，因此“原draw总会继续”仍是未来hook实现要求，不是本轮结论。

run-v1通过后，补充deferred拒绝/原CS实际继续/stop不可复活，run-v2再次通过。首版编译缺string头的错误日志保留；没有失败GPU运行或模型调用。

## 直接加载方向与现有安装

只读目录检查发现游戏根已有dxgi.dll、dinput8.dll、dwmapi.dll和多个ASI：AC7HUDCenter、AC7NativeInput、AC7RumbleTrace、AC7SceneProjection等。邻近HUD工程以MinHook接CreatePixelShader，未接Draw/Present；这是源码观察，不是完整现有hook链审计。

依用户最新要求，**下一步优先最直接的ASI入口**，不新增同名DXGI/DInput代理，不把兼容现有全部模组作为门槛；也不自动恢复ReShade专用方案。当前没有清模组的必要证据，不提出泛化卸载或实施删除。若最小ASI确需干净hook，先提供具体文件的临时移出/恢复步骤，保持可恢复，之后再做模组共存。

下一步：ASI窄hook在原CreatePixelShader成功后登记，在原draw前调用，确保原draw无条件继续；独立宿主先验证安装/默认关闭/显式启用/停止、hook生命周期与shader替换。再加入连续帧身份和guide关联，准备用户可审阅的固定尺寸中性包，游戏部署/启动仍由用户操作。尚未确定或实现动态卸载，不把MH_DisableHook当成线程/在途安全证明。

## 证据与运行

```text
cmake --build build/two-frame-sampler --config Release --target validate-neutral-entry
validate-neutral-entry CAPTURED_PREWARP_DXBC ORIGINAL_FRAME_SCENE NEW_OUTPUT
python tests/d3d11/check_neutral_entry.py CAPTURED_PREWARP_DXBC ORIGINAL_FRAME_SCENE RUN_DIR NEW_REPORT
```

[证据目录](../artifacts/research/neutral-host-v1)保留两次运行/原binary、checks、全scene/crop、独立核验与配置/模组只读指纹。独立检查器对每个正常输出重新比较原始字节，并检查debug/退出/停止控制，不将合成状态当成游戏状态。

提交仍81021a0，后续未追加提交；游戏/NVIDIA配置哈希与22个AC7登记路径保持不变。没有模型、FG、真正超分、性能、resize/设备丢失或游戏安全热卸载验收。
