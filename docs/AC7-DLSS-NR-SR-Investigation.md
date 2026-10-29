# 皇牌空战 7：DLSS、外部缩放与中心区域优化调查记录

调查目标：在 RTX 5080、5120×1440 普通屏幕及 PC VR 环境下，寻找能降低《皇牌空战 7：未知空域》（AC7）渲染压力、提高帧率并维持可接受画质的方案。

**当前需求：普通屏幕下，以原生场景颜色、深度、运动矢量和时序数据接入 Pre-SR NR → SR → FG，并为超宽屏加入 NR 中心保护/周边减负。** 原生接入的数据契约与分阶段验收见 [原生 DLSS 需求基线](AC7-Native-DLSS-Requirements.md)。[Magpie 组合方案](AC7-PreSR-NR-SR-FG-Setup.md) 及其导入配置保留为外部捕获对照，不作为该需求的目标实现；下文保留此前各路线的调查证据。

本文记录公开资料、源码检查和本地构建结果。项目声称的功能、作者给出的测量，以及本机已验证的事实分别说明；未将其他游戏的成绩视为 AC7 的实测结果。本文不涉及项目更新频率比较。

## 1. 核心结论

- AC7 PC 版已有帧率限制设置，优先使用游戏内无限帧选项并排查垂直同步、驱动和外部限帧，不需要另找解锁补丁。
- DLSS SR 不依赖 VR。AC7VRDLSS 与 VR 的绑定来自该插件的渲染实现，而不是 DLSS 算法本身。
- AC7 原版没有在本次调查中确认到官方 DLSS 接入。OptiScaler、Cheeky 等依赖既有超分管线的工具，不能仅靠复制 DLL 就给原版 AC7 补齐所需渲染数据。
- SAOG0721 的 Magpie Experimental Fork 确实实现了 DLSS SR、DLSS FG 和 DLSSNR；应与 Blinue 的官方 Magpie 区分。
- SR、NR 可以串联。SR 主要用于低分辨率重建，NR 主要用于画面增强；NR 会增加计算量，其优化通常是在减少新增成本。
- FoveatedDLSS5、wilsjo2 分支和 RTX Encore 的相关中心区域选项主要优化 NR；Cheeky 也能优化 SR。不能统称为“中心区域插帧”。
- 对 AC7 普通屏幕，Magpie Fork 是较直接的外部试验路径；对 AC7 VR，Cheeky 与已有 DLSS 插件能否共存值得调查，但尚未确认。

## 2. SR、NR、FG 的区别与组合

| 名称 | 全称与作用 | 对性能的主要影响 |
| --- | --- | --- |
| SR | Super Resolution：从低分辨率输入重建高分辨率画面 | 通常用于降低主要场景的渲染负载；重建自身仍有成本 |
| NR | Neural Rendering：调整已有画面的光照、材质、色调和细节等外观 | 增加 GPU 工作量，主要用于画面增强 |
| FG | Frame Generation：在真实帧之间生成中间画面 | 提高显示流畅度，不代表游戏逻辑和输入响应同比加速 |
| DLAA | 原生分辨率的深度学习抗锯齿 | 主要改善抗锯齿，不能当作降低内部渲染分辨率的性能方案 |

### 2.1 SR 和 NR 不互斥

常见的 Post-SR NR 顺序：

```text
游戏低分辨率场景 → SR 重建 → NR 增强 → 后续输出
```

部分工具支持 Pre-SR NR：

```text
游戏低分辨率场景 → NR 增强 → SR 重建 → 后续输出
```

Pre-SR 通常减少 NR 处理的像素数，代价可能是增强细节和时序表现发生变化。Post-SR 在较高清的画面上增强，通常更昂贵。各项目对 Pre-SR、周边压缩、多次 NR 和不同输入类型的组合限制并不相同。

FG 在受支持的管线中再与这些功能组合；具体插入点和 HUD 处理取决于游戏与工具。不能用一条通用流程图保证所有组合可用。

### 2.2 算法共存不等于插件共存

