# 固定尺寸会话所有权与关闭验证

新增 `FixedRuntime`，将转换pass、NR/SR模型及其共享桥、单步驱动和私有快照放进同一个资源容器。健康设备上的停止、退休、销毁与特性重建通过独立验证；下一步转向最小中性宿主回接，不以动态resize为首次游戏测试前置。

## 所有权与关闭

实现位于 [gpu_chain.cpp](../tools/nr/gpu_chain.cpp)，仍在独立工具内：

- `Resources`持有D3D11设备/上下文、D3D12设备/queue的COM引用，以及HdrProxy、GuideNormalizer、Convert、NR、SR和驱动。声明顺序保证先销毁驱动，再释放模型与bridge，随后释放pass和设备引用。
- `tryClose()`先stop，再单次推进未退休工作；pending时保留整个容器。只有驱动drained（模型均idle且D3D11退休fence完成）后才reset资源容器。
- 关闭后重复调用返回true，资源访问被拒；无法正常退休的device-teardown状态返回false，要求外层继续持有所有权。
- NGX全局Init/Shutdown仍在外层，必须比所有FixedRuntime活得久；本轮重建模型特性，不重启NGX或GPU设备。
- 析构前未显式关闭仍触发独立工具的ExitProcess保护，**不属于可部署到游戏的析构策略**。本轮统一了正常关闭路径的所有权，没有完成设备失效时的外层保留/清理或游戏模块卸载。后续中性宿主不加载模型，不能直接复制该致命保护。

## 验证

`gpu-chain ... --owned-session-test`使用原真实八帧，并创建4个完整资源会话：

| 样本序号 | 行为 |
|---|---|
| 0 | 首会话完整NR→HDR→SR，7阶段对原文件基线逐位；关闭并重建全部特性/pass |
| 1 | 第二会话NR在途，gate阻塞；16次tryClose均不释放资源 |
| 2 | closing拒绝新输入、保原图；释放gate，NR迟到输出不变，关闭第二会话；创建第三会话 |
| 3、4 | 第三会话Reset1后0；输出对前轮原输入独立参考资格通过的对应基线逐位 |
| 5 | 不支持尺寸输入提交前拒绝、保原图；关闭第三会话并重建同尺寸第四会话 |
| 6 | 第四会话Reset1，恢复输出逐位 |
| 7 | SR在途16次tryClose保留资源；释放gate后迟到输出不变，完成第四会话释放 |

另外用真实WARP上下文与硬件设备不匹配，触发容器构造中途拒绝：两个模型特性已完成创建但还未Evaluate；构造展开正常释放两特性与参数，不留下成功会话。此负向不模拟设备丢失。

最终 [checks.json](../artifacts/research/fixed-owner-v1/checks.json)、[日志](../artifacts/research/fixed-owner-v1/accepted-process.log)：4次会话创建/关闭、32次pending关闭、4完整原图回退、2迟到destination不变、6恢复/继续历史输出逐位；4成功会话加1次部分构造合计每模型5次创建/释放，10次参数销毁，两APIdebug0与NGX正常Shutdown。

测试刻意保留两个destination纹理的额外COM引用，在所有者关闭后检查未被写入；设备/context/NGX仍活着。因此“ownedReferencesReleased”指容器自身引用已释放，不证明全进程GPU资源分配为零。

[check_fixed_owner.cjs](../tools/nr/check_fixed_owner.cjs)核对事件、Reset/64位token、创建释放次数、基线与原图；篡改pending时resourcesRetained标志的日志负对照被拒绝。前轮基线位于nonblocking-chain-v1/run-v2，该模式在Init前检查所需基线存在。

正常无故障八帧回归：[normal-checks.json](../artifacts/research/fixed-owner-v1/normal-checks.json)，56阶段逐位、NR8/SR8/debug0/cleanup。最初run-v1和最终accepted各NR6/SR5，加normal8/SR8，本轮实际 **NR20/SR18 = 38 Evaluate**；部分初始化拒绝仅Create/Release，0Evaluate。最初成功后将关闭日志明确为“释放自有引用”，重新构建执行最终集，旧目录保留。

## 最小游戏验证方向

见[最小游戏测试路径](AC7-Minimal-Game-Test-Path.md)。首个宿主原型选用已有ReShade公开API20回调做**默认关闭、固定尺寸、中性回接**，复用已实测签名/设备/资源入口，不先接FG或修改显示设置；这不是最终NR/SR/FG后端选型。

本轮未新增或部署游戏插件、未启动游戏/AC7派生重放、未写游戏/NVIDIA配置或提交。实际尺寸/设备重建延后，错误状态仍必须安全停用。P1未校准延后、真实jitter0与未隔离重放禁令保持。
