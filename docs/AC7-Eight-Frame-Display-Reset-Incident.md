# 八帧首次实机失败与显示设置被重放间接覆盖

## 已确认原因

**后续状态：** 用户已关闭NVIDIA App自动优化，主会话只读确认EnableAutomaticApplyOPS=0。下一份58000-130125640八帧实际allocation恢复5120×1440并通过动态连续输入基本资格，说明本次补采没有再受200%分配影响；无需再次补采。详见[动态八帧验证](AC7-Dynamic-Eight-Frame-Validation.md)。该状态不撤销未隔离AC7派生重放的运行限制；本轮仅运行独立资源/NGX工具。

用户报告`F:/SteamLibrary/steamapps/common/ACE COMBAT 7/AC7-eight-frame-75616-128163625`失败，并指出反复启动游戏会回到全屏和屏幕比例200。已读本次failure/metadata/trace/ReShade.log，并将原文件拷贝保全至`artifacts/research/eight-frame-scale-failure/`；原目录不改。

**根因不是应当取消的采样尺寸检查，而是主会话离线重放间接触发NVIDIA App自动优化，修改了真实游戏的共享显示设置。** 先前“没有改游戏文件”的报告对助手直接写文件成立，但未覆盖NVIDIA App观察诊断EXE后的间接改写；以后不能据此声称没有用户配置副作用。

直接证据：[nvidia-direct-evidence.json](../artifacts/research/eight-frame-scale-failure/nvidia-direct-evidence.json)、[NVIDIA原始该重放状态](../artifacts/research/eight-frame-scale-failure/nvidia-replay-state.json)、[自动优化开关](../artifacts/research/eight-frame-scale-failure/nvidia-config.xml)。

| 证据 | 内容 |
| --- | --- |
| NVIDIA App全局配置 | EnableAutomaticApplyOPS=1 |
| 新诊断应用 | localId2013091083 / hex0x77fd550b，`build/sampler-addon-v4-eight-replay/Ace7Game.exe`，shortName为真实AC7 |
| backend.log33474 | 本地时间10/29 01:34:07.826：`Automatic OPS apply succeeded for new app with already known shortname 0x77fd550b(ace_combat_7_skies_unknown)` |
| 同时记录33479 | 新应用从上述项目重放目录经dynamicfp发现，不是真实Steam目录EXE |
| 该应用CurrentACPosition.Original.Values | Display Mode=Windowed Borderless、Scaling=100、Resolution=5120x1440、FXAA High等 |
| 该应用CurrentHistory.Values | Display Mode=Full-screen、Scaling=200、Resolution仍5120x1440，其他High质量项仍High |
| NVIDIA AC7 wrapper | 读取/保存用户AppData的`ACE COMBAT 7/Config/GameUserSettings.ini`，不随重放EXE目录隔离；可见WindowsGraphicsOptions;DrawScale、FullscreenMode等键 |
| 启动/采样 | 随后01:43启动真实游戏、01:44:42采样；metadata记录allocation10240×2880、active rect(2560,0,5120,2880) |

NVIDIA App中共识别22个AC7路径，包括多份先前以Ace7Game.exe运行的派生重放。此事实解释为何助手离线实验之间，用户又遇到显示设置改变；不能将其描述为游戏每次自主重新计算画质。并未对22条记录逐一证明全部历史覆盖，本轮直接钉实最新一次v4重放。

“屏幕比例200”是内部场景渲染比例，实际本次纹理宽高相对此前5120×1440各翻倍，像素量四倍；不是把Windows桌面改为200%的UI缩放。采样View有效rect也翻倍，不仅是保留一个较大空padding。它会使High预设成为Custom，与用户观察一致；这不证明任何Custom都错误。

## 本次采样拒绝正确

[原failure](../artifacts/research/eight-frame-scale-failure/failure.txt)：

```text
Two-frame logical data exceeds 256 MiB before allocation: bytes=943726608 allocation=10240x2880 rect=(2560,0,5120,2880)
```

这里“Two-frame”指两个在途槽的预算，当前目标依然8帧：

- 两槽逻辑943,726,608字节≈900MiB，已超过256MiB；在staging分配前拒绝。
- metadata计划八帧磁盘3,774,906,944字节≈3.52GiB，同样超1GiB限制。
- 0个raw，INCOMPLETE/failure保留；没有跳帧或伪装成功。
- 不提高限制、不压低模型输出、不改资源契约来绕过这次拒绝。当前v4 addon不需要因此换版。

## 配置侧只读排查与停止措施

本轮已保存GameUserSettings.ini/Engine.ini快照。退出后当前GameUserSettings.ini为DrawScale=5、sg.ResolutionQuality=100、FullscreenMode/LastConfirmed/Preferred=1；通用sg值不能单独代表AC7运行时缩放。本轮没有改这些文件。NVIDIA wrapper的枚举可见Scaling50至200对应DrawScale0至15（100对应5），显示模式1对应Windowed Borderless；实际运行仍以新采样metadata为准，不能用退出后保存值推翻此前10240×2880证据。

Steam当前AC7条目无LaunchOptions。只读Luna scout核对已部署UE4SS文本、游戏root配置/触发命令、Ultrawide-Fix源代码，未找到写显示设置；主会话复查sampler/addon/ReShade.ini也无显示配置/Fullscreen/ScreenPercentage调用。[子报告](../artifacts/research/eight-frame-scale-failure/startup-settings-scout.md)保留，workflow`ff0b0744-1cc9-463d-8a75-1d1064efbcc7`、child`a238bcec-e992-48c2-8101-d282382356c3`。首个workflow调用缺脚本校验拒绝、未启动；记录dirty diff/branch/ref后用同协议正确重试，无CLI替代。

**AGENTS已增加硬限制：未确认NVIDIA自动优化关闭或真正配置隔离前，禁止任何离线AC7重放再次启动。** 不能仅改EXE名称就声称不会被识别，不能把原始Nsight C++重放天然当成用户配置隔离环境。以后实验前后须核对本机辅助程序的共享配置副作用。

不自动修改NVIDIA全局自动优化开关（会影响其他游戏），不锁INI只读、不改Steam云存档、不删除NVIDIA缓存、原capture、派生证据或mod，不因此增加新工具链。独立gpu-live-pair/NGX工具不运行游戏本体或同名AC7重放，可继续按原授权做模型分析；本轮没有新运行。

## 用户下一步

目前退出后保存值已经100/无边框，无需重新安装addon或更改显存预算。停止上述诊断重放后，下一次正常Steam启动先确认屏幕比例100、需要的无边框模式已应用，然后照旧用游戏目录的八帧延迟命令正常飞行采样。如果进游戏仍自动变成200/全屏，先不要再采，保留当时配置/日志；不能在没有观察的情况下宣称持续性修复已经验证。

如用户希望从NVIDIA层阻断自动配置覆盖，可在NVIDIA App关闭“自动优化新添加的游戏和应用程序”，但它是全局偏好，本轮仅报告现状，没有代改。主会话停止新的同名重放是已执行的措施，用户重启与新的真实8帧输入验收尚待完成。
