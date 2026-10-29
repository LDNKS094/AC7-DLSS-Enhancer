# AC7 运行与渲染调查操作

**当前 RenderDoc 试启动已暂停**，不要继续执行下文历史捕获步骤。最终结果、止损依据、备份及现场见 [阶段收束](AC7-RenderDoc-Stop-Decision.md)。下一步评估其他原生观察工具，尚无有效 AC7 捕获。

当前 1.45/1.46 在固定条件下均报 D3D11 设备丢失 0x887A0020。下一项为 [D3D11 验证层取证](AC7-D3D11-Validation.md)：输出收集工具及独立自检已通过，游戏验证轮待执行；不继续盲换版本。mod 继续隔离，ProcDump/注册表监视不启用。下文为历史基础操作。

## 目的

先用可重放的游戏帧确认实际 API、输出目标和渲染阶段，再定位原生输入。取得这些证据后才比较后端；首轮不加载 NR/SR/FG 插件。

## 当前只读观察

- 本机已有 RenderDoc：`C:\Program Files\RenderDoc\qrenderdoc.exe`。
- 检查时 AC7 正在运行，进程路径为 `F:\SteamLibrary\steamapps\common\ACE COMBAT 7\Ace7Game.exe`，命令行未显示额外渲染参数。
- 进程模块中观察到系统 `d3d11.dll` 和 `dxgi.dll`，未观察到 `d3d12.dll` 或 `renderdoc.dll`。
- 这支持当前使用 DX11 的判断，但模块列表仍只是线索；以有效捕获中记录的 API、设备及资源为进一步证据。
- 游戏已有 `dinput8.dll`、`dwmapi.dll` 等加载器，首轮保持现有环境并记录它，出现捕获问题再单独隔离，不直接覆盖文件。

这些是一次检查的观察，不自动代表后续启动、所有场景或其他 API 模式。

## 第一轮：最小有效捕获

### 用户操作

1. 正常退出当前 AC7，保留 Steam 运行。
2. 启动 RenderDoc，打开 **Launch Application** 页面。
3. 设置：

   ```text
   Executable Path:
   F:\SteamLibrary\steamapps\common\ACE COMBAT 7\Ace7Game.exe

   Working Directory:
   F:\SteamLibrary\steamapps\common\ACE COMBAT 7

   Command-line Arguments:
   留空，首轮不加入 -dx11/-dx12 或画质修改参数
   ```

4. 本轮不新增 Magpie、ReShade、OptiScaler 或其他 NR/SR/FG 注入器。不改变现有游戏文件。
5. 通过 RenderDoc 的 Launch 启动。若游戏转交/新建了子进程，再核对实际捕获进程；只有确实发生这种情况才调整子进程捕获选项。
6. 先保持目前能正常运行的分辨率与画质，记录它们；进入实际可控制飞机的任务或自由飞行。
7. 选择能看到 HUD、地形/天空、云层和飞机的典型画面，尽量稳定镜头后捕获一帧。使用 RenderDoc 所示捕获快捷键，默认通常为 F12；以实际提示为准。
8. 打开捕获，确认可以正常重放并看到游戏事件，而不只是黑屏/菜单。将捕获保存到：

   ```text
   E:\CodingProject\AC8_MOZA\AC7-Native-Adapter\artifacts\captures\ac7-flight-baseline.rdc
   ```

9. 在本地记录任务、视角、是否暂停、分辨率、渲染比例和图像设置。首帧不要用暂停菜单或照片模式替代实际飞行。

`.rdc` 保存在项目忽略的 artifacts 目录，不提交到 Git，不要求上传。该文件可能很大。

如果直接启动被 Steam/启动器拦截、没有捕获提示、启动失败或捕获无法重放，先记录现象和 RenderDoc 日志。不要为了获得成功画面，同时切换 API、删加载器、降画质和修改多个 hook；每次只调整一个已定位的问题。

### 捕获后由助手分析

首帧先回答四件事：

| 项目 | 证据 |
| --- | --- |
| 实际 API、设备、输出尺寸 | 捕获 API 信息、设备/资源描述和输出事件；无法从工具取得的交换链字段标为未知 |
| 渲染阶段 | 事件列表和纹理使用关系，定位时序处理、色调映射、放大和 UI |
| 干净场景候选 | 无 UI 的中间颜色及其生产/消费 pass |
| 原生引导候选 | 同一帧的深度、速度及视图/曝光常量绑定 |

输出应包含事件 ID、资源 ID、格式、尺寸、有效区域和判断依据。不能只发一张看起来像深度的截图就认定语义。

## 第二轮：验证数据含义

