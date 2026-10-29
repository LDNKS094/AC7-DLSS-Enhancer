# 独立 OptiScaler 公开接口验证

此工具不注入或启动AC7，不运行发布包的安装脚本。它加载独立目录中的标准OptiScaler.dll，经公开NGX导出执行一次或固定八帧SR，保全HDR样本。包的DllMain会安装进程级hooks，因此不是无副作用的纯SDK加载器；不要放进游戏目录。

## 构建

```powershell
cmake -S tools/optiscaler -B build/optiscaler-interface -G "Visual Studio 17 2022" -A x64 -DNGX_INCLUDE_DIR="官方SDK include目录"
cmake --build build/optiscaler-interface --config Release
```

只使用公开SDK头，不链接NGX loader；D3D12模式复用本项目SharedTextureBridge。SDK、发布包、模型DLL不入库。

## 运行输入

```text
optiscaler-interface OPTISCALER_DLL SR_RUNTIME FIXTURE_FRAME_OR_SEQUENCE NEW_OUTPUT [--fl11] [--native-dx12|--native-eight] [--reset-control]
# D3D11入口的独立单帧诊断另支持 --shutdown-base
```

- FIXTURE_FRAME为现有`artifacts/research/live-nr-sr-eight/sr-input/frame0`；包含color/depth/motion/exposure.raw与真实frame-time.bin。输入已规范化、原点0、2560×1440；不是游戏取数接口。
- 默认D3D11设备请求FL11.1；`--fl11`显式请求FL11.0。绝不因测试而改游戏设备。
- 默认使用OptiScaler公开D3D11入口与Shutdown1。`--shutdown-base`改用公开无参Shutdown；其签名取自固定公开源码，当前官方SDK头未声明旧符号。
- `--native-dx12`改用同适配器D3D12设备、项目NT共享桥和OptiScaler公开D3D12入口，使用公开无参Shutdown12，不进入OptiScaler的D3D11桥。
- `--native-eight`以sr-input根目录为输入，GPU运行前检查全部八帧文件；同一个特性/桥/输入纹理连续八次求值，Reset只在首帧。`--reset-control`改为逐帧Reset。后续原D3D11输入用UpdateSubresource，桥等待上一帧退休后才复用allocator和资源。
- 八帧运行前必须执行`python tools/optiscaler/check_sequence.py SR_INPUT_ROOT --preflight`，核40个输入SHA与原View计数/时钟/dt/jitter；EXE只检查文件布局，不能把任意同格式目录称真实序列。
- 使用Reset1、真实jitter0、P1未校准；不是超分/时域/质量验收。
- 输出目录不得存在。失败保留INCOMPLETE和raw，不生成成功标记。公开vectored异常观察器只记录，不吞异常。

## 隔离目录配置

保留发布包的backend文件，只修改测试目录中的OptiScaler.ini。D3D11入口设`[Upscalers] Dx11Upscaler=dlss_12`；D3D12入口另设`Dx12Upscaler=dlss`。SR DLL使用已核验310.9.16.0，Libraries中指定实际backend/driver loader路径。

测试必须显式关闭NR、FrameGen、overlay、更新检查、第三方插件、fakenvapi、DXGI/Streamline/Vulkan/registry/user32 spoofing；ProcessFilter只允许`optiscaler-interface.exe`。本轮完整INI与包摘要在`artifacts/research/optiscaler-interface-v1/host-*`保存。不要调用setup_windows.bat或把EXE命名为Ace7Game。

本轮已通过组合：

1. FL11.1、D3D11公开入口、`--shutdown-base`。
2. FL11.0输入、`--fl11 --native-dx12`、项目共享桥。

已观察到OptiScaler D3D11桥FL11.0恢复64个UAV槽越界，以及Shutdown1访问异常；失败目录保留。不能把成功组合推广为该包装层或游戏兼容已修复。

## 检查

```text
python tools/optiscaler/check_interface.py EVIDENCE_ROOT DIRECT_NGX_FRAME0_RAW NEW_REPORT_JSON
```

检查器核发布摘要、真实backend日志、NaN输出覆盖、RGBA有限、四输入保持、全raw对照和清理/失败标记。当前数据集含五次SR，NR0；独立八帧数据集另含历史8＋Reset8，累计SR21/NR0。

八帧完成后执行：

```text
python tools/optiscaler/check_sequence.py SR_INPUT_ROOT HISTORY_RUN RESET_RUN DIRECT_EIGHT_ROOT NEW_REPORT_JSON
```

每组日志按`RUN-process.log`、`RUN-backend.log`、`RUN-exit.json`保存在RUN旁，运行后应立即快照host中的OptiScaler.log，防止下一进程覆盖。检查器核Reset/dt、持久特性/资源、后台真实DLSS、64输入和16输出、退出/完成标记。源码颜色政策核对见[八帧报告](../../docs/AC7-OptiScaler-Sequence-Validation.md)；尚未实际执行其NR/HDR、FG/周边压缩或性能测试，未测在途停止和模块热卸载。

## D3D12颜色通路（不加载模型）

```text
cmake --build build/optiscaler-interface --config Release --target color-interface
color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT
python tools/optiscaler/check_color.py REAL_CAPTURE DIRECT_EIGHT_BASELINE RUN_DIR NEW_REPORT_JSON
```

固定输入为`artifacts/captures/live-eight-58000-130125640`和`artifacts/research/live-nr-sr-eight`。先用既有`audited_frames`核原采样provenance；EXE检查全8帧文件后创建GPU设备。独立创建同适配器D3D11 FL11.0/D3D12，以同一份hdr_proxy.hlsl对照，非中性使用既有NR输出；不链接NGX loader或加载OptiScaler。

