# 正式 NR310.9.16.0：初始化与创建阶段记录

> 后续验证已完成：[真实AC7单帧SDR Evaluate](AC7-NR-Single-Frame-Evaluation.md)通过GPU完成、输入逐位读回、有限且非恒等输出和debug检查。本页保留之前初始化/创建阶段的完整结果、scratch错误与SDK副作用，不再将“尚无Evaluate”作为当前阻塞。

## 当前结果

RTX5080、驱动617.42、NVIDIA签名有效的正式NR310.9.16.0，使用官方NGX loader与本项目自己的CUSTOM Project ID：**D3D11 NR报告不可用，D3D12 NR初始化、创建、命令提交/GPU完成及清理通过**。本页对应的早期探针没有Evaluate或输出图像；后续求值结果见页首链接。

这是当前确切运行时/驱动/API组合的结果，不是所有NR版本的永久限制。此前两个API的GetFeatureRequirements均通过，说明单独的需求查询不足以证明特性实际可用。

| 阶段 | D3D11 | D3D12 |
| --- | --- | --- |
| 独立硬件设备创建 | S_OK | S_OK |
| NGX Init_with_ProjectID | Success | Success |
| GetCapabilityParameters | Success | Success |
| 实际加载NR路径 | 与已校验官方文件一致 | 与已校验官方文件一致 |
| DLSSNR.Available | **0** | **1** |
| DLSSNR.FeatureInitResult | **0xBAD00001 FeatureNotSupported** | **0x1 Success** |
| NeedsUpdatedDriver | 0 | 0 |
| 特性返回的最低驱动 | 615.0 | 615.0 |
| GetScratchBufferSize | 不调用：特性不可用 | **0xBAD00002 PlatformError** |
| 独立CreateFeature验证 | 不尝试：分支已停止 | **Success，非空handle** |
| 创建命令提交及GPU fence | 不适用 | **completed=1** |
| ReleaseFeature | 不适用 | Success |
| DestroyParameters / Shutdown1 | Success / Success | Success / Success |
| Evaluate / 图像 / AC7实时接入 | 未做 | 未做 |

D3D11退出5表示到达特性不可用的明确停止条件；D3D12 scratch探针退出6保留其失败；另一次D3D12创建探针退出0。不能把这些进程退出码混写为设备崩溃或全链路通过。

## 为什么保留scratch错误但仍做创建验证

公开NGX文档说明，多数当前特性不使用scratch，是否需要由具体特性确定。正式包内的`nvsdk_ngx_helpers_dlssnr_vk.h`只设置`DLSSNR.Width/Height`并直接调用Vulkan CreateFeature，没有GetScratchBufferSize或外部分配。

据此进行了**一次独立的D3D12通用公开CreateFeature合同验证**：使用同一loader、运行时、ID、能力参数图和2560×1440尺寸，不调用snippet专用ABI、不修改DLL、不改调用模块名，不添加猜测模型preset、输出尺寸或scratch缓冲区。没有把Vulkan资源结构传给D3D12。专用D3D helper在该正式包里缺失，因此这里只声称这组通用创建调用经实测有效。

创建返回成功后才关闭并提交direct命令列表，Signal fence并最多等待10秒；完成后释放feature。未完成时探针将退出并避免显式释放仍在使用的资源。实测未触发超时/设备移除。该成功说明**此创建配置无需先取得scratch查询成功**，不是把PlatformError改写为Success，也不证明其他配置同样成立。

## 工具和证据

[preflight.cpp](../tools/nr/preflight.cpp)及[独立CMake入口](../tools/nr/CMakeLists.txt)，不属于游戏hook或运行时后端。

```text
preflight-nr.exe --self-test
preflight-nr.exe d3d11 <正式NR目录> <独立日志目录> <本项目GUID>
preflight-nr.exe d3d12 <正式NR目录> <独立日志目录> <本项目GUID>
preflight-nr.exe d3d12-create <正式NR目录> <独立日志目录> <本项目GUID>
```

必须显式提供官方头路径（含`nvsdk_ngx_defs_dlssnr.h`）及官方x64`nvsdk_ngx_d.lib`，工具不下载或提交SDK。自检验证合法/非法GUID，不调用NGX。硬件探针每个API各用独立进程；D3D12创建使用另一个独立进程，原始scratch失败没有覆盖。

- [D3D11初始化与能力原始记录](../artifacts/research/official-nr/preflight-d3d11.json)
- [D3D12初始化与scratch原始记录](../artifacts/research/official-nr/preflight-d3d12.json)
- [D3D12创建与GPU完成原始记录](../artifacts/research/official-nr/create-d3d12.json)
- [身份及阶段汇总](../artifacts/research/official-nr/artifact-identity.json)
- [结果断言与SHA256复核](../artifacts/research/official-nr/preflight-checks.json)

独立目标编译成功；GUID自检通过；对原始输出的断言验证D3D11停止原因、D3D12 scratch错误、创建/完成/清理成功，重新计算运行时SHA256与先前身份记录一致。没有为文档更新重复跑模型或修改原始捕获。

## NGX本身的副作用

这些初始化探针**不是完全只读**。虽然程序仅显式创建项目内日志、设备和NGX对象，官方NGX core日志记载：

- 维护`C:\ProgramData\NVIDIA\NGX\models`，包括目录ACL；没有初始化前快照，不能量化与原状态的具体差异。
- 查找特性缓存，启动驱动目录内官方`nvngx_update.exe`。
- 记录telemetry初始化/关闭及OTAEnabled=1。仅凭日志不能证明实际上传/下载了哪些内容。

我们未手工改驱动、注册表、缓存权限、OTA配置或游戏；也不为清理这些SDK行为删除系统缓存或回滚未知ACL。后续再次初始化应沿用这一已知边界，不能继续写成“所有变化仅在项目目录”。原始文件、游戏补丁与捕获保持原状；无Git提交。

## 对后续路线的影响

- 本版本的纯D3D11 NR分支停止；已有D3D11输入规范化模块与官方D3D11 SR接口仍有效。
- D3D12 NR从“仅候选源码”推进为“本机正式运行时创建/GPU完成已验证”，可以作为下一阶段图像求值的执行API。
- 上述阶段之后，**D3D12独立真实AC7单帧SDR图像Evaluate、GPU完成与读回已通过**，见页首最新记录；多帧、完整输入语义及Pre-SR验收仍未完成。
- 同步/资源寿命、真正Pre-SR、周边压缩、SR与FG呈现仍需各自验证；没有据此选定完整宿主或进程架构，也没有提前建设桥。

正式文件的来源链、许可和SR/FG边界见 [图形API与正式运行时](AC7-Graphics-API-Support.md)。
