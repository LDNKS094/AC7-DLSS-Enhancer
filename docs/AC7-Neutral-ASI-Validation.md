# 中性 ASI 窄 hook：debug 宿主通过，普通设备仍阻塞

> **当前阶段**：[View/guide/loader协议](AC7-View-Identity-ASI-Validation.md)已通过独立八帧/错配/在途恢复，已准备默认关闭观察包；实际游戏启动/主View待用户日志。下文保留首轮ASI失败事实与当时结论。

> **后续已解除该阻塞**：[普通设备复制路径修复](AC7-Neutral-ASI-Ordinary-Fix.md)采用私有deferred command list，普通/debug完整ASI矩阵及核心/替换回归通过。分步确认首次失败在直接copy（尚未fence），底层原因仍未定；本篇保留上轮失败、原binary和当时边界，不再作为当前“普通设备未通过”结论。真实游戏View/guide/token/loader仍待接，未部署。

结论：已实现AC7NativeNeutral.asi，独立宿主真实LoadLibrary/InitializeASI和MinHook安装，在D3D11 **debug设备**完成精确shader登记、中性处理、原DrawIndexed始终继续、状态/像素保持、在途停止与退休。普通设备的ASI路径没有通过，**目前没有可部署游戏包**。

本轮模型NR0/SR0，游戏没有启动、加载ASI或清理模组，提交仍81021a0。用户的“先最直接实现，模组兼容后置”已遵循；失败出现在无其它模组的独立宿主，不归咎现有游戏模组，也不要求先清理它们。

## 最小实现与通过范围

[neutral_asi.cpp](../tools/neutral-asi/neutral_asi.cpp)复用原[NeutralEntry](../src/d3d11/neutral_entry.cpp)，现有MinHook源码由CMake显式路径引入，不下载或修改第三方代码。捕获Frame0Part01确认目标为DrawIndexed(3,6,0)，不使用draw序号作为运行时身份。

- DllMain只DisableThreadLibraryCalls；LoadLibrary本身不装hook。InitializeASI启动一次bootstrap线程，独立设备只用于发现CreatePixelShader/DrawIndexed方法地址。分别发现普通/debug公开入口，已存在的地址不重复hook，线程局部重入保护保证一次外层API不会被两层重复处理。
- CreatePixelShader首先执行原函数，成功且精确bytecode匹配后，使用返回shader的GetDevice创建owner，不假设底层回调this与公开包装设备相同。beforeDraw只在owner的公开immediate context、匹配参数/显式token/启用时执行，原draw无条件继续。
- 初始默认关闭。测试宿主通过AC7NeutralSetFrame显式提供uint64帧token与开关；没有用Draw计数造游戏帧，没有真实游戏toggle/token源，因此尚不能直接让游戏启用。
- AC7NeutralStop停止处理、拒绝新输入，GetStatus单次查询drained，保留在途资源。模块与owner固定到进程退出，不移除运行中的跳板、不FreeLibrary，不宣称热卸载。精确shader被替换/第二owner出现会锁定首次error5并停用，原shader创建和原draw继续，不隐式重建。

当前代码保留的是debug宿主已通过的双入口MinHook版本；两个vtable实验的源码/binary只存证，不混入当前实现。

## 最终 debug 宿主结果

[asi_host.inl](../tests/d3d11/asi_host.inl)被独立[validate-neutral-entry](../tests/d3d11/validate_neutral_entry.cpp)调用；无AC7派生replay。真实捕获pixel shader配合合成全屏VS、零位移warp和实际R11 scene，绘制中央rect。baseline中央像素必须准确等于输入，并检查区域外保留。

- baseline在装hook前执行。装hook后的默认关、启用、重复token拒绝、显式关、停止五份绘制输出与baseline全幅逐位；含baseline共6份相同输出。
- 错viewport时拒绝中性处理，但原draw仍执行；其实际输出与正确viewport不同，作为不是“根本没有绘制”的控制。
- 8次被拦截DrawIndexed全部调用原函数；7次（含baseline、不含两个gate中的额外draw）用pipeline statistics确认IA三顶点/一图元/PSInvocations>0。两次中性处理，两个拒绝；逐次完整getter快照保持。
- fence gate使第二次中性copy和原draw在途，8次状态查询仍未drain；停止拒新/放行后正常退休，stopped draw输出仍相同，原scene全幅逐位保持。
- debug信息error/corruption为0，设备正常。

