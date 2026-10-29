# 两帧采样诊断包（不是NR/SR/FG安装包）

**v4新增固定8帧有界模式**：升级与操作见[EIGHT-FRAME-README.md](EIGHT-FRAME-README.md)。默认两帧命令仍有效，八帧使用独立`Request-Eight-Frames.cmd`和`AC7-Capture8.request`，一次进程只能选择一种。最多2个在途槽，写完释放，逻辑GPU256MiB/磁盘1GiB约束，不能跳帧。下方v3为两帧历史说明；当前真实8帧等待实机补测，运行时未自动部署。

当前为v3：启动前遗留请求会被归档为`.stale-PID-TICK`，不触发采样。辅助guide绘制按最多16个不同depth/velocity/View/viewport组合收集，按prewarp实际绑定配对，完全相同组合去重，不同velocity的歧义仍拒绝。候选顺序写入最多64事件的`candidates.jsonl`。保持v2两帧总数据量最多256MiB、实际纹理/rect诊断和所有边界校验。升级只需退出游戏后备份并替换`ac7-capture-addon.addon64`，其余3文件不变；启动前移走现存`AC7-Capture.request`，进入正常飞行后才运行一次请求命令，不改画质/分辨率。真实多视图匹配仍待实机验证。

目前只通过合成GPU与已有AC7捕获的离线重放，真实游戏兼容性未知。详情：`docs/AC7-Bounded-Sampler-Validation.md`。

本包使用官方ReShade6.8 Addon作为独立诊断宿主，只有一个AC7采样addon；不加载Luma、不执行NGX、不改原shader、jitter或配置。固定2帧，源数据上限256MiB，成功在同目录新建`AC7-two-frame-PID-TICK`。

实际游戏采样步骤（尚未执行）：

1. 退出游戏。仅在原`Ace7Game.exe`目录没有`dxgi.dll`/ReShade配置和其他addon时，部署包内`dxgi.dll`、`ReShade.ini`、`ac7-capture-addon.addon64`、`Request-Two-Frames.cmd`，保留所有既有dinput8/dwmapi/ASI/ue4ss。任何文件冲突先停止，不覆盖。
2. 正常从Steam启动游戏，不经Nsight，不加载旧Luma实验包。保持现有分辨率、FXAA等设置，进入正常飞行。首次启动宿主或共存加载器可能不兼容；异常就退出并按下面回退。
3. 只运行一次游戏目录内`Request-Two-Frames.cmd`。当前源码触发命令先等待5秒再写请求，**不要等命令窗口关闭，启动后立刻切回飞行窗口**；插件随后自动停止于两个区间，不需要F11/Next Frame。v3原包附带的是早期立即触发版本，最新延迟命令单独位于`artifacts/packages/ac7-two-frame-flight-trigger/`；只需替换cmd，addon保持v3。延迟用于减少失焦暂停采样，不保证游戏模拟已恢复，仍由实际View时间核验。
4. 文件输出后退出游戏，检查新目录：`capture.json`存在且没有`INCOMPLETE`才是传输完成。失败目录保留`INCOMPLETE`和`failure.txt`。传输完成不代表帧相邻；把输出目录及`ReShade.log`交给助手独立核验。
5. 同进程不重试、不重发请求；若没有结果先看日志，不能反复按命令碰运气。绝不同时运行Nsight采集。

回退：退出游戏后，仅移除本包新增的上述4文件和本包生成的`AC7-Capture.request`（若有）。保留采样目录/日志用于调查。ReShade可能生成日志、缓存及配置更新；只清理能确认本轮新增的文件，不批量删除既有目录。任何原有文件都不由本包覆盖。

当前包仅在项目的ignored artifacts内准备，未自动复制到游戏。实际部署前再次核对文件冲突；不要把此README理解为已经部署或已经取得真实时域数据。
