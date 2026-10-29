# RenderDoc 调查归档

这里保留停止使用 RenderDoc 前的调查记录与原始诊断证据。文档内的进程、现场状态及下一步描述均为当时情况，不代表当前环境。

当前结论见 [RenderDoc 状态](../../docs/AC7-RenderDoc-Status.md)。实际工作使用 Nsight Graphics；原始补丁已恢复。

- `docs/`：实验调查和配置状态记录。
- `diagnostics/`：日志、转储及最小 D3D11 测试的捕获证据。
- `user-output.log`：用户提供的原始输出。

RenderDoc 专用脚本、注册表导入配置、启动配置、三角形测试程序源码/二进制和下载工具已移除。历史记录中的这些文件路径仅作实验说明，不是当前操作入口。游戏补丁备份仍在项目 `artifacts/backups/`，未随本次清理移除。
