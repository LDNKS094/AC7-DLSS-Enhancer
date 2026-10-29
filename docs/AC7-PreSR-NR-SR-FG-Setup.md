# AC7 普通屏幕：外部捕获 Pre-SR NR → SR → FG 对照方案

**定位：本文与导入配置仅用于外部捕获对照，不能提供引擎原生的干净场景和完整渲染数据，不满足后续明确的原生接入目标。** 当前需求与实施验收以 [原生 DLSS 需求基线](AC7-Native-DLSS-Requirements.md) 为准。

原始试验目标：RTX 5080、5120×1440 普通屏幕，在尽量不改程序源码的前提下，组合低分辨率 NR、DLSS SR 和帧生成。

## 1. 对照路径：用一个 Magpie Experimental 效果组完成

已核对本机下载的 `Downloads\Magpie-Experimental-x64.zip`：

| 项目 | 检查结果 |
| --- | --- |
| 版本 | 0.6.11 |
| 包内记录的源码提交 | `4fc74a196993c883ce2f01e96e8ed8d81f1490e6` |
| DLSS SR | 构建开关启用，`effects/DLSS/DLSS_SR.hlsl` 存在 |
| DLSS NR | 构建开关启用，`effects/DLSSNR/DLSSNR_AI_Filter.hlsl` 存在 |
| DLSS FG | 构建开关启用，`effects/DLSSFG/DLSS_FrameGeneration.hlsl` 存在 |
| NVIDIA 光流 | 构建开关启用 |

该提交的 Renderer、NativeEffectBackendFactory、ScalingOptions 和 ScalingModesService 已与本地克隆源码交叉核对，内容一致。

源码支持的图像传递方式：

1. 图像效果按列表顺序执行，每个效果接收上一个效果的输出。
2. NR 的效果元数据规定输出宽高等于输入宽高。
3. SR 的输出大小由缩放设置决定，可设为适应目标屏幕。
4. DLSS FG 在图像处理之后接收最终的 `effectsOutput`。它不是在效果列表中所在的位置立即生成帧。
5. 本包的 FG 硬件策略包含 CUDA 12.0 路径，即 RTX 5080 所属架构；是否最终可初始化仍取决于实际运行时能力检查。

因此可以先配置，而不开发新渲染器：

```text
AC7 低分辨率窗口
  → Magpie 捕获
  → NR：在捕获分辨率下增强，保持尺寸
  → SR：重建到屏幕尺寸
  → DLSS FG 2×
  → 屏幕输出
```

这是已确认源码可表达的候选链，尚未进行本机 GPU 运行验证。

## 2. Pre-SR 的具体含义

这里的 Pre-SR 是“在 Magpie 的 SR 之前执行 NR”。它的输入已经经过游戏自己的 TAA、色调映射及 HUD 绘制。

这与 OptiScaler 在游戏内部取得干净场景输入、原生深度/运动矢量和曝光后执行 Pre-SR NR 不同。窗口方案可以满足处理顺序，也能减少 NR 的工作像素，但画质、运动历史和 UI 处理不能按游戏内部集成作保证。

不要仅在 5120×1440 捕获画面上将 NR 的内部工作比例调小，再把整幅高分辨率结果交给 SR，并将此称为目标链。优先让游戏窗口本身低于显示分辨率，确保 NR 与 SR 的实际尺寸符合预期。

## 3. 已准备的导入文件

- [效果组配置](Presets/AC7-Magpie-PreSR-NR-SR-FG.json)
- [离线验证结果](Presets/AC7-Magpie-PreSR-NR-SR-FG.validation.json)

文件只包含 `scalingModes`，不包含全局配置、应用规则、快捷键或安装路径。所检查版本的显式导入会追加效果组，名称冲突时自动改名，不覆盖已有组。

三个组用于逐层比较：

| 组 | 图像顺序 | 用途 |
| --- | --- | --- |
| AC7 A - SR - 60 base | SR | 确认外部 SR 可用，建立基础处理成本 |
| AC7 B - PreSR NR + SR - 60 base | NR → SR | 确认 NR 确实生效，测量其新增成本 |
| AC7 C - PreSR NR + SR + FG2x - 60 base | NR → SR → FG 2× | 完整目标链 |

三个组都使用相同的 60 FPS 捕获处理上限，便于起步和比较。它们不是无限帧配置。FG 2× 在稳定接受 60 个真实内容帧且呈现能力足够时，以约 120 FPS 输出为目标，不能保证必达。

## 4. 关键参数