一个工具内部管理 SR 与 NR，可以统一控制资源、顺序和历史。两个工具若同时拦截 NGX、Streamline 或相同渲染阶段，可能出现重复处理、输出覆盖或崩溃。

例如 RTX Encore 的文档明确不支持与 OptiScaler 叠加。FoveatedDLSS5 的安装说明也要求移除会拦截同一 DLSS 调用的另一 RenoDX DLSS 插件。

## 3. AC7 原版与 AC7VRDLSS

### 3.1 帧率设置

AC7 PC 版支持内置取消帧率限制。测试时应在实际飞行场景观察，不以菜单或过场帧率判断。

本机读取到的配置中有 `FrameRateLimit=0.000000`，同时存在不同字段的垂直同步设置；仅凭某一个字段，不能替代运行时帧率观察。

### 3.2 AC7VRDLSS 的公开范围

项目：<https://github.com/Beren5556/AC7VRDLSS-Downloads>

公开仓库和发行包提供安装说明、配置、Lua 脚本及原生插件，但作者在分发说明中明确表示可构建的原生 C++ 核心和 Python 开发源码保留在私有仓库。

因此，仓库中存在 MIT 许可证，并不意味着本次取得了完整可重编译的核心实现。

### 3.3 VR 绑定的位置

发行包中的 `scripts/NVIDIA_DLSS.lua` 主要负责：

- 向原生插件发送设置及保存请求。
- 读取游戏飞行、暂停、任务和相机状态。
- 在 UEVR 中提供参数界面。

DLSS 渲染位于原生 DLL。静态字符串检查可见：

```text
source=UEVR.SDK.stereo_hook.scene target=
missing stereo boundary or repeated eye
waiting for requested stereo pair
right eye disagrees with left eye dimensions/history/jitter
VR submission before both DLSS outputs; no fallback
```

这些信息支持该实现依赖双眼配对、双眼历史及发布边界的判断，但字符串检查不是完整控制流反编译。

指定 UEVR 源码还显示：关闭 VR 会使 `is_hmd_active()` 返回 false；Stereo Emulation 会改变 render-target manager 路径。因此不能把“Disable VR Entirely”“Stereo Emulation”或桌面镜像单眼当作已验证的普通屏幕 DLSS 解绑方案。

## 4. Luma：不依赖 VR 的开源接入基线

项目：<https://github.com/Filoppi/Luma-Framework>

Luma 的 Unreal Engine 通用模块在 DirectX 11 TAA 阶段识别颜色、深度、速度纹理和视图常量，解码运动矢量并调用 NVIDIA NGX，不要求 UEVR。

本机完成两次构建：

1. 未修改源码的 Development-Release。
2. 加入首次成功 SR 求值尺寸日志的 Publishing-Release。

但是该通用模块源码注明 `DLAA only`：输出尺寸取自原 TAA 输出纹理，求值成功后将结果写回该纹理。不能只把输出尺寸改成显示器分辨率，因为后续纹理复制、视口、空间放大与 HUD 仍需匹配。

真正的 AC7 超分适配需要先实测各阶段尺寸，再确定高分辨率 DLSS 输出的发布位置，同时处理相机切换、任务重启、暂停和分辨率变化的历史重置。

已有本地产物：

- [Luma 接入详细报告](../../research/AC7-Flat-Probe/README_zh.md)
- [普通屏幕实验包](../../research/AC7-Flat-Probe/AC7-Flat-Probe.zip)
- [源码诊断补丁](../../research/AC7-Flat-Probe/Luma-flat-probe.patch)
- [离线验证记录](../../research/AC7-Flat-Probe/verification.json)

实验包仅是接入验证基线，不是已完成的 AC7 性能补丁。尚未安装到游戏或进行飞行测试。

## 5. Magpie 官方版与 Experimental Fork

