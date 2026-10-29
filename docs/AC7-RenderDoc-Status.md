# RenderDoc 当前不可用

在本机 RTX 5080、驱动 617.42 和当前 AC7 版本下，RenderDoc 1.45 / 1.46 均未能取得有效 AC7 帧。移除额外 mod 后仍出现 D3D11 设备丢失：`0x887A0020 / DXGI_ERROR_DRIVER_INTERNAL_ERROR`，调试输出包含 `D3D11: Removing Device` 和警告 #379。

最小 D3D11 程序能捕获并重放，说明基础捕获链路可用，但不能证明 AC7 兼容；现有证据也不足以把故障归因于驱动单方。RenderDoc 路线已停止，不再进行无明确新假设的版本、参数或系统设置试错。

实际调查使用 **Nsight Graphics 2025.5 Frame Debugger**，已成功捕获 AC7 飞行帧并导出 C++ Capture；重放和原生资源语义仍待验证。

实验文档和原始诊断证据归档到 [history/renderdoc](../history/renderdoc/README.md)。专用测试脚本、启动/注册表配置、测试程序和项目内下载工具已移除。补丁恢复记录和备份继续保留；本次清理未修改游戏或系统。
