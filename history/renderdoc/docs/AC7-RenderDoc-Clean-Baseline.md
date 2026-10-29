# AC7 干净游戏目录与 RenderDoc 对照

当前结果：经 Steam 启动的干净基线 PID 138000 已持续运行至少 116 秒，窗口 Responding=true。模块没有 UE4SS、ASI 或 RenderDoc；DINPUT8 和 dwmapi 来自 Windows 系统目录，另有 Steam 自带 Overlay 及客户端库。随后用户报告干净 RenderDoc 对照仍失败，PID 130124 和 83672 均到达 D3D11 Presenting。最近保存的实际配置却为工作目录空、delayForDebugger=20，与准备配置不同；不能把既往启动条件视为完全一致。当前暂停新试验，先按 [假设与预期矩阵](AC7-RenderDoc-Hypotheses.md) 建立无工具的相同上下文直接启动对照。

## 当前范围

这次补做的是 **游戏目录内不保留非原版 DLL/ASI 或 mod 文件** 的对照，先经 Steam 正常启动，再验证 RenderDoc 注入。此前只停用了 UE4SS，未覆盖 ASI 加载器及超宽屏插件，不能算此项测试。

没有重置用户画质配置、存档或 Steam 设置。系统/驱动/Steam Overlay 等运行时模块需要在启动后另行记录，不能仅凭游戏目录干净就声称所有外部注入均不存在。

## 官方文件依据

本机 Steam appmanifest：App ID 502500，Build ID 9855922，Depot 502501，Manifest ID 788319397026631312。

缓存位置：`D:\Application\Steam\depotcache\502501_788319397026631312.manifest`。

解析该缓存清单的 protobuf 文件映射后对照本机文件：

- 共 18 个官方 EXE/DLL，文件大小和 SHA-1 全部匹配，包括主 EXE 和 Steam API DLL。
- 主 EXE SHA-1：`fdcaf9f40ae0c18b683270601769b0bee9c49602`。
- 未发现额外 PAK；现有 PAK 的名称和大小匹配。
- 未对整套大型 PAK 做完整内容哈希，也未重新从 Steam 服务器验证缓存清单；以上是本机缓存清单对照结果。

## 已隔离的全部额外文件

原游戏目录：`F:\SteamLibrary\steamapps\common\ACE COMBAT 7`。

额外文件共 20 个，分为 7 个顶层条目：

```text
dinput8.dll
dwmapi.dll
AC7HUDCenter.asi
AC7SceneProjection.asi
AC7HUDCenter.log
AC7SceneProjection.log
ue4ss\                       # 包括 UE4SS.dll、配置、Lua mod 和日志
```

全部复制到游戏目录外的备份位置，逐文件校验 SHA-256 后才移除原路径；移除后再次验证备份哈希。未删除这些组件的唯一副本。

备份：`E:\CodingProject\AC8_MOZA\AC7-Native-Adapter\artifacts\backups\clean-renderdoc-baseline`。

[逐文件清单与校验状态](../../../docs/AC7-Clean-Test-State.json)保存原相对路径、文件大小及 SHA-256。备份本体在 Git 忽略目录中，不提交。

隔离后根目录仅为 `Ace7Game.exe`、`Engine`、`Game`；剩余官方二进制数量为 18，没有 ASI。Steam 基线启动前没有临时 `steam_appid.txt` 或 `renderdoc.asi`；基线启动后，仅为下一次直接 RenderDoc Launch 重新放入 App ID 文本。ProcDump 未运行，AC7 的 IFEO / SilentProcessExit 两个诊断键在检查时不存在。

## 测试顺序

1. **干净 Steam 基线**：由助手经 Steam `-applaunch 502500` 启动，不用 RenderDoc 或 ProcDump。助手核对进程与模块，用户确认能正常到菜单/飞行。进程存活和模块列表不等于完成画面/操控验收。
2. **干净 RenderDoc 对照（尚未执行）**：基线确认后正常退出游戏。仍保留所有 mod 在目录外。仅放入先前准备的 App ID 文本 502500，以避免已观察到的 Steam API 启动转交；使用原 `.cap` 配置 Launch，不启动 ProcDump，不启用注册表监视，不改画质/API。
3. 若干净 RenderDoc 对照成功，先保存标明“无 mod”的飞行帧，再考虑逐个恢复组件；若仍失败，也不能归因于已隔离的 DLL，应保留对应 PID/日志后再调查。

## 恢复

退出 AC7 后，从备份位置按清单把 7 个顶层条目恢复到游戏原目录。恢复前检查目标路径没有新文件，避免覆盖；恢复后逐文件对照清单 SHA-256，保留备份直到恢复验证完成。

若同时存在后来测试放入的临时 `steam_appid.txt`，先核对内容仍为 502500 再移除。原有 mod 的配置、脚本和日志均随备份保存，恢复时不需要重装。

当前尚未完成干净 RenderDoc 注入测试，不能据此宣布抓帧成功或确认冲突来源。
