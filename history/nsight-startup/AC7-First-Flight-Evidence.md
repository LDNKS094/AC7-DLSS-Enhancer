# AC7 第一份飞行帧证据索引

> 历史记录：Nsight启动资格与首帧导出阶段，非当前操作计划。后续两份C++ Capture、GPU旁路及正式NR查询已完成；当前状态以 [项目README](../../README.md) 和 [API核对](../../docs/AC7-Graphics-API-Support.md) 为准。下文进程ID、等待导出和恢复步骤均为当时现场。

当前标记：这份捕获属于 **无补丁原版基线**。用户请求后已恢复20个原始补丁文件并校验哈希，运行验证待完成；见 [恢复记录](../../docs/AC7-Patch-Restore-State.json)。本文事件ID和资源ID不得直接用于下一份带补丁捕获。中央2560×1440 viewport与移除超宽屏补丁的上下文一致，不能当成最终超宽屏适配条件。

## 采样条件

- 用户确认：任务1、Free Flight、机外视角。
- 工具：Nsight Graphics 2025.5.0，Frame Debugger / Capture for Live Analysis。
- 在游戏有焦点时按F11捕获，避免失焦暂停；画面可见飞机、场景与HUD。
- 目标PID132328；此前已核对配套Nsight DLL及无mod/RenderDoc/SDK验证层。
- 当前只有用户截图与live会话，无已导出的API JSON、独立重放或原始纹理数据。

## 截图可直接读取的字段

用户所发Nsight主界面截图：

| 字段 | 观察 | 边界 |
| --- | --- | --- |
| 当前事件 | 16152，IDXGISwapChain4::Present | 不代表实际设备功能级别或完整交换链创建描述已确认 |
| Present返回 | S_OK | 只对应当前事件 |
| SyncInterval / Flags | 0 / 0 | 不自动等于全程呈现策略 |
| Back Buffers | Current Back Buffer Index=0，条目RTV2，Index=0 | RTV是工具视图命名，不直接认定底层Texture ID |
| SwapChain.BufferUsage | DXGI_USAGE_SHADER_INPUT \| DXGI_USAGE_RENDER_TARGET_OUTPUT | 来自Present页截图 |
| SwapChain.BufferCount | 1 | 不与最小三角形样例的双缓冲混用 |
| SwapChain.Windowed | TRUE | 只确认本会话窗口状态 |
| Output Window | 0x40c40 | 运行期句柄，不作为固定标识 |
| Events | 6912 / 16153，Filter Enabled: default-d3d11 | 当前有过滤，6912不是全帧draw数量 |
| 根marker | Frame30162，范围8–16152，尚未展开 | 与状态栏Frame #30161相差1，保留两种显示，不擅自统一 |
| marker异常 | 帧前有“[Mismatched marker]…”条目 | 需保留，不能假设所有嵌套marker天然可靠 |
| Scrubber | 帧marker显示3.73ms | 非独立无工具性能测量，不当作真实游戏性能基准 |
| API | D3D11设备/调用和IDXGISwapChain4::Present | 运行API确认有捕获证据；资源语义仍未知 |

分辨率/像素格式尚未在截图可见字段中取得。预览缩略图的形状、黑边或保存配置值不能代替资源描述。

## Present JSON 已读取

实际文件：`artifacts/captures/nsight-flight-baseline/Combined__Ace7Game__Event_16152__2026_10_27-07_22_35.json`，109197字节，顶层只有Device与Present。附带 `Images/Present/RTV2_BackBuffers_Name_Row0.PNG`。

从结构化字段确认：GPU NVIDIA GeForce RTX5080，feature level11_0；交换链Buffer Desc Width5120、Height1440、Format R10G10B10A2_UNORM，RefreshRate0/0、Scanline/Scaling UNSPECIFIED；SampleCount1、Quality0；BufferCount1，Windowed TRUE，SwapEffect DISCARD，Flags ALLOW_MODE_SWITCH，Usage SHADER_INPUT|RENDER_TARGET_OUTPUT。

这些是输出交换链描述，不能替代内部场景分辨率/有效区域，也不能由10-bit格式判断HDR、scRGB、PQ或实际DXGI color space。Device.Format Support是设备格式能力表，不是这一帧的实际纹理清单。Present JSON没有全帧Events或场景纹理绑定。

用户报告展开事件列表过长，无法截图。下一步使用截图可见的Events面板左上软盘图标尝试导出列表，格式以对话框实际提供的选项为准；不要求用户手工分段截图。若图标实际功能不同，先记录tooltip/对话框，不猜字段或声称已读到全帧。

## Events CSV 已做代码筛选

文件`artifacts/captures/nsight-flight-baseline/events.csv`，1344876字节，带BOM、5列（Event/Description/Object/CPU ms/GPU ms）。用支持引号和双引号转义的CSV解析器处理，6912条数据、ID范围0–16152，所有行列数验证一致；包含593条Draw族调用、40条Dispatch、114条OMSetRenderTargets绑定。它是当前过滤视图导出，不能当完整API跟踪；这593也不代表未经滤的全帧绘制数量。

CSV中没有导出的命名pass标记，只有Start of Capture与Mismatched marker注释；不能据此认定游戏完全没有marker（过滤/导出也可能影响）。无需用户截图全部长表。

帧尾可见候选：