输出40份正常raw（对应48次阶段对照）＋非法模型控制raw与checks。HdrProxy12的prepare/restore使用分开的描述符槽，可在同一list中串接模型；对象/输入内容必须由调用方保留并等待上一帧退休再复用，不自带游戏状态、调度或错误停用策略。模型同链尚待验证。详见[颜色验证](../../docs/AC7-D3D12-Color-Validation.md)。

## 公开独立NR与D3D12整链

```text
optiscaler-interface OPTISCALER_DLL MODEL_LIB INPUT_NR_FRAME NEW_OUTPUT --fl11 --native-nr
color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT OPTISCALER_DLL MODEL_LIB
color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT OPTISCALER_DLL MODEL_LIB --sr-reset-control
python tools/optiscaler/check_model_chain.py PROJECT_ROOT SOLO_NR_EVIDENCE CHAIN_EVIDENCE NEW_REPORT_JSON
```

MODEL_LIB必须同时含官方310.9.16.0的nvngx_dlssnr.dll与nvngx_dlss.dll，核摘要再复制；不使用修改模型。独立NR输入含NR代理/depth/motion和同帧exposure/frame-time.bin。整链从原捕获HDR开始，绝不把已NR的SR颜色fixture作为场景。

整链模式复用color-interface的I/O，动态调用公开导出并共用原evaluation_parameters。每组创建两特性、同queue/list串prepare→NR→restore→SR，完成后才读回全部阶段；NR历史保持，选项仅将SR改为逐帧Reset。INI内建NR/FG仍关闭，目标进程改为color-interface.exe。无NGX loader链接。

检查器当前固定证据目录名history-v1与sr-reset-v1，各保存process/backend/exit；独立NR目录含run-v1。此模式尚不含实时D11输入输出桥、deadline或在途停止，不可复制测试的等待/ExitProcess进游戏。见[公开整链报告](../../docs/AC7-OptiScaler-GPU-Chain-Validation.md)。

## 原始HDR单共享桥与发布故障控制

```text
color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT OPTISCALER_DLL MODEL_LIB --shared-chain
color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT OPTISCALER_DLL MODEL_LIB --shared-faults
python tools/optiscaler/check_shared_chain.py PROJECT_ROOT EVIDENCE_ROOT NEW_REPORT_JSON
```

输入由文件初始化D11原始5120x1440 R11/规范depth/MV/exposure，一套NT共享桥接12颜色/NR/SR并回D11私有纹理。共享模式使用ChainPublication确认双API退休和绝对deadline；仅published可消费，禁止将game scene直接作为destination。故障模式依赖保留的nonblocking-chain-v1/run-v2/frame3/4/6参考，缺失则在模型初始化前拒绝。

最终检查器固定normal-v2/faults-v2，累计计数包含normal-v1/faults-v1；每次保存独立raw、process/backend/exit。原FP16传输单测重新编译/执行，不产生模型求值。测试预算200ms/5s不是生产帧预算，整list提交后SR不会因中途过期自动取消。

当前仍以规范fixture装入guide，未本轮在线串GuideNormalizer/游戏回调；无FG/超分/性能/真实设备失效/热卸载资格。见[共享整链报告](../../docs/AC7-OptiScaler-Shared-Chain-Validation.md)。

## 同通路直接NGX与有限测量

配置`NGX_LOADER_LIBRARY`指向已核验的官方nvsdk_ngx_d.lib时，会额外构建`chain-direct-interface`。与color-interface共用全部source，唯一入口绑定和Shutdown由编译标志切换：直接版官方loader/Shutdown1，Opti版动态公开导出/无参Shutdown12。

```text
cmake -S tools/optiscaler -B build/optiscaler-interface -DNGX_LOADER_LIBRARY=OFFICIAL_LOADER_LIBRARY
cmake --build build/optiscaler-interface --config Release
chain-direct-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT DIRECT_UNUSED_ARGUMENT MODEL_LIB --shared-chain
color-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT OPTISCALER_DLL MODEL_LIB --measure-shared
chain-direct-interface REAL_CAPTURE DIRECT_EIGHT_BASELINE NEW_OUTPUT DIRECT_UNUSED_ARGUMENT MODEL_LIB --measure-shared
python tools/optiscaler/check_route_comparison.py PROJECT_ROOT EVIDENCE_ROOT NEW_REPORT_JSON
```

直接版第四路径参数占位不加载DLL；host不得放OptiScaler。公开loader已静态链接。测量模式固定40调用：真实8帧重复5轮、每轮双Reset；首8预热、后32有效。无循环阶段图像读回/文件/cout，只有40字节timestamp readback；每进程最后读图对基线。记录CPUparams/Evaluate/record/submit与D12四段GPU，completionPollWall不能算FPS，GPU不含D11复制。

**配置勘误**：OverlayMenu=false不是禁用overlay，而是旧菜单路径；30帧后无RT标志/隐藏菜单分支可能留COPY_SOURCE。本次测量独立host用默认OverlayMenu=true且无swapchain/Present绕开旧路径，不修改DLL源码、不认为游戏菜单已修复。旧8帧证据继续保留，勿照搬旧ini声称已关全部overlay。logger全关sink可能退回控制台默认logger；测量使用LogToFile=true＋LogLevel=6/off建立空sink，最终文件0字节。这里只改变测试目录配置。

按Opti/Direct/Direct/Opti串行执行，不并发GPU。两个失败测量40与31模型调用保留并排除，检查器固定opti-a-v3/direct-a-v3/direct-b-v3/opti-b-v3，原始分布/失败计数可复核。详见[路线对照](../../docs/AC7-Route-Comparison-Validation.md)。

详见 [验证报告](../../docs/AC7-OptiScaler-Interface-Validation.md)。