| 阶段 | 初始设置 | 原因 |
| --- | --- | --- |
| 帧率过滤 | Custom，60 FPS | 给单次 NR、SR 和 FG 留出处理余量 |
| NR | 单次、强度 1、保持输入尺寸 | 先验证明确的一次模型求值 |
| NR 二次内部降采样 | 关闭 | 初始输入本身已经是低分辨率，避免再叠加一层降采样 |
| NR 抗闪烁 | 关闭 | 先测基础处理成本和原始运动表现，再单独比较抗闪烁 |
| SR | 适应屏幕，比例 1× | 由 SR 承担放大到显示尺寸的工作 |
| SR 模型预设 | 请求 K | 这是模型预设请求，不能替代实际生效状态观察 |
| NR/SR/FG 光流 | NVOF，Balanced | 为 RTX 5080 选择现有 NVIDIA 光流路径；不是引擎原生运动矢量 |
| FG | 2×，模型 Auto | 先验证基本插值与节奏，再讨论更高倍率 |

效果元数据中 NR 和 FG 都是同尺寸输出，导入文件显式保留 1×；仅 SR 使用 `ScalingType::Fit`。无需为 NR 设置“放大到全屏”。

## 5. 使用步骤

1. 将已下载的完整 Magpie Experimental 主包解压到独立目录，运行其中的 Magpie.exe。
2. 首轮采用 SDR，避免同时引入 HDR 转换链。若另有 HDR 配置，先保留备份。
3. 在“效果组”页面使用导入功能，选择 `AC7-Magpie-PreSR-NR-SR-FG.json`。导入后可直接检查组内顺序。
4. AC7 使用窗口模式，先选 **2560×720**，游戏内部 Scaling 保持 **100%**。输入为 32:9，目标屏幕为 5120×1440。
5. 先选 A，进入可重复的实际飞行片段，确认最终输出尺寸及 SR 是否实际初始化。
6. 再选 B。确认 NR 状态为运行中，并检查其 `sourceSize/inputSize`。没有 NR 生效证据时，不继续把 C 的画面当作完整链。
7. 最后选 C，检查实际真实内容帧率、生成帧输出和 FG 日志。
8. 若 2560×720 下文字或目标细节不足，再测试 3840×1080。此时 NR 工作像素增至原来的 2.25 倍，需重新测量成本。

预期尺寸示例：

```text
窗口 2560×720
  → NR source/input 2560×720
  → SR input 2560×720，output 5120×1440
  → FG 输入最终 5120×1440 图像
```

最终呈现和光流内部纹理可能有独立尺寸，不能把任意中间纹理尺寸都当作 SR 输入或最终显示尺寸。

帧率过滤器只限制 Magpie 捕获处理，不替游戏限帧。如果 AC7 本身无限帧占满 GPU，应在游戏或已有外部限帧工具中配合设置稳定的真实帧率，给 Magpie 留余量。不要让游戏基础帧率、Magpie 目标和其他工具互相设置矛盾的上限。

如果想以更高真实帧率运行，可在三个组中同步调高过滤器的 60 FPS，并按显示器刷新率及处理余量测试。初始配置不会自动修改游戏或 RTSS。

## 6. 验证功能是否真正执行

源码中可用于核对的日志包括：

```text
DLSSNR STATUS: Feature=18 created=true ... sourceSize=... inputSize=...
DLSS SR initialized (...) ...x... -> ...x...
DLSSFG stage=CapabilityAvailable ...
DLSSFG progress: realGroups=... evaluations=... generatedPublished=...
```

其中初始化成功只说明创建了功能；仍需结合 NR 的持续运行状态和 FG 的后续计数判断实际执行。

尤其注意：NR 单次处理初始化失败时，程序可能保留原图继续缩放。因此“画面正常显示”和“SR/FG 在运行”不能证明 NR 已加入。

建议保留同一航线下的三组记录：

| 记录项 | A：SR | B：NR→SR | C：NR→SR→FG |
| --- | --- | --- | --- |
| 游戏真实 FPS | 待测 | 待测 | 待测 |
| Magpie 有效内容 FPS | 待测 | 待测 | 待测 |
| 最终输出 FPS | 待测 | 待测 | 待测 |
| NR/SR/FG 处理耗时 | 待测 | 待测 | 待测 |
| GPU 占用、显存、帧间隔 | 待测 | 待测 | 待测 |
| HUD、锁定框、远处目标、云层运动 | 待测 | 待测 | 待测 |

若已经达到 60 FPS 上限，新增处理可能体现为 GPU 余量下降，而不是平均 FPS 下降，所以不能只比较三个组的平均帧率。

