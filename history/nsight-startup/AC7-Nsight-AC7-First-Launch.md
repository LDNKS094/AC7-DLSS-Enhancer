# Nsight AC7 无 mod 首测

> 历史记录：Nsight启动资格与首帧导出阶段，非当前操作计划。后续两份C++ Capture、GPU旁路及正式NR查询已完成；当前状态以 [项目README](../../README.md) 和 [API核对](../../docs/AC7-Graphics-API-Support.md) 为准。下文进程ID、等待导出和恢复步骤均为当时现场。

最新：用户已在实际飞行保持焦点按F11成功捕获，可见事件列表和飞行画面。下一步保留当前Live Analysis会话，按 [第一帧现场记录](AC7-Nsight-First-Flight.md)保存截图/事件状态，再查资源语义；尚未导出独立可重放AC7捕获。

## 首启动结果：已通过菜单阶段

用户确认已进入菜单，Nsight中有连接/数据。助手检查实际AC7 PID132328，父进程为Nsight UI PID64592，参数为`-windowed`，窗口Responding=true。模块含安装目录2025.5的`Nomad.Injection.dll`与`Nvda.Graphics.Interception.dll`；未在模块列表发现RenderDoc、UE4SS、ASI或D3D SDK层。DINPUT8/dwmapi均来自Windows系统目录。

这证明Nsight路径已越过此前RenderDoc失败的启动阶段，不等于已取得可分析飞行帧，也不能反推RenderDoc故障根因。下一步保持同一会话进入实际飞行，Pause and Capture Frame，确认事件和资源可读；保留捕获会话，不立即退出或恢复mod。


## 前置资格

用户已确认 Nsight Graphics 2025.5.0 下最小 D3D11 三角形启动、Pause and Capture Frame 及前述事件/资源检查操作无问题。此结论来自用户实际操作报告，助手未导出或重放该 Nsight 捕获，不将它记为已离线验证。

GPU Performance Counters Unavailable / ERR_NVGPUCTRPERM 提示只列 Shader Profiler 和 GPU Trace 不可用。基础 Frame Debugger 测试已通过，首轮不为此提升权限或修改驱动权限。

助手检查时 UI PID64592、三角形 PID100516；未找到 Steam/AC7/ProcDump/DebugViewCLI 进程。游戏根目录仅 Ace7Game.exe、Engine、Game 和内容为502500的临时steam_appid.txt，额外 mod 仍隔离。

## 下一步：先验证启动到菜单，不立即抓飞行帧

1. 在 Nsight 结束当前三角形调试会话，通过工具的结束/Terminate操作关闭这个测试目标。不要将旧目标连接当成新游戏连接。
2. 正常启动 Steam 并登录拥有 AC7 的账号，保持客户端运行。不要先从 Steam 点击游玩。
3. Nsight Start Activity 选择 **Frame Debugger**，Launch 页填写：

   ```text
   Application Executable:
   F:\SteamLibrary\steamapps\common\ACE COMBAT 7\Ace7Game.exe

   Working Directory:
   F:\SteamLibrary\steamapps\common\ACE COMBAT 7

   Command Line Arguments:
   -windowed

   Automatically Connect:
   Yes
   ```

4. 高级选项保持通过三角形首测的默认状态，不开启 D3D debug runtime、不运行旧 RenderDoc/验证脚本、不加720p参数。UI切到General或D3D标签不等于更换API，实际API由游戏创建。
5. 点击 Launch Frame Debugger。如果再次出现 Interfering Processes 且包含 Steam，本轮选 **Keep** 保留所需的已登录客户端；不点Terminate杀掉Steam，也不同时改为注入Steam。Keep不是兼容性保证，记录这项启动条件；若失败先保留原错误，不来回切换Keep/Terminate凑成功。
6. 性能计数器权限提示仍可点OK，不影响本轮既定目标。
7. 先观察能否越过闪屏进入菜单，并保持游戏运行。向助手报告结果，助手核对实际AC7 PID、Nsight配套模块、无RenderDoc及无额外mod，之后才指导飞行捕获。

## 飞行帧捕获：保持游戏焦点，按 F11

用户确认 AC7 失去焦点会暂停，因此不通过切出游戏点击主机按钮抓首帧。只读检查本机 `%APPDATA%/NVIDIA Corporation/NVIDIA Nsight Graphics.ini` 的热键设置为 `Capture=F11`、`CycleHUDPosition=Shift+F11`。2025.5官方HUD文档支持使用目标应用捕获热键进入Frame Debugger。

保持实际飞行和游戏焦点，按一次F11触发捕获（笔记本按键若优先媒体功能需Fn+F11），等工具完成暂停/帧捕获后再切回Nsight。用户界面按钮实际名为 **Capture for Live Analysis**，此前“Pause and Capture Frame”来自文档措辞，不应要求找到完全同名按钮。捕获主动冻结目标是正常的；应避免的是捕获前因切换焦点而进入游戏自身暂停状态。

若没有反应，不反复按键；检查HUD或保留现场，核对是否有按键冲突。以上只读检查，未修改游戏失焦暂停或热键配置。

## 成功与停止条件

- 进菜单且Nsight连接正确：通过启动阶段，尚不等于飞行帧合格。
- 同类0x887A0020或其他Fatal error：保留弹窗复制文本及Nsight Output Messages，记录一次后停止，不立即改驱动、权限或安装新工具。
- Steam error/转交导致连接丢失：先核对最终PID，不能把启动握手当成捕获成功，也不晚注入已初始化的图形进程。
- 明确API不支持/Frame Debugger不可用：核对实际活动与版本后停止，不切换D3D12/Vulkan冒充D3D11成功。

后续若完成启动，再选择实际可控制飞机的场景，捕获并检查颜色/深度/速度候选及pass顺序；没有完成之前保持后端选型开放。mod备份和恢复清单不变，临时App ID测试结束后核对内容再移除。
