# AC7 RenderDoc 抓帧：公开经验与下一次测试

## 结论和证据级别

用户确认移除 `renderdoc.asi` 后，AC7 可以正常启动。原有加载器与插件保留。这证明本次新增捕获组件与启动失败存在关联，但不确定其内部原因，也不能单凭此判断是 UE4SS、驱动或游戏本身的问题。

本轮以英文游戏名、`Ace7Game`、中文及日文游戏名配合 RenderDoc 检索，没有找到能核实环境、版本和启动步骤的 AC7 专用成功捕获案例。结果中有其他 Ace Combat 作品、模拟器捕获、泛用工具说明等，不当成 AC7 成功证据。搜索覆盖有限，不能据此说无人试过或 AC7 不支持。

App ID 文件配合 RenderDoc 自身 Launch 已实测：PID 80920 保持连接并到达 `D3D11 (Presenting & supported)`，随后进程结束/连接重置，用户报告崩溃。只停用 UE4SS 代理的下一次测试（PID 504）仍失败，已恢复 UE4SS。启动期 ProcDump 监视随后引入不同的 Steam 加载错误（PID 97748，T:0000065432），只有终止转储，没有异常流。监视已结束、临时 App ID 文件已移除。下一项候选诊断及管理员操作文件见 [Silent Process Exit 方案](AC7-RenderDoc-Silent-Exit.md)，尚未启用。

## 启动期异常/终止转储监视记录（已结束）

**结果：用户看到不同的 Steam Application load error T:0000065432；ProcDump 已退出，临时 App ID 文件已移除。以下为此前的准备记录，当前不再等待此项复现。** 转储属于 PID 97748 的 Process termination，没有异常流，未定位原渲染阶段崩溃。

无 UE4SS 的测试 PID 504 在 05:13:42 到达 D3D11 Presenting，05:13:49 连接重置，用户确认仍崩溃。UE4SS.log 修改时间仍为上一轮的 05:08:55，支持该测试未加载 UE4SS。对应日志保存在 `../artifacts/diagnostics/renderdoc-no-ue4ss-failure/`。未找到对应 PID 504 的新系统崩溃记录或转储；不能把连接重置当成访问冲突证明。

已将 `dwmapi.dll.renderdoc-disabled` 改回 `dwmapi.dll`，四个原始加载器/插件的哈希全部匹配。临时 App ID 文件先回退，随后仅为受监视的直接启动复现重新放入。没有继续停用其他插件，没有放回 `renderdoc.asi`。

从微软官方 https://download.sysinternals.com/files/Procdump.zip 下载 ProcDump 12.02 x64，Authenticode 状态 Valid，签名主体 Microsoft Corporation。工具放在忽略的 `artifacts/tools/procdump/`。使用的选项经其本机帮助验证：`-e` 监听未处理异常，`-t` 在进程终止时转储，`-mm` 使用小型转储，`-w` 等待下一次进程启动；未安装全局事后调试器（没有使用 `-i`）。

当前监视 PID 为 120840，已确认在等待 `Ace7Game.exe`。下一步由用户按现有 RenderDoc 配置 Launch 一次，转储目录为 `../artifacts/diagnostics/renderdoc-exit-dump/`。ProcDump 作为调试器可能影响目标行为，分析时要区分未处理异常转储与普通退出转储；后者不自动证明原始故障点。具体状态及取消/回退方式见 [转储监视记录](RenderDoc-Exit-Monitor.json)。

## 单组件隔离测试记录（已失败并恢复）

直接 Launch 的新日志显示，PID 80920 于本地 05:08:51 握手，05:08:55 识别 D3D11 Presenting，05:09:02 连接被重置。日志和 UE4SS.log 快照已保存在 `../artifacts/diagnostics/renderdoc-direct-failure/`。未发现与该 PID 对应的新系统异常记录或转储；旧 UE4SS 崩溃事件不能直接用于归因。

RenderDoc 原安装路径注入时，崩溃辅助进程可成功连接；先前 ASI 副本的 helper error 2 在此处未重现，故修复 helper 路径不足以保证游戏正常运行。UE4SS 的 GuiConsoleEnabled 原本已为 0，不再重复建议关闭 GUI。

当时准备状态（已结束，代理已恢复）：

- 已确认没有游戏进程后，把原 `dwmapi.dll` 重命名为 `dwmapi.dll.renderdoc-disabled`，原字节保留。SHA-256 为 `ea3cdbb755dd61b9f4de70590b78a15974be65ba5fb3837ea2e1799d78d4c116`。
- UE4SS 的 UI 比例修正和遥测在本次测试中暂时不可用；`ue4ss` 文件夹和设置未修改。
- 保留内容为 502500 的临时 `steam_appid.txt`，以保持上一轮的直接启动路径；`renderdoc.asi` 仍不存在。
- `dinput8.dll`、`AC7HUDCenter.asi`、`AC7SceneProjection.asi` 保留，RenderDoc 配置不再改动。
- 下一次仍用相同 `.cap` 配置 Launch，先观察能否进菜单并保持连接。若成功，再抓第一份标记为“无 UE4SS”的诊断飞行帧；若仍失败，保存这次日志后恢复代理，不继续顺带停用更多插件。

