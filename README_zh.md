# AC7 DLSS Enhancer

[English](README.md) | 中文

为《皇牌空战7》加入 DLSS 超分辨率、神经渲染、可选锐化及 NVIDIA 帧生成。

## 安装

需要 Windows x64、支持所启用功能的 NVIDIA 显卡和驱动，以及已单独安装的 x64 ASI Loader。不需要 UE4SS；可以保留已有的宽屏和力反馈模组。

退出游戏，运行 `Install.cmd`。安装器自动查找 Steam 游戏目录，也可以指定：

```powershell
.\Install.cmd -GameDirectory 'F:\SteamLibrary\steamapps\common\ACE COMBAT 7'
```

安装器保留已有的 `AC7DLSS.ini`。首次从旧版升级时，会转换 `AC7NativeModels.ini` 的设置，并备份旧 ASI，避免重复加载；旧模型、日志和截屏保留。备份位于本安装包的 `backups/`。

也可手动复制：

| 安装包内容 | 游戏目录中的位置 |
|---|---|
| `payload/AC7DLSS.asi` | `Ace7Game.exe` 旁 |
| `payload/AC7DLSS/` | 同名目录，包含全部运行库 |
| `presets/AC7DLSS.ini` | `Ace7Game.exe` 旁的 `AC7DLSS.ini` |

手动升级时先移走旧 `AC7NativeModels.asi`，保留备份，不要同时加载两版。不要替换 ASI Loader、UE4SS 或其它模组。

## 设置

编辑游戏目录中的 `AC7DLSS.ini`，按 **Ctrl+Shift+F11** 应用。

规范化配置保存在 [presets/AC7DLSS.ini](presets/AC7DLSS.ini)，安装时作为起始 preset；不会在每次更新时覆盖已有设置。

| 分组 | 用途 |
|---|---|
| `SuperResolution` | DLSS 开关、画质模式和锐化 |
| `NeuralRendering` | 神经渲染强度、预设和屏幕边缘减负 |
| `FrameGeneration` | 帧生成开关与倍率 |

关闭游戏内 FXAA。`QualityMode` 的 0／1／2 分别为质量／平衡／性能。

`OptimizeEdges` 降低屏幕边缘的神经渲染处理质量以减负；`CenterAspectRatio` 指定保留完整质量的中央区域比例，`EdgeQuality` 越高保留的边缘细节越多，`BlendWidth` 控制过渡宽度。

帧生成 `Multiplier=0` 为 Auto，`1` 不插帧，`2` 及以上为固定总倍率。Auto以游戏帧率上限为目标，无上限时跟随刷新率；垂直同步优先。固定倍率保留游戏原有限帧。超出支持范围时采用最高支持倍率；不支持动态Auto时采用最高固定倍率，不支持FG则停用。

`Enabled=1` 后按游戏状态自动启停，无需进入关卡再按快捷键。已有记录确认SR、NR、固定2倍和Auto额外呈现；自动菜单／暂停切换尚未在记录中覆盖，未量化性能收益。

| 快捷键 | 功能 |
|---|---|
| Ctrl+Shift+F9 | 临时切换帧生成总开关 |
| Ctrl+Shift+F10 | 切换SR／NR／锐化效果，不切换帧生成 |
| Ctrl+Shift+F11 | 重载配置 |
| Ctrl+Shift+F12 | 保存一次处理前后截屏，不包含生成帧 |

日志为 `AC7DLSS-*.log`，截屏位于 `AC7DLSSCaptures/`。发生意外错误时会记录原因并终止，不静默掩盖；反馈问题时保留对应日志。

## 卸载

退出游戏，运行 `Uninstall.cmd`，或使用相同的 `-GameDirectory` 参数指定游戏目录。卸载移除本模组的ASI和8个运行库，保留配置、缓存、日志、截屏、旧版文件及其它模组。

## 源码与构建

需要 Visual Studio 2022 C++工具、Windows SDK、CMake，以及 `third_party/` 中的依赖。二进制SDK和模型不随Git源码保存，来源见 [依赖说明](third_party/README.md)。

```powershell
.\scripts\Build.ps1 -CMake 'cmake'
.\scripts\Package.ps1
```

构建产物位于 `payload/`；安装ZIP和SHA-256文件位于 `releases/`。构建和打包脚本不部署或启动游戏。

`src/` 为正式源码，`presets/` 为配置，`scripts/` 为发布工具，`licenses/` 为第三方许可证。`development/` 保存本地开发文档、历史和捕获，不进入安装包；`build/` 只保存可重建产物。
