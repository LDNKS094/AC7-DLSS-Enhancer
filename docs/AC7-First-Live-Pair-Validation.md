# 首次真实两帧：传输成功，模拟时间保持

## 结论

用户提供`F:/SteamLibrary/steamapps/common/ACE COMBAT 7/AC7-two-frame-77324-124692125`。v3已完成真实游戏资源采样，支持两个相邻渲染区间，但两帧模拟时间和相机保持，**不能据此验收动态MV/历史或完整SR时域质量**。本轮不请求继续尝试Nsight，也没有运行NGX。

原始文件和ReShade日志已复制保全到[项目证据目录](../artifacts/captures/live-pair-77324-124692125/)，原游戏目录不修改。原始capture.json的`temporalQualified:false`及`candidateTimeChain:false`保持原样；独立核验见[audit.json](../artifacts/captures/live-pair-77324-124692125/audit.json)。

## 实际数据

| 检查 | 结果与限度 |
| --- | --- |
| 传输完成 | capture.json存在、无INCOMPLETE/failure；2×5资源完整，235,937,808逻辑字节 |
| 资源语义 | allocation5120×1440，rect(1280,0,2560,1440)；HDR26、depth19、MV35、曝光16，View各4096字节；View内部rect/尺寸也与raw和manifest对应 |
| 候选绘制链 | 每个Present区间各guides→prewarp→tonemap，6事件，viewport一致，1个guide候选 |
| 帧号候选 | 393→394，步长1 |
| 独立采样索引候选 | 7→0，模8步长1；**不是帧号模8**（393%8=1、394%8=2），旧995/3样本里的相等仅是单帧巧合 |
| 当前实时时间 | 4.880345821380615→4.8913679122924805秒，差0.011022090911865234秒 |
| 实时时间串接 | B的前实时时间= A当前实时时间，逐float一致 |
| 当前游戏时间 | 两帧均1.0422450304031372秒，无模拟推进证据 |
| 六组矩阵候选链 | A当前→B前帧：TranslatedWorldToClip、WorldToClip、ViewToClip、ClipToView、TranslatedWorldToView、ViewToTranslatedWorld均逐位对应；字段名按已核旧UE4布局，不能外推所有游戏路径 |
| 相机/clip motion | row0–113两帧逐位相同；ClipToPrevClip接近单位阵，最大偏差约5.82e-11 |
| 真实jitter | 两帧row114均(0,0,0,0)，不注入数字伪造采样 |
| 场景颜色 | 有效区75,109像素不同，区外0；说明不是重复读取同一份输出，数值变化不当质量评分 |
| depth/stencil与MV | 两帧完整raw逐位相同；物体编码有效373,027像素，按已知shader公式解码最大约0.00085062像素，>0.1像素为0（仅object路径统计） |
| 曝光 | 两帧均[1.0000017881393433,1]，不在此重启预曝光调查 |

## 为什么candidateTimeChain=false

A当前游戏时间1.0422450304，但B“前帧游戏时间”1.0312231779，残差−0.01102185249秒。不能仅凭这个字段不等就判采集跳帧。

已保存的公开旧UE4参考`old-sceneview-cpp.txt`2468–2469行明确赋值：

```cpp
PrevFrameGameTime = Family->CurrentWorldTime - Family->DeltaWorldTime;
PrevFrameRealTime = Family->CurrentRealTime  - Family->DeltaWorldTime;
```

它不是从上次采样保存GameTime读取。当前两帧的游戏时间保持，而每帧当前减“前帧”的差约等于其真实delta；这与模拟暂停但渲染/实时时间继续的状态相容。该参考是旧UE4修改分支，不是AC7原生源码，故这里只给与实测一致的解释，不证明具体暂停原因。Alt+Tab失焦、暂停菜单等都可能影响模拟；当前没有输入焦点日志，不定因于其中一个。

原manifest采用“game和real两条都串接”这一保守判断，故false。独立审计保留它，同时分别列出counter、real chain、六矩阵链和simulation advance，不改文件把false抹成true。

## 可复查审计与对照

新工具[check-live-pair.cjs](../tools/nsight/check-live-pair.cjs)校验raw头/长度、View尺寸、候选顺序、时钟、矩阵、有效区变化及SHA256。结果标为`adjacentRenderedIntervalsSupported:true`、`simulationAdvanceObserved:false`、`dynamicTemporalQualified:false`，不能只看一个传输成功标志。

同工具审计已有重复重放995/995，得到`adjacentRenderedIntervalsSupported:false`，负对照见[replay-negative-audit.json](../artifacts/captures/live-pair-77324-124692125/replay-negative-audit.json)。初稿错误地强制索引=帧号mod8，被新数据393/7反例拒绝；该假设已删除，同时修正旧单帧审计脚本，不放松计数+1与real/matrix检查。矩阵均静止意味着本样本不能独立校准运动状态下的坐标/重投影方向。

运行示例（拒绝覆盖已存审计）：

```text
node tools/nsight/check-live-pair.cjs INPUT_PAIR_DIRECTORY NEW_AUDIT_JSON
```

## 下一次只更新触发命令

目前不需要换DLL。最新[Request-Two-Frames.cmd](../tools/capture-addon/Request-Two-Frames.cmd)等待5秒再写请求，让用户启动命令后立即回到游戏，再在正常飞行中采两帧。独立命令包`artifacts/packages/ac7-two-frame-flight-trigger/`只有cmd与哈希manifest。启动命令后**不要等窗口关闭再切回游戏**。

在独立目录测试，首次请求在约5256ms后生成，已有请求拒绝且原内容不变，见[触发检查](../artifacts/captures/live-pair-77324-124692125/trigger-checks.json)。该延迟没有更改游戏时间/NGX参数，也不保证焦点/模拟恢复；下一份样本仍核对实际游戏时间/相机/运动。每进程一次的v3限制保留，下一次需重新启动游戏；若遗留请求存在，退出后移走再启动。

先用动态两帧通过资格，再扩展8–16帧。原始两帧已是有效采样工具/静止历史证据，保留，不因模拟保持而删除或称整个采样失败。本轮未改游戏文件、未启动游戏/Nsight/模型、未提交。