回退：退出 AC7，把 `dwmapi.dll.renderdoc-disabled` 改回 `dwmapi.dll`，勿覆盖其他新文件；若结束全部试验，检查内容后删除临时 `steam_appid.txt`。参见 [隔离记录](RenderDoc-UE4SS-Isolation.json)。

## 资料

### Valve 官方：SteamAPI_RestartAppIfNecessary

来源：https://partner.steamgames.com/doc/api/steam_api#SteamAPI_RestartAppIfNecessary

官方说明，若直接启动的程序没有通过 Steam 启动，函数可能通过 `steam://run/<AppId>` 重新启动游戏。若存在 `steam_appid.txt`，该函数返回 false，从而允许在调试器下直接启动。

同页 SteamAPI_Init 文档要求 Steam 客户端运行、登录的账号拥有游戏，且程序和 Steam 处于相同用户/权限上下文。App ID 文件只包含数字；Steam 从当前工作目录查找，因此 RenderDoc 的 Working Directory 需要明确设为游戏目录。

这只能解释标准 Steamworks API 路径，不保证解决 AC7 的所有启动/DRM 行为。

### 游戏开发者实例：《No Rest for the Wicked》

来源：https://forum.norestforthewicked.com/t/renderdoc-gpu-captures-guide/26972

作者 TomasBlaho 在开发者论坛的抓帧说明中要求先创建 `steam_appid.txt`，再用 RenderDoc Launch 游戏，最后捕获并保存 `.rdc`。原文使用该游戏的 App ID 1371980；AC7 必须改用其自身的 502500。

这是其他游戏的使用实例，不是 AC7 的成功报告。

### 社区：Steam/启动器游戏的 ASI 方案

来源：https://mos9527.com/posts/cg/renderdoc-inject/

文章明确描述直接启动后转交常驻启动器的问题，并以《荒野大镖客 2》举例介绍 `renderdoc.dll` 改名为 `renderdoc.asi` 的办法。

该方法确有公开经验，但本机 AC7 已试验失败并回退，不再次照抄，更不把文章当成与 AC7 的兼容性保证。

## 本地前提核对

- `F:\SteamLibrary\steamapps\appmanifest_502500.acf` 中的 appid、name、installdir 确认本机 AC7 App ID 为 **502500**。
- 检查时游戏目录没有 `steam_appid.txt`，也没有已回退的 `renderdoc.asi`。
- 现有 RenderDoc `most_recent.cap` 指向正确 EXE，但 `workingDir` 为空。
- 准备的新配置仅将工作目录显式设为游戏目录，保留原捕获选项、空命令行与空环境变量；不启用全局 Hook 或额外子进程 Hook。
- 本轮没有向游戏目录部署文件或启动游戏，只在项目内准备下一次测试材料。

## App ID 直接启动测试操作（已试验，仍崩溃）

已准备：

- `AC7 Launch 配置`（清理时已移除的实验文件）：根据本机现有 RenderDoc 配置格式准备，`autoStart=false`。
- `App ID 文件`（清理时已移除的实验文件）：ASCII 内容仅为 `502500`，无 BOM。

步骤：

1. 正常退出 AC7，保留已登录且拥有游戏的 Steam 运行。RenderDoc 使用与 Steam 相同的权限级别。
2. 将上面的 App ID 文件临时复制到 `F:\SteamLibrary\steamapps\common\ACE COMBAT 7\steam_appid.txt`，不要覆盖已有文件。如果出现已有文件，先核对来源和内容。
3. 在 RenderDoc 的 Launch Application 页面加载上述 `.cap` 设置，或手动填写：

   ```text
   Executable: F:\SteamLibrary\steamapps\common\ACE COMBAT 7\Ace7Game.exe
   Working Directory: F:\SteamLibrary\steamapps\common\ACE COMBAT 7
   Command line: 空
   ```

4. 使用 RenderDoc Launch。先观察原注入实例是否保持连接，核对 PID/模块，暂不改变其他参数。
5. 若能进菜单且连接仍在，再进入实际飞行，抓取并重放第一帧。保存位置沿用运行调查文档中的 `artifacts/captures/ac7-flight-baseline.rdc`。
6. 退出游戏后移除本次临时 App ID 文件；原有加载器、插件和用户设置均不变。

### 如何判断结果

| 结果 | 含义与后续 |
| --- | --- |
| 原进程保持连接，能进游戏 | Steam 转交问题可能已解决；继续验证帧捕获与重放 |
| 仍由另一个未注入进程启动 | 标准 RestartAppIfNecessary 路径的处理不够；保留日志后回退，不认定捕获成功 |
| 原进程不再转交，但仍崩溃 | 转交和捕获兼容性是两件事；记录对应 PID/时间/调用栈，再决定隔离哪个组件 |
| 没有捕获提示但进程运行 | 先核对 RenderDoc 模块、设备日志和连接；不向运行中的已初始化图形进程晚注入 |

在拿到对应本次故障的证据前，不将旧 UE4SS 崩溃记录当成原因，不批量停用现有插件，也不盲目换 API、驱动或 RenderDoc 版本。