首帧可分析后，再按需采样，不一次收集大量文件：

1. 同一视角的相邻/连续帧：检查 jitter、历史与资源变化。连续帧要核对实际帧号，不把间隔较大的两次按键捕获当作连续帧。
2. 相机移动及物体移动的对照：区分相机运动和物体速度，确认向量方向、单位及无效像素。
3. 一档较低游戏渲染比例：确认内部 view rect、纹理分配/对齐、原空间放大及 HUD 分辨率。
4. 明暗变化：确认曝光、pre-exposure 和颜色空间。
5. 相机切换、任务重启或其他必要场景：寻找可靠的历史重置信号。

只在基础捕获被证明可用后进行。若后端需要捕获未显示的交换链或运行时字段，再增加最小诊断，不先开发完整适配器。

## 第三轮：以证据选型

把实际 API、资源、参数和回接点与候选接口对照，再比较：

- 哪些可以直接复用。
- 哪些只需 AC7 参数转换。
- 哪些需要改后端、呈现或资源生命周期。
- FG 是否能取得正确的无 HUD/UI 与相机信息。
- 同进程与跨进程路径的同步成本及维护范围。

结果写入 [架构调查](../../../docs/AC7-Native-DLSS-Architecture-Selection.md)，不在首帧尚未取得时锁定宿主。

## Launch 转交故障与 ASI 尝试（已回退）

**当前状态：用户报告 ASI 加载方式启动崩溃，新增 `renderdoc.asi` 已校验哈希后移除，用户确认游戏恢复正常启动。以下部署及操作说明仅为试验记录，不应继续按此启动。** 原有加载器和两个超宽屏插件哈希均未变。

RenderDoc 新日志表明 PID 53536 已进入 AC7，拦截到 RTX 5080 的 D3D11 设备创建和 frame-capturer 注册。其 crash-helper 启动有 error 2，但没有取得可确认根因的故障调用栈。系统中的 UE4SS 访问冲突记录对应更早的 PID 44332，不能据此断言本次崩溃由 UE4SS 导致。日志快照保存在 `../artifacts/diagnostics/renderdoc-asi-failure/`。

首次按 Launch 启动后，日志记录 RenderDoc 成功注入 PID 160904、166820，但连接均很快断开；实际运行的 AC7 是另一个由 Steam 启动的进程，模块中没有 RenderDoc。这与 Steam 转交启动相符，不能仅据连接断开认定游戏崩溃。

本机启用了 Secure Boot，因此不选择依赖 AppInit_DLLs 的全局 Hook，也不为抓帧修改 Secure Boot 或系统注册表。

已核实现有 `dinput8.dll` 为 Ultimate ASI Loader 10.0.0，使用该加载器尝试启动期加载 RenderDoc：

```text
来源：C:\Program Files\RenderDoc\renderdoc.dll
新增：F:\SteamLibrary\steamapps\common\ACE COMBAT 7\renderdoc.asi
版本：RenderDoc 1.46 x64
```

只新增此文件，未替换 `dinput8.dll`、`dwmapi.dll`、超宽屏 ASI 或 UE4SS。来源与副本的 SHA-256 一致，既有四个加载器/插件文件的哈希保持不变。详情见 [部署及回退记录](RenderDoc-ASI-Deployment.json)。

### 原试验操作（已停止）

1. 正常退出当前 AC7，确认进程已结束。
2. 保持 RenderDoc 打开，改为从 Steam 正常启动 AC7，不再使用 Launch。
3. 观察是否出现 RenderDoc 捕获提示；即使没有明显叠层，也先检查进程是否加载 `renderdoc.asi` 以及日志中的设备/连接信息。
4. 在 RenderDoc 选择 **File → Attach to Running Instance**，刷新并连接实际 `Ace7Game` 实例。这里是连接启动时已加载 RenderDoc 的进程，不是图形初始化后的晚注入。
5. 成功识别图形设备后再进实际飞行抓帧，保存到前述 captures 目录。

ASI 加载仍需验证是否足够早、是否能与现有组件共存。若出现启动异常或捕获仍不可用，先正常退出游戏，仅移除本次新增的 `renderdoc.asi` 恢复，再根据新日志定位；不要继续叠加加载方式。

## 当前边界

已做：工具/进程/日志检查、尝试部署启动期 RenderDoc 副本、用户报告启动崩溃后回退，确认新增文件已移除且原有四个文件未变，保存失败日志。

未做：强制退出或重启游戏、执行全局 Hook、修改系统设置、抓取或重放游戏帧。用户已确认回退后正常启动，崩溃根因仍未确认；完整帧捕获及渲染阶段仍未完成验收。
