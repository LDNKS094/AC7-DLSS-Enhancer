# Nsight 2025.5 最小 D3D11 首测

> 历史记录：Nsight启动资格与首帧导出阶段，非当前操作计划。后续两份C++ Capture、GPU旁路及正式NR查询已完成；当前状态以 [项目README](../../README.md) 和 [API核对](../../docs/AC7-Graphics-API-Support.md) 为准。下文进程ID、等待导出和恢复步骤均为当时现场。

结果：用户确认已完成启动、Pause and Capture Frame及前述检查操作，无问题。基础Frame Debugger资格通过；尚未由助手导出/离线复核。ERR_NVGPUCTRPERM仅提示性能分析能力受限，不为此改变权限。下一步见 [AC7无mod首启动](AC7-Nsight-AC7-First-Launch.md)，先到菜单核对连接，再抓飞行帧。

本机已确认安装 NVIDIA Nsight Graphics 2025.5.0，DisplayVersion=25.5.25335.2111。运行 UI PID64592，路径为 `D:\Application\Nsight Graphics 2025.5.0\host\windows-desktop-nomad-x64\ngfx-ui.exe`。这只是安装/启动验证，尚未捕获。

1. 在 Start Activity 进入活动配置，选择 **Frame Debugger**。不选 Graphics Capture、GPU Trace 或 Shader Debugger。
2. Launch 页设置：

   ```text
   Application Executable:
   E:\CodingProject\AC8_MOZA\AC7-Native-Adapter\artifacts\tools\d3d11-probe\d3d11-probe.exe

   Working Directory:
   E:\CodingProject\AC8_MOZA\AC7-Native-Adapter\artifacts\tools\d3d11-probe

   Command Line Arguments:
   留空（不要填 --self-test）

   Automatically Connect:
   Yes
   ```

3. 不通过 RenderDoc 或之前 AC7 的验证脚本启动，不开启 D3D debug runtime。直接在 Nsight 点击 Launch。
4. 应看到深色背景上的旋转彩色三角形，客户端尺寸960×540。让窗口保持运行，不按 Esc 或关闭。
5. 在 Nsight 主工具栏使用 **Pause and Capture Frame**。快捷键可按应用 HUD 实际提示使用，不假定沿用 RenderDoc 的 F12。
6. 捕获后核对事件中有 Draw(3,0)，目标纹理960×540，D3D11 API Inspector能看到VS/PS及VS常量缓冲。截图或告诉助手实际界面/报错；先不启动AC7。

成功门槛：不仅显示HUD或连接成功，而是能停止在一帧、浏览绘制事件并查看实际资源/管线状态。程序启动成功但不能捕获，仍不算通过。

窗口保持运行便于助手核对PID/模块，普通运行不会调用RenderDoc捕获API（只有self-test且RenderDoc实际注入时才会触发）。

若报不支持、设备丢失或无法连接，记录原文，不修改驱动/权限或切换活动凑成功。首测失败则暂停AC7测试。首测成功后再准备一次独立的AC7无mod首测。
