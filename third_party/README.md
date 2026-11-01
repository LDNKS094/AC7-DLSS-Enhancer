# 第三方依赖

正式构建不再从 `development/build`、历史实验包或相邻项目取依赖。CMake 默认使用本目录，仍允许显式覆盖路径。

| 路径 | 用途与本机来源 |
|---|---|
| `minhook/include`、`minhook/src`、`minhook/LICENSE.txt` | 原样复制自相邻 AC7-Ultrawide-Fix 的 MinHook 依赖，供窄 hook 使用；保留原许可证 |
| `ngx/include` | NVIDIA Remix NR SDK 的 NGX／NR 头文件；配套公共 `nvsdk_ngx_defs.h` 从已保存的官方 Streamline v2.14.16 归档 `external/ngx-sdk/include` 原样提取，保留文件原版权声明 |
| `ngx/lib/nvsdk_ngx_d.lib` | 此前实际构建使用的 NVIDIA NGX loader，来自原 `artifacts/research/official-nr` |
| `ngx/LICENSE.txt` | 随原 Remix SDK 保留的许可证 |
| `models/nvngx_dlss.dll` | 原 `artifacts/research/official-sr` 的 SR 模型原件 |
| `models/nvngx_dlssnr.dll` | 原 Remix SDK `lib/Windows_x86_64/rel` 的 NR 模型原件 |
| `downloads` | 原 SDK 下载归档，保留原文件名；不是 CMake 输入，也不据文件名宣称已完成 Streamline 接入 |

本目录不放本项目业务实现。自有核心仍在根 `src/`，ASI 游戏接线在 `development/src/` 和 `development/tools/neutral-asi/`。没有修改模型或 NGX core。

模型和 loader 二进制、下载归档不进入 Git；它们是本机需要保留的依赖，不能随 build 清理。ASI 的运行契约不变：由用户将模型放在 ASI 旁的 `AC7NativeModels/`。本次整理没有替换游戏目录文件。