| 事件 | 导出中可见状态 | 待验证 |
| --- | --- | --- |
| 15080 | 绑定RTV2、无DSV | 工具Present JSON也以RTV2标识backbuffer，但底层texture关系仍需描述 |
| 15121 | DrawIndexed(IndexCount3,StartIndex6,BaseVertex0)；按导出可见状态PS388/VS118，输出RTV2 | 是否全屏合成/tonemap/UI，需shader、SRV、OM及图像确认，不能凭3索引命名 |
| 16059 | DrawIndexed(IndexCount12,StartIndex0,BaseVertex0)；按导出可见状态PS268/VS124，输出RTV2 | 是否最终叠加/HUD/其他输出，需状态/图像比较 |
| 16152 | Present S_OK | 已有Device/Present JSON |

中间存在缺失ID，说明导出不是连续完整调用记录。上述shader/RT追踪来自可见状态更新，是候选状态，不排除过滤隐藏了更新。最后一次PSSetShaderResources也只能说明一次局部slot更新，不是完整SRV列表，不能拿最后一条当唯一输入。

下一步使用Events的Event输入框和Go to分别跳到15121、16059，为每项导出API Inspector的All Pages JSON到同目录，保留自动文件名和Images子目录。与Present页不同，Draw事件导出应包含shader/OM等页面；导出后助手先验证字段是否实际存在。若操作后只有高亮行改变、scrubbed事件未更新，需确认API Inspector显示的当前事件ID对应。

## 15121 / 16059 JSON 已读取

文件为`Combined__Ace7Game__Event_15121__2026_10_27-07_29_50.json`（223948字节）与`Combined__Ace7Game__Event_16059__2026_10_27-07_30_09.json`（224997字节）。均含Device、IA、OM、PS、Rasterizer、VS页。

确认：

| 项目 | 15121 | 16059 |
| --- | --- | --- |
| OM输出 | Texture2D39 / RTV2 | 同左 |
| 资源 | 5120×1440 R10G10B10A2_UNORM、单采样 | 同左 |
| viewport | x1280,y0,w2560,h1440 | x0,y0,w5120,h1440 |
| scissor | left1280,right3840,top0,bottom1440 | left0,right5120,top0,bottom1440 |
| blending | 关闭，RGBA写入 | 关闭，RGBA写入 |
| depth/stencil | 均关闭，无绑定DSV | 均关闭，无绑定DSV |
| PS slot0 | Texture2D2052 / SRV2696，5120×1440 B8G8R8A8_TYPELESS RT\|SRV | Texture2D9 / SRV22，1×1 B8G8R8A8_TYPELESS |
| PS slot1 | Texture2D219 / SRV271，5120×1440 R8G8B8A8_TYPELESS RT\|SRV | Texture2D9 / SRV22 |

意义与边界：15121当前viewport/scissor确实限制到中央16:9区域，不能把5120分配尺寸当成整个场景在此draw中的有效区域；也不能从viewport直接推断所有上游pass均2560宽。16059为全宽viewport且绑定1×1纹理，结合12索引使边缘/纯色矩形绘制成为候选，但尚无真实图像/像素shader确认，不把它直接标记为黑边、HUD或已确认的输出变换。

PS页面还保留多个其他槽位资源（含cube及buffer），绑定存在不等于当前shader实际读取；没有反汇编，不宣称所有均是输入。Shader Description部分大量统计为0，15121还含异常Num Interface Slots值4099932160，常量页未导出；不能据此认定shader零指令或不用常量。这可能涉及工具反射/收集能力，具体原因未确认。

图片保全纠正：JSON引用的资源PNG实际检查为16×16图标，不是真实纹理导出。两个事件的同资源引用完全相同Images路径，后导出可能覆盖早事件文件；不能利用当前同名PNG比较事件前后图像。未来实际图片和JSON按event独立目录保存。

下一项最小观察：在15121定位PS slot0 Texture2D2052，打开其真实纹理查看画面，确认有没有飞机/场景/HUD/黑边和有效区域。先取得一张可辨认图像，不根据资源名或大小认定是干净scene color。必要时对当前target同事件截图，不能用16×16图标代替。

## 当前保全与下一步

1. 保持live会话，不Resume/Next Frame/Terminate或关闭目标。
2. 当前Present事件16152的API Inspector右上角有两个导出图标。以悬停提示 **Export All Pages to Json** 为准，导出全部页面到 `artifacts/captures/nsight-flight-baseline/event-16152-present.json`。该文件仅为当前事件状态，不是完整帧。
3. 展开Events中8–16152对应的Frame30162根节点（文字左边的小三角），截图子分组；先记录层级和marker名称，再决定定位何处，不盲搜假定的TAA名称。
4. 如需完整持久化，截图中已确认工具栏有 **Export as C++ Capture**。在先保存JSON/marker证据后再执行并核对生成内容与能否编译重放；不能仅凭导出按钮存在宣称已保存完整可重放捕获。
5. 后续查看Present页交换链完整字段、RTV底层资源描述，再从帧尾UI向前追踪干净场景/后处理。每项记录事件ID、资源标识、格式、尺寸和生产/消费关系。

## 未决

实际输出尺寸与格式、内部scene有效区域、TAA/tonemap/UI阶段、颜色/深度/速度/jitter/曝光/相机常量语义、历史重置、输出回接点。未完成这些之前后端仍未选定。
