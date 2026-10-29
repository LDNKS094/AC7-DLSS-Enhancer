# AC7：避开启动期调试器的退出记录

当前优先进行 [无 mod 的干净游戏对照](AC7-RenderDoc-Clean-Baseline.md)。下述 Silent Process Exit 配置尚未导入，也不作为这次干净基线测试的前提。

## 当前状态

ProcDump 启动期异常监视引入了不同失败：用户看到 `Steam error: Application load error T:0000065432`。本轮 PID 97748 的 RenderDoc 日志只有初始化及握手，没有前两次的 D3D11 设备创建/Presenting。

已取得的 `artifacts/diagnostics/renderdoc-exit-dump/Ace7Game.exe_261027_052016.dmp` 为 14,361,374 字节。按 Windows minidump 格式读取流目录、进程信息和注释后确认：PID 97748，注释为 `Process termination`，没有 ExceptionStream。它是新失败的终止快照，不是原先渲染阶段崩溃的异常栈。这里只完成转储元数据检查，没有用符号调试器完成调用栈分析。

ProcDump 已自行结束，已移除临时 `steam_appid.txt`；UE4SS 代理恢复，四个原始加载器/插件哈希匹配，没有 `renderdoc.asi`。当前游戏目录恢复到试验前文件状态，恢复后的正常启动尚未再次验证。

启动期调试监视可能改变 Steam 的加载行为，这是基于这次前后差异的怀疑，不是对 T:0000065432 的官方解码。没有找到可核实的资料能单凭该代码确定原因；不据此要求重装 Steam、替换 steam_api DLL 或关闭安全设置。

## 下一项候选：Windows Silent Process Exit Monitoring

微软官方文档：

- https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/registry-entries-for-silent-process-exit
- https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/enable-silent-process-exit-monitoring

该机制监控目标调用 `ExitProcess` 自行终止，或被其他进程调用 `TerminateProcess` 终止。可以配置为由 Windows 生成本地转储，不需要提前将 ProcDump 以异常调试器方式附加到进程。

限制：不监控最后一个线程退出造成的正常终止，也不监控内核模式代码发起的终止。它仍会为目标设置 GlobalFlag，不能保证对游戏行为完全无影响；尚未在本机 AC7 验证。

## 已准备，尚未启用

当前会话没有管理员权限，只读检查确认以下两个 **Ace7Game.exe 子键均不存在**：

```text
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\Ace7Game.exe
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\SilentProcessExit\Ace7Game.exe
```

只为这个前提准备了下列启用/回退配置：

- `Enable-Silent-Exit.reg`（清理时已移除的实验文件）：新建上述两个单进程子键，设置 GlobalFlag=0x200、ReportingMode=2、IgnoreSelfExits=0、DumpType=0（普通小型转储）及项目内输出目录。
- `Disable-Silent-Exit.reg`（清理时已移除的实验文件）：删除本次新建的两个子键。**仅适用于这两个键仍只含本次设置的情况**；若期间其他工具往其中添加了配置，不应直接使用此回退文件，应先保留并逐项恢复。没有改动或删除任何父键。

没有自动导入，没有修改注册表。`.reg` 导入需要 Windows 的管理员确认。输出目录已准备：

```text
E:\CodingProject\AC8_MOZA\AC7-Native-Adapter\artifacts\diagnostics\ac7-silent-exit
```

## 启用后的协同复现步骤

1. 关闭 AC7；确认 ProcDump 不再运行。若两个目标注册表键已被其他工具创建，先停止，重新核对而非覆盖。
2. 由用户以管理员权限导入 Enable-Silent-Exit.reg。
3. 重新复制本项目准备的 `steam_appid.txt`（清理时已移除的实验文件） 到游戏 EXE 同目录，内容仅为 502500；游戏目录若有不同来源的同名文件，不覆盖。
4. 使用原 `RenderDoc Launch 配置`（清理时已移除的实验文件） 复现一次，不运行 ProcDump，不新增其他变量。若出现弹窗，保留原文并记录时间。
5. 检查本地输出目录及对应 Windows 事件；即使有 dump，也要核对 PID、触发原因、线程和模块，不能直接认定原因。
6. 退出游戏，由用户导入 Disable-Silent-Exit.reg（先确认键中没有其他工具的新设置），移除本次临时 App ID 文件，并验证两个临时键已清除。正常游戏不应长期启用该诊断配置。

此流程仍是准备好的候选诊断，不代表已经捕获到游戏帧或解决原始退出问题。
