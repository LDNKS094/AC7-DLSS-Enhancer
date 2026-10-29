# 第二轮实际日志：ReShade代理context身份不匹配

输入保存在[second-game.log](../artifacts/research/game-entry-diagnostics-v1/second-game.log)，原文件AC7NativeNeutral-214596-206609437.log不变。此次共7行，所有记录enabled0/applied0，初始化/双shader捕获正常。

## 明确证据

两条foreign-guide/foreign-prewarp均module=dxgi.dll、ownerContext0/deviceEqual0/identityEqual0、contextType0（immediate）。已只读核对实际游戏dxgi.dll版本信息：ProductName ReShade，6.8.0，FileDescription为crosire的64位post-processing injector，OriginalFilename ReShade64.dll。

首次记录prewarp时：otherDraws952540、ownerDraws0；成功Map截获1038347、ownerMaps0；Update截获1365599、ownerUpdates0；uploads0/observed0。这里不是没有API调用，而是**所有已记录的调用都未命中ASI所保存的owner身份**。hook看到ReShade代理接口，当前实现从返回shader的GetDevice获取owner，两者未归一。不能只删除身份检查、自动接纳foreign context或把任意数据当View。

本轮guide与首次记录prewarp的viewport均0,0,5120,1440，确认实际测试确实全宽；宽屏观察规则已准备，但这次在owner分流之前就被挡住，尚未进入它的View匹配流程。

guide PS CB1为4096字节，首次记录的同hash prewarp draw PS CB1只有512字节。后者不能当4096 View读取；它也可能是同shader的其它pass/残留绑定，所以暂不据此推断所有prewarp都没有View。先隔离代理，再看原生入口与实际绑定，不靠宽松fallback猜View。

## 当前最直接的下一步

用户已明确其它模组兼容后置，且ReShade仅为旧采样诊断备用。现在有具体冲突证据，建议用户可恢复地隔离**唯一文件**：

- 完全退出游戏后，将游戏根目录`dxgi.dll`改名`dxgi.dll.disabled-neutral`，临时停用ReShade注入。
- 保留dinput8.dll（ASI loader）、dwmapi.dll（UE4SS）、宽屏/其它ASI，以及ReShade.ini、shader目录、addon文件。后几项在注入器不加载时无需移动。
- 复用当前本地AC7NativeNeutral.asi，不需要重编译、更换其它DLL或新ZIP；不创建AC7NativeNeutral.enable。
- 正常进入同一飞行场景退出，带回新的AC7NativeNeutral日志。先核ownerDraws/ownerMaps/ownerUpdates是否命中，再核View/CB/全宽观察。
- 恢复ReShade时退出游戏，将该文件改回dxgi.dll。

这只是隔离当前ASI的代理兼容边界，不把ReShade本身判为有bug。不会先做完整代理兼容层或自动改动用户模组；是否仍有CB绑定问题由原生日志决定。

本轮仅分析、只读版本核对、归档和计划更新；无源逻辑/二进制修改、无新GPU/model运行、无部署/游戏启动/文件重命名/ZIP/提交。NR0/SR0，当前提交仍81021a0。实际View/guide语义及全宽主动处理未验。
