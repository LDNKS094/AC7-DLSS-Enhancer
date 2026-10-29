# NR / DLSS SR / FG 图形API与正式运行时

> 最新[扭曲前配对NR→SR链](AC7-Prewarp-NR-SR-Validation.md)：使用新prewarp颜色实际NR/SR各一次成功，GPU恢复/源校验后465后写回，原warp/后处理/UI保留。NR/SR输入读回逐位、fence完成、debug0；中性15raw与SR恒等18raw逐位，错旧结果拒绝。P1暂用，下一步连续帧/jitter/history，仍无实时/超分/FG验收。

> 最新[SR同尺寸离线回接](AC7-SR-Writeback-Validation.md)：已消费正式SR既有GPU输出，经原FXAA/后处理/UI进入最终帧；旁路12项逐位、错源拒绝、区外0变化、UI逐位。本轮没有新增模型求值，P1按用户决定暂用、预曝光调查延后。未实时/时域或高输出验收，下面创建/诊断“未回接”是此前阶段边界。

## 当前结论

DLSS5已正式发布，且本项目已从**Remix官方依赖源**取得可核验的NR运行时310.9.16.0及配套参数头。无需用户另找早期或来源不明的DLL。

通过官方NGX loader、自己的CUSTOM Project ID及本机RTX5080，NR的D3D11/D3D12需求查询均通过；进一步初始化后，**D3D11报告特性不可用/FeatureNotSupported，D3D12初始化、创建和真实AC7单帧SDR Evaluate/GPU完成/读回通过**。后续CPU HDR准备/恢复参考、局部GPU颜色pass和同一GPU代理实际NR非中性回写均已通过阶段验证，见 [配对回接](AC7-NR-GPU-Writeback-Validation.md)。现场代理逐位核验后GPU恢复/回写，原后处理/曝光当前算术/三张UI输入已检查；仍是离线文件衔接，未验收实时桥、动态曝光或时域。当前见 [HDR参考验证](AC7-HDR-NR-Roundtrip.md)，首次求值和API阶段见 [单帧求值](AC7-NR-Single-Frame-Evaluation.md)、[初始化/创建记录](AC7-NR-Runtime-Preflight.md)。

| 功能 | 当前证据 | 未完成项 |
| --- | --- | --- |
| 深度、速度规范化与同尺寸旁路 | D3D11硬件与AC7独立重放验证通过 | 实时hook、多帧和最终全幅适配 |
| DLSS SR/DLAA | 正式310.9.16.0，本机能力/创建通过；后续原始及NR后HDR在显式独立域假设下的Reset求值/GPU读回成功 | 游戏真实pre-exposure/时域标定、画质与高输出回接未验收；见[Reset诊断](AC7-SR-Reset-Diagnostic.md) |
| NR310.9.16.0 | NVIDIA签名有效；正式参数定义；本机D3D11特性拒绝，D3D12真实AC7单帧SDR求值/GPU完成/读回成功 | 时域、完整颜色/深度/速度标定、Pre-SR及实时接入 |
| DLSS FG | 官方公开D3D接口为D3D12，另有Vulkan，无对应D3D11 helper | 运行查询、特性执行、相机/UI/呈现节奏 |

针对这一确切版本，NR的D3D12单帧SDR执行已通过，纯D3D11 NR分支停止。SR本身仍可用D3D11；FG的公开执行/呈现也需要D3D12或Vulkan。因此，D3D11游戏输入与非D3D11处理/呈现之间的衔接现在有了实测依据，但具体桥、完整宿主及进程架构仍未选定，不需要把AC7渲染器移植成D3D12。不能把需求查询mask0视为所有API都能执行NR。

## 正式发布与SDK获取是不同问题