| 项目 | 本次确认的功能定位 |
| --- | --- |
| [Blinue/Magpie](https://github.com/Blinue/Magpie) | 窗口捕获、外部缩放与滤镜；其 FSR EASU/RCAS 属于 FSR 1 空间放大与锐化 |
| [SAOG0721/Magpie](https://github.com/SAOG0721/Magpie) | 增加实验性 DLSS SR、DLSS FG、DLSSNR 及其他原生后端 |

### 5.1 Fork 的 DLSS 实现

检查本地 `E:\CodingProject\Magpie` 源码，确认：

- `DLSSSRUpscaler.cpp` 实际调用 NGX 的 DLSS 创建和求值接口。
- `DLSSFrameGenerator.cpp` 实际调用 DLSSG 接口。
- `DLSSNRFilter.cpp` 有独立 NR 实现和运行库加载路径。

该方案处理完整窗口画面。运动可以由光流估算，缺失时回退为零；SR 的 jitter 在所检查实现中为零。它无法取得游戏引擎原生的完整深度、运动矢量、曝光和独立 UI 数据，因此不能等同于游戏内部 DLSS 集成。HUD、文字和高速运动边缘也参与整图处理。

### 5.2 为什么内置列表有 FG、NR，却没有 SR

默认效果组代码只预建 Lanczos、FSR、RTX Video、DLSSFG、XeSSFG 和 DLSSNR，没有预建 SR 组。

SR 是可单独添加的效果器：

```text
效果组 → 新建效果组 → 添加效果器 → 搜索 DLSS → DLSS_SR
```

效果标识为 `DLSS\DLSS_SR`，完整程序包应包含：

```text
Magpie.exe
effects\DLSS\DLSS_SR.hlsl
```

“预建效果组”与“全部可用效果器”不是同一个列表。若添加菜单中也没有 SR，再检查文件是否存在、是否完整解压，以及 `logs\magpie.log` 中的解析错误。

后续已检查用户下载的 0.6.11 ZIP：`DLSS_SR.hlsl` 确实存在，构建清单也记录 `EnableDLSSSR=true`。尚未检查实际解压后的程序目录和 GUI 列表。

### 5.3 两个可选附件

#### DLSSNR-DLL-Options.zip

它提供两种 `nvngx_dlssnr.dll`：

| 选项 | 用途 |
| --- | --- |
| NVIDIA 原版 RTX 50 选项 | 适用于 RTX 50，包括本机 RTX 5080 |
| RTX 20/30/40 社区兼容选项 | 经社区修改的兼容运行库 |

若测试 NR，完全退出 Magpie，备份原 DLL，再将所选 DLL 放在 **Magpie.exe 旁边**。每次只选一种，不放到 AC7 目录。

该附件用于 NR，不是启用 SR 或 FG 的必装补丁。有关签名和哈希的说明来自项目分发文档，不能替代对实际下载文件的校验。

#### NGX_OTA_Switch.bat

这是系统级 NVIDIA NGX 自动更新管理工具，可修改：

```text
HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore
EnableOTA
```

它还提供结束 `nvngx_update.exe` 的操作。正常安装不需要运行；不是 DLSS 解锁或性能补丁。恢复设置应使用 Restore default，删除 BAT 不会撤销注册表修改。

本次只阅读和比对脚本，没有执行它。

### 5.4 普通屏幕测试方式

以原生输出 5120×1440 为目标，先使用保持相同比例的低分辨率窗口，再由 Magpie 放大：

| 游戏窗口 | 相对原生像素量 | 观察重点 |
| --- | --- | --- |
| 3840×1080 | 56.25% | 较温和的减负，检查 HUD、远处飞机和细线 |
| 2560×720 | 25% | 更强减负，重建与文字清晰度压力更大 |

游戏内部 Scaling 先保持 100%，避免先由游戏空间放大、再由 Magpie 二次处理。降低的是游戏窗口分辨率，不只是内部 Scaling。

先测 SR 单独开启，再加入 FG 2×，最后决定是否需要 NR。光流、捕获、SR、FG 和 NR 都有成本，不能由像素量直接推算帧率倍数。

Magpie 处理桌面窗口或镜像，不代表减少 VR 头显左右眼的渲染负载。

## 6. FoveatedDLSS5

项目：<https://github.com/maximedgoulet/FoveatedDLSS5>

### 6.1 技术路线

基于 RenoDX 的 ReShade NR 插件，中心保留较高处理分辨率，周边通过非均匀压缩减少 NR 工作图像的像素数，求值后再还原。颜色、深度和运动矢量需对应同一空间映射。

它减少的是 **NR 模型处理成本**，游戏场景仍按原来的配置渲染。属于固定中心，不自动跟随眼睛。

主要参数包括中心宽高、过渡长度和边缘分辨率。其“边缘比例 50%”为每轴比例；若两个方向都达到该比例，角落的像素密度约为 25%。

### 6.2 限制

- 中心区域压缩仅用于 Post-SR NR；NR 放在超分之前时，该实现回到均匀处理。
- 文档注明已测试环境为《巫师 3》DX12、RTX 4090、7680×2160。
- 未确认 AC7 或 VR 双眼兼容性。
- 深度或运动矢量格式无法正确处理时，周边可能产生涂抹。

### 6.3 性能数字的正确解读

媒体转述作者在上述环境、两次 NR 处理下约有 49→80 FPS 的结果。这比较的是开着 NR 时，是否启用周边优化，而不是与关闭 NR 的原版游戏比较。

按文档默认中心 3000×1800、过渡 400、边缘比例 50%，在 5120×1440 下纵向中心覆盖全高，仅横向压缩，工作像素约保留 83.2%。该数值是按文档映射推算的处理量，不是本机测量，也不是 FPS 预测。

## 7. CheekyFoveatedDLSS

项目：<https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS>

### 7.1 技术路线

它不仅优化 NR，也能对 **DLSS SR 本身**分区处理。中心运行主要 DLSS，周边可使用较低处理比例和较快预设的辅助 DLSS。

提供普通屏幕 Standalone 和 UEVR 插件，支持固定中心及兼容头显的眼动追踪。没有眼动追踪硬件也可使用固定中心；OpenXR VR 安装另有对应 layer 要求。

设置文档中的默认中心宽高约为 55%/45%，周边处理比例为游戏 DLSS 输入宽高的 75%。这些是可调参数，不是 AC7 推荐值。

### 7.2 对 AC7 的意义

要求中明确写有游戏需具备 DLSS SR。它不会自动为 AC7 原版创建缺失的 DLSS 数据入口。

如果 AC7VRDLSS 已经运行，Cheeky 值得作为后续优化研究，但尚未确认它能截获该自定义插件的 NGX 调用、正确识别双眼并与其历史管理共存。两者都支持 UEVR，并不能证明兼容。

D3D11 的 NR 功能还要求其 DX12 Transport 路径。不能把所有 D3D11/D3D12 功能视作完全对等。

### 7.3 性能宣称

README 概括某些 DLSS Performance 场景可获得 20% 以上 FPS 收益。此处保留为项目方宣称，没有将其当作 AC7 验证结果。

该方案主要减少重建处理，不会自动同比减少游戏几何、阴影和其他场景成本。中心区域过大时，收益可能很小；额外中心超采样还会增加计算量。

## 8. wilsjo2 的 OptiScaler NR 分支

项目：<https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass>

### 8.1 技术路线

提供 NR 前后置、模型分辨率、多次处理及可选 Peripheral compression。需要已有可用超分输入，不应视作 AC7 原版的通用 DLSS 注入器。

周边压缩作用于 DX12 和原生 Vulkan NR 路径，已有 DX11→DX12 bridge 沿用相关实现，后端原有约束仍然存在。

### 8.2 “90%中心视点区域”的含义

当前技术文档的默认布局是：

| 参数 | 默认 |
| --- | --- |
| 中心宽度、中心高度 | 各为图像对应轴的 80% |
| 压缩后工作宽度、工作高度 | 各为原图对应轴的 90% |
| NR 工作像素量 | 90% × 90% = 81% |

因此，90% 指工作尺寸，不是“只给中心 90% 区域插帧”。中心每轴 80% 与中心占全图面积 80% 也不是一回事。

模型分辨率是另一层全局比例：若它低于 100%，中心细节也会先减少。不能把开启周边压缩理解成中心永远保持显示器原生分辨率。

### 8.3 性能证据

项目文档报告 RTX 5090、4K、默认 80/90 布局的两次离屏测试，打包/模型/还原 GPU 时间减少约 10.6%–12.3%。每次每案例 20 次求值，排除前两次。

这不包含游戏渲染及完整宿主颜色编解码/合成成本，作者明确表示不能由此预测 FPS。“最多提升 20–30 帧”的引用未找到可复现到本机条件的原始对照证据。

## 9. RTX Encore

项目：<https://github.com/SilyNoMeta/rtx-encore>

### 9.1 功能与定位

除 FG 等功能外，它提供 Foveated NR：中心维持较强处理，周边减轻，支持 Light、Balanced、Strong 和自定义。相关功能是 NR 优化，与多帧生成是不同开关。

主要面向 RTX 20/30/40。文档对 Vulkan NR 提到实验性接纳 RTX 50，不能据此推断本机 RTX 5080＋AC7 DX11 是已确认组合。

公开仓库主要提供程序和文档，核心源码没有发布，不能按完整开源项目规划修改。

### 9.2 性能与组合限制

作者报告 Strong 在 RTX 3070 Ti Laptop 的《赛博朋克 2077》中有平均 FPS +27%，并说明边缘 NR 效果会更柔和。这不是 AC7 的测量。

文档明确不支持与 OptiScaler 的组合。其 Pre-SR 路径也有独立输入限制，不能默认将 Pre-SR、Foveated NR 和任意重建模式全部叠加。

对本机不优先选它作为 AC7 性能方案，原因是目标硬件和渲染接入条件不匹配，而不是单看其功能数量。

## 10. 针对本机的试验顺序

### 普通屏幕

1. 确认原版实际飞行帧率、垂直同步与限帧状态，建立原生 5120×1440 基准。
2. 通过 Magpie Fork 单独测试 SR：低分辨率窗口输入，保持相同比例，记录实际游戏帧率及 Magpie 处理成本。
3. 在稳定真实帧率基础上单独加入 FG 2×，区分真实帧率和输出帧率。
4. 若确实想要 NR 画面增强，先单次处理，再根据损耗考虑低工作分辨率、Pre-SR 或周边压缩。
5. Luma 留作游戏内部接入研究基线，需要飞行渲染数据后继续适配，不将现有编译产物当成完成品。

### VR

1. 先验证 AC7VRDLSS 的基础运行和画质。
2. 分别记录每眼分辨率、GPU 帧时间和头显刷新/重投影状态。
3. 再研究 Cheeky 能否与该自定义 DLSS 插件共存；先固定中心，再考虑眼动追踪。
4. 不用桌面镜像帧率或 Magpie 的输出数字替代头显渲染指标。

## 11. 性能测量要求

- 固定任务、航线、视角、天气和画质；尽量使用可重复片段。
- 对比真实渲染帧率、GPU 帧时间、低帧和处理器耗时，不能只看平均输出 FPS。
- FG 打开时记录倍率和真实输入帧率；补帧后多 20 FPS 不等于基础游戏多 20 FPS。
- NR 周边优化应比较“同样开启 NR，优化开/关”；还要保留 NR 全关基准。
- SR 测试记录实际输入/输出尺寸，不能仅凭模式名称判断。
- 检查高速转向、遮挡、云层、远处飞机、导弹尾迹、锁定框、雷达和 HUD 文字。
- 普通屏幕固定中心不跟随眼睛：查看屏幕边缘 HUD 或目标时，可能更容易察觉周边画质下降。
- 不由像素量削减比例、单个模型耗时或其他游戏的成绩推算 AC7 总帧率。

## 12. 已完成与尚未验证

已完成：

- 克隆 AC7VRDLSS、指定 UEVR 和 Luma，检查公开源码范围。
- 下载并静态检查 AC7VRDLSS 发行包，提取可读脚本、配置和文件目录。
- 编译 Luma 独立 DX11 验证模块及加入尺寸日志的实验包，完成离线完整性检查。
- 检查本地 Magpie Fork 的 NGX 调用、默认效果组、SR 效果入口及可选附件说明。
- 核对四个中心区域优化项目的原始技术文档与性能基准边界。

尚未验证：

- Magpie Fork 在本机 AC7 上的 SR/FG/NR 实际效果及实际解压后的运行环境。下载 ZIP 的效果文件、构建开关及候选导入参数已核对。
- Luma 在 AC7 中实际求值成功，以及真正低分辨率输入、高分辨率输出的游戏内适配。
- AC7VRDLSS 与 Cheeky 的共存、双眼对齐和历史管理。
- 上述组合与本机 MOZA、UE4SS、超宽屏修复的运行兼容性。
- 飞行帧率、延迟、显存、头显重投影和动态画质。

未对游戏安装进行本记录所述实验注入，未执行 NGX OTA 系统修改。不能宣称已经取得 AC7 性能收益。

## 13. 主要来源

### AC7 与接入基础

- [AC7 PC 图像与取消限帧介绍的转载报道](https://www.zddgame.com/ace-combat-7-4k-pc-gameplay-shared-by-nvidia-alongside-gpu-recommendations-and-more-21400.htm)
- [AC7VRDLSS](https://github.com/Beren5556/AC7VRDLSS-Downloads)
- [AC7VRDLSS 分发与源码范围](https://github.com/Beren5556/AC7VRDLSS-Downloads/blob/main/docs/DISTRIBUTION.md)
- [UEVR](https://github.com/praydog/UEVR)
- [Luma Framework](https://github.com/Filoppi/Luma-Framework)
- [OptiScaler 的既有超分输入前提](https://github.com/optiscaler/OptiScaler)

### Magpie

- [官方 Magpie](https://github.com/Blinue/Magpie)
- [官方内置效果](https://github.com/Blinue/Magpie/wiki/Built-in-effects)
- [Magpie Experimental](https://github.com/SAOG0721/Magpie)
- [Fork 中文说明](https://github.com/SAOG0721/Magpie/blob/experimental/README_ZH.md)
- [SR 效果定义](https://github.com/SAOG0721/Magpie/blob/experimental/src/Effects/DLSS/DLSS_SR.hlsl)
- [DLSSNR DLL 选项](https://github.com/SAOG0721/Magpie/blob/experimental/docs/DLSSNR-DLL-OPTIONS-README.txt)
- [NGX OTA 脚本](https://github.com/SAOG0721/Magpie/blob/experimental/scripts/diagnostics/NGX_OTA_Switch.bat)

### 中心区域优化

- [FoveatedDLSS5](https://github.com/maximedgoulet/FoveatedDLSS5)
- [FoveatedDLSS5 数学与实现说明](https://github.com/maximedgoulet/FoveatedDLSS5/blob/main/src/addons/dlss5/FOVEATION.md)
- [FoveatedDLSS5 性能报道；属于媒体转述](https://videocardz.com/newz/modders-bring-vr-style-foveated-rendering-to-dlss-5-boosting-rtx-4090-performance-by-over-60-on-ultrawide-monitors)
- [CheekyFoveatedDLSS](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS)
- [Cheeky 设置与 NR 说明](https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS/blob/master/USAGE.md)
- [wilsjo2 分支](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass)
- [wilsjo2 周边空间压缩与测量](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/blob/main/docs/NR-SPATIAL-COMPRESSION.md)
- [RTX Encore](https://github.com/SilyNoMeta/rtx-encore)
- [RTX Encore 中心区域设置](https://github.com/SilyNoMeta/rtx-encore/blob/main/docs/SETTINGS.md)
- [RTX Encore NR 管线](https://github.com/SilyNoMeta/rtx-encore/blob/main/docs/NEURAL-RENDERING.md)
- [RTX Encore 硬件与 API 条件](https://github.com/SilyNoMeta/rtx-encore/blob/main/docs/COMPATIBILITY.md)
