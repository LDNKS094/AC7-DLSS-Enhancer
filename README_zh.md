# AC7 DLSS Enhancer

[English](README.md) | 中文

为《皇牌空战7》加入 DLSS 神经渲染、超分辨率以及帧生成。

安装方式可选[脚本安装](#脚本安装)或[手动安装](#手动安装)。

## 工作原理

模组在游戏原有的渲染流程中取出场景颜色、深度、运动矢量和相机参数，交给 NVIDIA 的神经渲染（NR）、超分辨率（SR）和帧生成（FG）处理，再把结果交回游戏。同时启用时，顺序为 **NR → SR → FG**：NR 先在低分辨率场景上恢复 HDR 颜色，SR 再重建到目标分辨率，由游戏合成后，FG 再在最终呈现前依据游戏帧及其运动矢量生成中间帧。

NR 的计算量随输入像素增长，因此除 SR 降低基础渲染分辨率外，还在横向做非均匀压缩：画面中央保持完整采样，向两侧采样密度平滑降低，使 NR 在较窄的工作域内求值，结果再映射回原始宽度交给 SR。

## 安装

### 前置依赖

| 依赖 | 说明 |
| --- | --- |
| NVIDIA GeForce RTX 显卡及驱动 | 不同系列支持的功能见下表，需安装支持相应功能的驱动 |
| [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)（x64） | 加载 ASI 模组，需单独安装 |

根据 [NVIDIA 官方支持表](https://www.nvidia.com/en-us/geforce/technologies/dlss/#compatibility)，各代显卡的功能支持范围如下；模组实际可用的功能与最高倍率由 SDK 查询决定。

| 功能 | RTX 20／30 系 | RTX 40 系 | RTX 50 系 |
| --- | --- | --- | --- |
| 超分辨率（SR） | 支持 | 支持 | 支持 |
| 神经渲染（NR，DLSS 5） | 不支持 | 不支持 | 支持 |
| 普通帧生成（FG） | 不支持 | 支持 | 支持 |
| 多帧生成／动态 Auto | 不支持 | 不支持 | 支持 |

**NR目前仅在RTX 50系上获得官方支持。** RTX 20／30／40系用户需将配置中 `[NeuralRendering]` 下的 `Enabled` 设为 `0`。

安装完成后，游戏根目录的结构应该是：

```text
ACE COMBAT 7/
  Ace7Game.exe
  dinput8.dll            ASI Loader
  AC7DLSS.asi            本模组
  AC7DLSS/               运行库
  AC7DLSS.ini            设置
```

### 脚本安装

运行 `Install.cmd`，安装器自动查找 Steam 中的游戏目录。

找不到游戏时指定目录：

```powershell
.\Install.cmd -GameDirectory 'F:\SteamLibrary\steamapps\common\ACE COMBAT 7'
```

安装器保留已有的 `AC7DLSS.ini`。被覆盖的文件备份在本安装包的 `backups/`。

### 脚本卸载

运行 `Uninstall.cmd` 移除 `AC7DLSS.asi` 与 `AC7DLSS/`。`AC7DLSS.ini`、日志和截屏保留。默认使用安装时记录的游戏目录；
将自动移除本补丁添加的模组文件

需要指定目录时：

```powershell
.\Uninstall.cmd -GameDirectory 'G:\Games\ACE COMBAT 7'
```

### 手动安装

复制以下文件到游戏目录：

| 安装包内容 | 复制到 |
|---|---|
| `payload/AC7DLSS.asi` | 游戏根目录 |
| `payload/AC7DLSS/` | 游戏根目录 |
| `presets/AC7DLSS.ini` | 游戏根目录 |

### 手动卸载

直接删除以上文件。

## 使用与设置

编辑游戏目录中的 `AC7DLSS.ini`，按 **Ctrl+Shift+F11** 应用。Ctrl+Shift+F10 关闭总开关后各项设置仍保留；重载配置不改变总开关状态。

规范化配置位于 [presets/AC7DLSS.ini](presets/AC7DLSS.ini)，首次安装时作为起始 preset；之后的更新保留已有设置。

| 分组 | 用途 |
|---|---|
| `SuperResolution` | DLSS 开关、画质模式和锐化 |
| `NeuralRendering` | 神经渲染强度、预设和屏幕边缘减负 |
| `FrameGeneration` | 帧生成开关与倍率 |

关闭游戏内 FXAA。`QualityMode` 的 0／1／2 分别为质量／平衡／性能。

`OptimizeEdges` 降低屏幕边缘的神经渲染处理质量以减负；`CenterAspectRatio` 指定保留完整质量的中央区域比例，`EdgeQuality` 越高保留的边缘细节越多，`BlendWidth` 控制过渡宽度。

帧生成 `Multiplier=0` 为 Auto，`1` 不插帧，`2` 及以上为固定总倍率。Auto以游戏帧率上限为目标，无上限时跟随刷新率；垂直同步优先。固定倍率保留游戏原有限帧。超出支持范围时采用最高支持倍率；不支持动态Auto时采用最高固定倍率，不支持FG则停用。

`Enabled=1` 后按游戏状态自动启停。

| 快捷键 | 功能 |
|---|---|
| Ctrl+Shift+F10 | SR／NR／锐化／FG总开关，重新开启后恢复各项设置 |
| Ctrl+Shift+F11 | 重载配置 |

日志为 `AC7DLSS-*.log`，发生错误时记录原因并终止。反馈问题时请附上对应日志。

## 从源码构建

需要 Visual Studio 2022 C++工具、Windows SDK、CMake，以及 `third_party/` 中的依赖。二进制SDK和模型不随Git源码保存，来源见 [依赖说明](third_party/README.md)。

```powershell
.\scripts\Build.ps1 -CMake 'cmake'
.\scripts\Package.ps1
```

构建产物位于 `payload/`；安装ZIP和SHA-256文件位于 `releases/`。构建和打包脚本不部署或启动游戏。

`src/` 为正式源码，`presets/` 为配置，`scripts/` 为发布工具，`licenses/` 为第三方许可证。`development/` 保存开发文档、历史和捕获，不进入安装包；`build/` 为可重建的构建产物。

| 源码目录 | 职责 |
|---|---|
| `src/game/` | 原生Hook、渲染图接线、View身份和相机读取 |
| `src/presentation/` | DXGI入口、交换链桥、HUDless合成和真实帧上下文 |
| `src/rendering/` | 场景处理、颜色、运动模糊、guides及共享纹理 |
| `src/nvidia/` | NGX模型、共用GPU上下文及Streamline会话 |
| `src/settings/` | SR／NR／FG参数定义；INI读取统一在config.cpp |
| `src/input/` | 快捷键处理 |
| `shaders/` | 全部HLSL，在构建时编译并嵌入ASI |

## credits

## 许可证