- [NVIDIA正式发布公告](https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/)说明DLSS5已随NBA2K27发布。
- [10月更新公告](https://www.nvidia.com/en-us/geforce/news/dlss-5-faster-ai-model-hogwarts-legacy-rtx-spark-laptops/)说明10月16日新模型、10月23日RTX Remix接入。不能把NR整体继续称为偷跑或未发布。
- 本轮直接检查[NVIDIA开发页](https://developer.nvidia.com/rtx/dlss)：仍有NR相关Notify Me入口，UE公开下载名为DLSS4.5。该页面不等于所有正式开发组件的完整目录。
- 已下载[Streamline v2.14.16 x64正式包](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.16)，511个archive条目；有`kFeatureDLSS_NR=1004`声明，但没有NR专用header、plugin或guide。完整zip SHA256与GitHub release asset公开digest一致。**功能编号存在，不等于完整NR接入包已包含。**
- Remix公开依赖配置确实提供另一条取得运行时及NR头文件的途径，不能将Streamline包内缺失写成“官方SDK/运行时无法取得”。

## 已取得的官方NR包

来源链：

1. 官方Remix提交`890740c237e72cf9df97438605e9582a8f8820f9`的[packman-external.xml](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/890740c237e72cf9df97438605e9582a8f8820f9/packman-external.xml#L35)声明`rtx-remix-ngx_sdk_dlnr` version2。
2. 同提交[公开Packman remote配置](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/890740c237e72cf9df97438605e9582a8f8820f9/scripts-common/packman/config.packman.xml)给出NVIDIA使用的CloudFront下载源。
3. 只读检查官方Packman8.7.0实现，确定其依次尝试`.7z`/`.zip`；实际`.7z`返回200并完整下载367851723bytes。此前无后缀/`.zip`返回403是错误候选路径的结果，不作为访问权限结论，也没有绕过认证。
4. 只提取headers、LICENSE和Windows_x86_64运行时，不安装到游戏或系统。

运行时身份：

```text
文件：nvngx_dlssnr.dll
版本：310.9.16.0
大小：266424944 bytes
SHA256：2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f
Authenticode：Valid，NVIDIA Corporation
```

本地路径：`artifacts/research/official-nr/remix-sdk/lib/Windows_x86_64/rel/nvngx_dlssnr.dll`。

包内存在`nvsdk_ngx_defs_dlssnr.h`、通用NR helpers及**Vulkan NR helper**，没有D3D11/D3D12专用NR helper或完整独立D3D接入指南。定义正式把feature18映射为DLSSNR，包含color/depth/MV/output/control mask、subrect、reset、depthInverted、模型preset及强度设置等。标准NGX枚举仍写Reserved18并不否定专用NR头的正式含义。

LICENSE为NVIDIA RTX SDKs License，不是本项目开源代码的许可证；下载件保留在Git忽略的artifacts内，未复制进可提交源码或重新分发。当前来源和签名可以核实，但不能仅凭签名替代每个API/输入组合的执行验证。

[包和运行时身份记录](../artifacts/research/official-nr/artifact-identity.json)，[完整包文件清单](../artifacts/research/official-nr/remix-nr-package-inventory.txt)。

## 本机查询结果：必须区分loader与runtime同名入口

工具：[query_requirements.cpp](../tools/nr/query_requirements.cpp)（直接runtime导出）及[query_core_requirements.cpp](../tools/nr/query_core_requirements.cpp)（官方loader）。均为独立程序，不注入游戏，不改驱动/补丁，不冒用游戏ID/模块名，不执行NR工作。

官方头明确允许`GetFeatureRequirements`在Init前使用。使用自建CUSTOM Project ID，调用的feature为18，适配器为NVIDIA，日志和feature搜索路径都显式指向项目私有目录。

| 路径 | D3D11 | D3D12 | 可得结论 |
| --- | --- | --- | --- |
| 直接LoadLibrary运行时并调用同名导出 | `0xBAD00002 PlatformError`，输出哨兵未改变 | 同左 | 直接入口没有完成查询；不能解释为FeatureNotSupported，也不能区分两API支持性 |
| 官方NGX loader静态库→运行时 | `0x1 Success`，mask0 | `0x1 Success`，mask0 | 两API的初始化前requirements阶段均通过 |

成功路径额外输出MinHWArchitecture=`0x1B0`、MinOS=`10.0.0`；这些是该运行时query返回，不外推为完整产品系统要求。`GetModuleFileName`核对加载路径为上述官方NR文件。公共loader取自NVIDIA/DLSS固定提交`9981c635...`，库SHA256为`bc43df9e...e28789f99c`，完整值在身份记录。

本轮工具通过 [tools/nr/CMakeLists.txt](../tools/nr/CMakeLists.txt)独立构建，显式传入官方`NGX_INCLUDE_DIR`与`NGX_LOADER_LIBRARY`；不把SDK头/库提交进项目源码。已用该可保留的构建入口重建两个probe成功。运行官方loader版时参数为`<NR目录> <日志目录> <自建GUID>`；直接runtime版仅为已记录的对照，不是推荐集成入口。

[直接入口记录](../artifacts/research/official-nr/requirements-query.json)、[官方loader记录](../artifacts/research/official-nr/core-requirements-query.json)。这些早期probe没有Init/Create/Evaluate；随后独立preflight已执行初始化和D3D12创建，见 [后续验证](AC7-NR-Runtime-Preflight.md)。需求查询进程退出0仅表示查询完成。后续D3D11实际FeatureNotSupported，已证明mask0不能直接标成“D3D11 NR已跑通”。

## SR、FG及候选实现证据

SR：[官方D3D helper](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_helpers_d3d.h)明确提供`NGX_D3D11_CREATE_DLSS`和`NGX_D3D11_EVALUATE_DLSS`，不是仅靠通用CreateFeature推断。

FG：[官方D3D FG helper](https://github.com/NVIDIA/DLSS/blob/9981c6350dc58c0ce21f024fd65e31e851c31689/include/nvsdk_ngx_helpers_dlssg_d3d.h)使用ID3D12Resource与D3D12命令列表；[Streamline指南](https://github.com/NVIDIA-RTX/Streamline/blob/625db07d4bca5abc67d781c42af7e7fb0761f1b6/docs/ProgrammingGuideDLSS_G.md)列出D3D12/Vulkan支持。SL路径要求sl.reflex、正确frame index及Present期间有效的输入资源；FG执行和生成帧pacing/呈现不是同一层。当前AC7的D3D11 DISCARD链不能仅靠几张共享纹理就视为接入完成。尚未查询本机具体FG版本/HWS要求，没有修改系统设置。

NR：[官方Remix wrapper](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/890740c237e72cf9df97438605e9582a8f8820f9/src/dxvk/rtx_render/rtx_ngx_wrapper.cpp)和[NR pass](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/890740c237e72cf9df97438605e9582a8f8820f9/src/dxvk/rtx_render/rtx_dlss_neural_rendering.cpp)提供Vulkan路径。OptiScaler/Cheeky/Overglaze本地固定版本使用D3D12 NR或D3D12 transport；这是各实现的选择，不是新版NR所有API均受限的证明。社区旧版D3D11 FeatureNotSupported报告保留在研究附件，不再用作当前310.9.16.0的结论。

## 现在的执行门槛

NR的D3D12独立单帧SDR Evaluate/GPU完成/读回已实测。11,059,200个输出RGB通道全部有限、无NaN哨兵残留，输出非恒等；三张输入读回逐位一致，D3D12 debug error为0。D3D11 NR在FeatureNotSupported停止；D3D12通用scratch查询PlatformError仍保留，但未阻止此前创建和本次求值。

- 官方路径/公开NR专用接口与本机直接HDR对照已确认：当前310.9.16.0需要SDR兼容输入，零修改强度输出也严格clamp(input,0,1)。原生HDR直入分支关闭，不能以FP16或API Success宣称支持HDR。
- 单帧Reset=1不验证运动时序、jitter/history或透明动态覆盖，也不验证最终5120×1440补丁场景。不要复制同一帧伪装历史。
- 用户限定不深入改工具链；NR非中性离线回接及SR正式创建已通过，后续[显式独立域Reset诊断](AC7-SR-Reset-Diagnostic.md)也完成两组HDR求值/读回。P=1是诊断假设，不是游戏校准；真实pre-exposure/时域仍未知，输出高光变化未验收，不回写SR结果。候选顺序不变，下一步解释重建差异并定位真实语义；若需patch模型/NGX、改SDK或重写UE4/调色链则停止。
- NR使用正式参数定义及公开loader合同；未混用snippet ABI、修改DLL或调用模块名，也未因包内D3D helper缺失自动导入完整框架。

没有修改原始捕获或游戏，没有Git提交。**初始化会触发官方NGX维护ProgramData模型缓存/ACL及启动官方更新器**，不能声称完全只读；未手工改系统/OTA设置，详见 [副作用记录](AC7-NR-Runtime-Preflight.md#ngx本身的副作用)。