## 7. 现成组合与其他项目的选择

| 路径 | 是否优先 | 对 AC7 的判断 |
| --- | --- | --- |
| 一个 Magpie 效果组：NR→SR→DLSS FG | 是 | 已下载包包含三后端，源码链路成立，先导入实测 |
| wilsjo2 OptiScaler：Pre-SR NR＋既有 SR＋受支持 FG | 保留为游戏内部方案参考 | 依赖既有超分输入，AC7 原版缺少这一入口；NR 成功也不自动证明 FG 可用 |
| Cheeky：Pre-SR NR＋SR＋其 FG 路径 | 保留为已有 DLSS 游戏的参考 | 需要 NGX-SR 数据来源，不能单独给 AC7 原版补齐场景数据 |
| RTX Encore 叠加 OptiScaler | 不采用 | 项目明确不支持该组合；硬件定位也不适合优先用于本机 |
| FoveatedDLSS5 | 不作为此次主链 | 其周边优化要求 Post-SR NR，不符合本次 Pre-SR 处理顺序 |

不建议将多个能拦截同一 NGX/Streamline 功能的工具一起装入 AC7 后再排查。先用 Magpie 一个进程完成完整链，才能分清捕获、模型、重建和呈现阶段的问题。

## 8. 何时才值得开发

先根据实测定位缺口：

| 实测问题 | 优先行动 | 确需开发时的范围 |
| --- | --- | --- |
| 缺少 SR 组 | 导入本文件，确认效果文件完整 | 无需改代码 |
| NR 跳过或创建失败 | 查看 DLL/驱动/状态日志，使用包内匹配组件 | 修复该失败路径；不要先重写整条链 |
| NR 太贵 | 保持单次，先减小游戏窗口分辨率，再单独试 NR 工作比例 | 在现有 NR 后端优化，明确比较基准 |
| NVOF 不可用或开销过高 | 在界面分别尝试 AMDOF/None 并重新开始缩放 | 只有可重复失败才修改光流路由 |
| FG 无法初始化 | 保留 A/B 工作路径；单独测试现成其他 FG 后端的受支持组合 | 聚焦 FG 运行库能力、输入、传输和呈现问题 |
| FG 后帧间隔不均 | 检查真实输入稳定性、限帧及呈现节奏 | 聚焦调度和测量，不重做 NR/SR |
| HUD/目标始终涂抹 | 调参、提高窗口分辨率并作 A/B | 若外部捕获数据不足，才考虑游戏内 Luma/AC7 渲染接入 |

开发优先级是修复现有单进程链中的已证明缺口。只有外部窗口捕获无法满足画质要求，才转向游戏内部获取颜色、深度、运动矢量、jitter 和 HUD 分层。这一步明显比配置组合工作量大，需先有渲染采样证据。

## 9. 已验证的边界

已完成的离线检查：

- 对用户下载主包的版本、提交和 SR/NR/FG/NVOF 开关进行核对。
- 检查三个效果组使用的四种效果定义确实存在于该包。
- 对照包内 HLSL 元数据检查参数名称、整数类型、范围和枚举。
- 检查 NR 在 SR 之前、NR 同尺寸、SR 适应屏幕，且只有一个 2× FG。
- 核对该版本的导入逻辑为追加效果组，并对同名组自动改名。

没有执行 GUI 导入、游戏捕获或 NVIDIA GPU 求值。配置与源码链路检查通过，不等于 AC7 运行兼容性通过。没有改动现有 Magpie 设置、游戏目录或系统注册表，也没有修改 Magpie 程序源码。

## 10. 源码依据

固定至下载主包记录的提交：

- [按顺序执行效果，Renderer.cpp](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/Renderer.cpp#L4007)
- [FG 接收最终 effectsOutput](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/Renderer.cpp#L4314)
- [NR/SR 原生后端创建](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/NativeEffectBackendFactory.cpp#L141)
- [导入追加及名称冲突处理](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie/ScalingModesService.cpp#L658)
- [缩放类型定义](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/include/ScalingOptions.h#L87)

相关上游说明：

- [wilsjo2 Pre-SR 配置及 FG 前提](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/main/INSTALL-DLSSNR.md)
- [Cheeky Pre-SR 与 FG 文档](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS/blob/master/USAGE.md)
- [FoveatedDLSS5 的 Post-SR 限制](https://github.com/maximedgoulet/FoveatedDLSS5)

原始综合记录：[AC7 DLSS 调查记录](AC7-DLSS-NR-SR-Investigation.md)。
