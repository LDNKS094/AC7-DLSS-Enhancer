# 最小 D3D11 捕获对照结果

后续结果：用户执行最后的尺寸参数试验后仍为相同设备丢失；PID108636，未取得实际720p尺寸证据，按约定停止进一步RenderDoc试启动。见 [阶段收束](AC7-RenderDoc-Stop-Decision.md)。

## 已完成

源码：`main.cpp`（清理时已移除的实验文件），构建：`build.cmd`（清理时已移除的实验文件）。使用已安装 VS2022 x64 编译器、Windows SDK、D3D11/DXGI/D3DCompiler，以及已核对的官方 1.45 应用 API 头文件，无新增库依赖。

程序创建 960×540 窗口、硬件 feature level 11.0 设备、双缓冲 legacy discard 交换链；编译 VS/PS，使用 16 字节常量缓冲绘制旋转彩色三角形，调用 Present(1,0)。自检最多 120 帧/15 秒，检查创建 HRESULT、Present 和 GetDeviceRemovedReason。被 RenderDoc 注入时在第 60 次循环触发一次捕获，检查捕获计数恰为 1。

实测结果：

| 项目 | 结果 |
| --- | --- |
| MSVC /W4 编译 | 通过，无输出警告 |
| 无 RenderDoc 运行 | NVIDIA GeForce RTX 5080，feature level 11.0，120 帧，设备 S_OK |
| 官方 RenderDoc 1.45 注入运行 | 120 帧，设备 S_OK，成功保存 frame61 捕获 |
| .rdc | `artifacts/diagnostics/d3d11-probe-capture_frame61.rdc`，269787 字节 |
| 官方 CLI 重放 | `replay --loops 3`，退出码 0 |
| 捕获结构检查 | XML 转换成功，包含 UpdateSubresource、ClearRenderTargetView、Draw 和 Present |

这些是基础捕获与重放的实际 GPU 结果，不是 AC7 捕获成功，也不是对全部 D3D11 功能的覆盖。未做三角形像素的自动颜色校验。程序仅使用简单单线程图形工作，没有模拟 UE4 延迟上下文、复杂资源、trueSKY、Aftermath 或其初始化顺序。它也不是 AC7 完全相同的交换链/显示配置。

## 判断更新

基础 D3D11 链路在当前显卡/驱动/RenderDoc 1.45 上可用。降低“RenderDoc 在这台机器上完全不能用”的解释，聚焦 AC7 具体初始化、资源、API 使用及其与捕获包装/驱动的交互。复杂路径才触发的驱动问题仍未排除。

## 最后一项低成本 AC7 对照：资源尺寸

候选假设：AC7 当前 5120×1440 对应的早期资源/呈现尺寸，与捕获包装交互时触发问题。低分辨率可检验尺寸依赖性，但没有证据将它叫作显存不足；降低尺寸也可能改变内部工作/时序，成功不直接定位责任。

已准备 `Launch-AC7-720p-Validation.cmd`（清理时已移除的实验文件），相对上一轮 CLI 只新增 `-ResX=1280 -ResY=720`。仍使用窗口化、1.45、同一 App ID、工作目录、验证/取消静音参数和同一驱动；不恢复 mod、不改 HDR、NvAPI 或系统设置。

运行前已把当前 GameUserSettings.ini 备份到 `artifacts/backups/before-720p-test/`；若游戏保存显示设置，不自动覆盖用户的后续更改。

步骤：

1. 正常退出当前 AC7；只执行一次 720p 脚本。若需收集验证消息，先运行原 Collect-AC7-D3D11.cmd，但不要重复启动多个收集器。
2. 若成功进入菜单，确认实际窗口/分辨率是否为 1280×720，再考虑最小飞行捕获。不能仅凭命令行断定游戏采用了尺寸。
3. 若参数已生效且仍相同设备丢失，按此前止损约定暂停 RenderDoc 路线，不再扩散试版本/改驱动。
4. 若主窗口出现前就失败、无法确认参数采用，结果只记为不充分，不当作排除了尺寸假设；同样先停下来评估其他原生观察方式，不要求无限补测。

这项 AC7 参数测试已由用户执行，报告无区别，输出仍为0x887A0020。实际尺寸未确认，不能据此完整排除尺寸假设；按上述止损条件暂停RenderDoc，不再追加补测。
