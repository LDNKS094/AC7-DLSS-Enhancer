# AC7 DLSS Enhancer

[English](README.md) | 中文

为《皇牌空战 7》加入 DLSS 神经渲染、超分辨率以及帧生成。

安装方式可选[脚本安装](#脚本安装)或[手动安装](#手动安装)。

## 工作原理

模组在游戏原有的渲染流程中取出场景颜色、深度、运动矢量和相机参数，交给 NVIDIA 的神经渲染（NR）、超分辨率（SR）和帧生成（FG）处理，再把结果交回游戏。同时启用时，顺序为 **NR → SR → FG**：NR 先在低分辨率场景上恢复 HDR 颜色，SR 再重建到目标分辨率，由游戏合成后，FG 再在最终呈现前依据游戏帧及其运动矢量生成中间帧。

NR 的计算量随输入像素增长，因此除 SR 降低基础渲染分辨率外，还在横向做非均匀压缩：画面中央保持完整采样，向两侧采样密度平滑降低，使 NR 在较窄的工作域内求值，结果再映射回原始宽度交给 SR。

## 安装

### 前置依赖

| 依赖 | 说明 |
| --- | --- |
| NVIDIA GeForce RTX 显卡 | 不同系列支持的功能见下表 |
| [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)（x64） | 加载 ASI 模组，需单独安装 |
| NVIDIA DLSS SR／NR 模型 | 随包提供，遵循 NVIDIA SDK 许可证 |
| NVIDIA Streamline 运行库 | 随包提供，遵循原许可证及第三方许可声明 |
| NVIDIA DLSS FG 运行库 | 需自行从官方 Streamline SDK 2.14.16 获取 |

安装前，从 [Streamline SDK 2.14.16](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.16) 的 `bin/x64/nvngx_dlssg.dll` 取得正式运行库，放入解压后的安装包 `payload/AC7DLSS/`，再执行下方安装步骤。该文件暂不随本项目分发。

根据 [NVIDIA 官方支持表](https://www.nvidia.com/en-us/geforce/technologies/dlss/#compatibility)，各代显卡的功能支持范围如下；模组实际可用的功能与最高倍率由 SDK 查询决定。

| 功能 | RTX 20／30 系 | RTX 40 系 | RTX 50 系 |
| --- | --- | --- | --- |
| 超分辨率（SR） | 支持 | 支持 | 支持 |
| 神经渲染（NR，DLSS 5） | 不支持 | 不支持 | 支持 |
| 普通帧生成（FG） | 不支持 | 支持 | 支持 |
| 多帧生成／动态 Auto | 不支持 | 不支持 | 支持 |

**NR目前仅在RTX 50系上获得官方支持。** 

### 脚本安装

运行 `Install.cmd`，安装器自动查找 Steam 中的游戏目录。

找不到游戏时指定目录：

```powershell
.\Install.cmd -GameDirectory 'G:\Games\ACE COMBAT 7'
```

安装器不会覆盖已有的配置文件。

### 脚本卸载

运行 `Uninstall.cmd`，将自动移除本补丁添加的模组文件和配置文件。

需要指定目录时：

```powershell
.\Uninstall.cmd -GameDirectory 'G:\Games\ACE COMBAT 7'
```

### 手动安装

复制以下文件到游戏目录：

| 安装包内容 | 复制到 |
|---|---|
| `payload/AC7DLSS.asi` | 游戏根目录 |
| `presets/AC7DLSS.ini` | 游戏根目录 |
| `payload/AC7DLSS/*.dll` | 游戏根目录下的 `AC7DLSS/` |

### 手动卸载

直接删除以上文件。

## 使用与设置

安装完后直接启动游戏即可。

需要更改设置时，编辑游戏目录中的 `AC7DLSS.ini`，按 **Ctrl+Shift+F11** 实时应用。

按 **Ctrl+Shift+F10** 可以总体启停整个模组处理。

| 分组 | 用途 |
|---|---|
| `SuperResolution` | DLSS 开关、画质模式和锐化 |
| `NeuralRendering` | 神经渲染强度、预设和屏幕边缘减负 |
| `FrameGeneration` | 帧生成开关与倍率 |

建议关闭游戏内 FXAA，DLSS已经能很好的处理锯齿。

`OptimizeEdges` 降低屏幕边缘的神经渲染处理质量以减负。
`CenterAspectRatio` 指定保留完整质量的中央区域比例。
`EdgeQuality` 越高保留的边缘细节越多。
`BlendWidth` 控制过渡宽度。

`Multiplier` 控制帧生成倍率，默认为 Auto，以游戏帧率上限为目标；`2` 及以上为固定总倍率，保留游戏原有限帧。超出支持范围时回退最高支持倍率。

## 从源码构建

环境：Visual Studio 2022（C++ 工具与 Windows SDK）、CMake。

外部模型与预编译库未提交到 Git，首次克隆后需自行准备。下表路径均相对于项目根目录；SDK 头文件和 MinHook 源码已在仓库中。

| 依赖来源 | 放置路径 |
| --- | --- |
| [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) 的正式 SR 模型；项目使用版本 `310.9.16.0` | `third_party/models/nvngx_dlss.dll` |
| [RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix) 的 NGX／NR SDK，当前使用归档 `rtx-remix-ngx_sdk_dlnr@2.7z` 中的正式 NR 模型，版本 `310.9.16.0` | `third_party/models/nvngx_dlssnr.dll` |
| [RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix) 的 NGX／NR SDK 的 `nvsdk_ngx_d.lib` | `third_party/ngx/lib/nvsdk_ngx_d.lib` |
| [Streamline SDK 2.14.16](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.16) 的 `bin/x64/` 正式运行库 | `third_party/streamline/bin/x64/` |

各组件遵循其原许可证。构建会将 ASI 和上述运行库放入 `payload/`。

在项目根目录执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build.ps1
```

产物：

```text
payload/
  AC7DLSS.asi
```

编译后可按常规安装方式部署使用。

## 致谢

- [OptiScaler](https://github.com/optiscaler/OptiScaler)：参考其 D3D11／D3D12 交换链包装、共享资源与 GPU 资源退休思路。

## 许可证

本项目原创代码采用 [MIT 许可证](LICENSE)。第三方代码、NVIDIA 模型与运行库遵循各自许可证，不适用本项目的 MIT 授权，详见 [第三方许可说明](licenses/README.md)。