单独replacement-v1：新建同样bytecode的另一shader对象成功，owner锁定error5停用，不接受新token；三个原draw均执行、绘制/scene不变且正常drain/debug0。它是先前同一debug MinHook逻辑的失败关闭测试，不是自动shader重建验收。

## 失败隔离与准确阻塞

所有失败目录、INCOMPLETE、退出码、原binary/关键实验源码保留于[neutral-asi-v1](../artifacts/research/neutral-asi-v1)：

| 实验 | 结果与解释边界 |
|---|---|
| run-v1/v2 | hook已装但登记失败，v2 status matches0/error6；返回shader的公开设备与底层hook参数不同，改用GetDevice |
| run-v3/v4 | shader登记成功，但底层context检查拒绝/不命中中性处理；原draw未因此省略 |
| run-v5 | 尝试底层Draw转公开context转接，完成查询watchdog退出10；该转接方案废弃 |
| run-v6 | 双公开入口MinHook能处理；合成VS与捕获PS的硬件寄存器不匹配，debug报错。修正VS输出顺序并加强baseline像素断言后重新测 |
| run-v7/debug-final | debug宿主完整矩阵通过，debug-final为最终源码构建回归 |
| release-v1 | 同双入口MinHook在普通设备上启用后的完成查询不结束，watchdog退出10。不能凭debug成功推普通游戏设备成功 |
| core-release-v1/core-active-query-v1 | 不装ASI，普通设备中性核心通过，后者还跨活动pipeline query通过；缩小到拦截路径，不能归咎核心区域copy或活动查询本身 |
| release-v2 | dummy设备发现的公开vtable替换无法满足draw拦截计数，原目标未稳定覆盖 |
| release-v3 | 在实际shader所属context的vtable替换后，首次命中但后续draw未再命中；该入口覆盖不可靠，实验废弃 |

普通设备失败没有触发模型或修改游戏。当前只确定普通设备的Draw拦截路径不合格，尚未证实最终根因在运行时分派、driver或hook库；不patch驱动/NGX/私有ABI，不靠清理其它游戏模组或关闭测试断言宣称修复。

下一步必须先解决**普通设备稳定DrawIndexed拦截**，用同一个真实draw/像素/关闭/停止矩阵通过，再接真实View/guide关联和帧身份，最后准备可审阅游戏包。若常规公开/本地窄hook方法仍不能解决，则需重新评估宿主选择；不自动恢复已暂停的ReShade专用方案，也不直接绕开验证部署。

## 构建与诊断命令

```text
cmake -S . -B build/two-frame-sampler -DAC7_MINHOOK_SOURCE_DIR=EXISTING_MINHOOK_SOURCE_TREE
cmake --build build/two-frame-sampler --config Release --target ac7-neutral-entry validate-neutral-entry
validate-neutral-entry PREWARP_DXBC FRAME_SCENE NEW_OUTPUT ASI_PATH
validate-neutral-entry PREWARP_DXBC FRAME_SCENE NEW_OUTPUT ASI_PATH --no-debug
validate-neutral-entry PREWARP_DXBC FRAME_SCENE NEW_OUTPUT ASI_PATH --shader-replacement
```

测试用LoadLibrary后显式调用导出InitializeASI；没有验证用户现有ASI loader是否在相同时机调用此导出。启动晚于shader创建会漏登记，必须保守不启用。没有自动游戏安装器或全局热键。

[check_neutral_asi.py](../tests/d3d11/check_neutral_asi.py)独立检查最终debug图像、draw/停止控制与普通设备失败保留，报告明确ordinaryDeviceAsiQualified=false/deployableGamePackage=false。[independent-checks.json](../artifacts/research/neutral-asi-v1/independent-checks.json)不能被解读为游戏资格。

当前仍有进程固定模块/owner、串行mutex及仅一个shader/设备的限制；没有CPU/GPU性能验收、模型、FG、resize/device-loss、异常native SEH或多线程hook解除资格。游戏/NVIDIA配置哈希与22个AC7登记路径保持不变，未清模组/部署/启动游戏/新增提交。
