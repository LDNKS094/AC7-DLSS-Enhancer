# 有界两帧资源采样：实现与当前阻塞

## 结果

**最新实机传输已成功：** 用户77324目录得到393→394两帧，real时间/六矩阵链支持相邻渲染；GameTime/相机/depth/MV保持，动态时域尚未验收。见[首次真实两帧验证](AC7-First-Live-Pair-Validation.md)。无需再换v3 addon，下一次只换独立延迟5秒触发命令并及时回到飞行，核验模拟推进。下方“待实机”是各阶段历史，首次尺寸失败具体触发分支仍无原值，不能从成功样本倒推旧失败原因。

**第二次实机失败与v3：** 用户目录`AC7-two-frame-102664-123890484`在scene之前因`Duplicate motion consumer in Present interval`拒绝，无raw/scene metadata。这次不是v1尺寸报错。日志00:33:25.311启动、00:33:29.601已arm、00:33:30.676失败；现场仍有创建于00:34:53.309本地时间的请求文件。arm早于该新请求约84秒，支持前次遗留请求在启动阶段被消费的判断；不能声称这次已经采到用户后来操作时的正常飞行，也不能据此判断v2尺寸修正失败。证据保存在[第二次实机记录](../artifacts/research/temporal-preflight/second-live-failure/live-observations.json)及同目录ReShade.log/failure。

v3修正两个离线假设，而非取消一致性检查：

- [请求门禁](../src/d3d11/capture_request.h)用公开GetProcessTimes与文件last-write时间判断请求是否早于当前进程；旧请求归档`.stale-PID-TICK`，不arm。新请求仍一次/进程，不重复采集。
- [引导候选配对](../src/d3d11/guide_candidates.h)最多保存16个不同depth/velocity/View/viewport组合；完全相同绑定去重，多个不同view不立即判错。到实际prewarp时，以DSV resource、PS View buffer、精确viewport选择唯一候选；无匹配的其他视图不借用guide，同一匹配有两个不同velocity则拒绝。匹配后的多scene/曝光重复、缺帧仍严格拒绝，不能选择任意first/last consumer或跨Present补齐。
- 最多64个已识别draw事件写`candidates.jsonl`，保留role顺序、Present区间计数、viewport和候选数；v2真实texture描述/rect文件和256MiB预算保留。角色0=guides、1=prewarp、2=tonemap。未识别完整多视图序列不预先验收。

硬件自检覆盖相同候选去重、不同viewport准确选择、无匹配不借用、不同velocity歧义拒绝、16项上限、缺请求/旧请求归档/新请求接受，原GPU检查仍debug0。v3派生AC7重放fresh请求成功，两帧/6候选事件、原15raw/8纹理快照逐位；独立stale重放不arm并留归档。第一次测试尝试用ReShade日志通知后发fresh请求，日志缓冲导致通知过晚，进程结束未采入；保留此测试，不把它算传输成功。调整为测试进程创建150ms后写fresh请求才通过，不在真实游戏使用固定延时。见[硬件检查](../artifacts/research/temporal-preflight/second-live-failure/v3-gpu-test.log)、[重放检查](../artifacts/research/temporal-preflight/second-live-failure/v3-replay-checks.json)。所有重放仍995/995、temporalQualified=false。

当前包`artifacts/packages/ac7-two-frame-diagnostic-v3/`，未改游戏文件。下一次退出游戏后仅备份/替换addon，并将现存`AC7-Capture.request`移到备份处；进入正常飞行后才发一次新请求。真实游戏多视图配对、最初尺寸问题与相邻性仍待实际输出确认，不保证v3已解决所有实机问题。下方v2为前阶段记录。

**首次真实游戏采样已触发但未完成：** 用户目录`F:/SteamLibrary/steamapps/common/ACE COMBAT 7/AC7-two-frame-118836-122153859`仅有`INCOMPLETE`和`failure.txt`，错误为`Unsupported scene dimensions or View rect`。ReShade日志确认插件加载、04:32.978请求启动、04:33.864触发此拒绝；没有纹理文件，未取得真实两帧。显示swapchain/config为5120×1440、ResolutionQuality100，但v1未记录内部纹理/rect，不能断言是padding、超采样或错误绑定。原日志和失败文件已保存至[首次实机失败](../artifacts/research/temporal-preflight/first-live-failure/)。

**v2已准备，游戏内仍是v1，未自动替换。** 修正把离线5120×1440误当分配硬上限的假设：现在按精确两帧逻辑数据预算`2*(width*height*16+4096+8)`预检，仍最多256MiB，超预算在staging分配前拒绝；D3D11尺寸、rect完整包含、格式/设备等检查保留。插件viewport整数校验也按D3D11合法尺寸上限，不再另藏5120限制。每次scene在拒绝前保存`frameN-scene-metadata.json`（实际color/depth/MV描述、rect、预算），错误说明实际尺寸和拒绝类别。此修正解决不合理限制和诊断缺口，**真实失败是否同因仍待v2数据，不宣称已修好实机采集**。

v2硬件回归包括旧两组10资源精确/debug0/拒绝检查，额外验证32×1456分配+32×1440rect可以采入、4096×2049两帧超预算在staging前拒绝并保存尺寸。派生AC7重放exit0、原15raw逐位、8纹理快照逐位、2metadata正确，仍995/995非真实序列。证据：[硬件检查](../artifacts/research/temporal-preflight/first-live-failure/v2-gpu-test.log)、[重放检查](../artifacts/research/temporal-preflight/first-live-failure/v2-replay-checks.json)。新包`artifacts/packages/ac7-two-frame-diagnostic-v2/`，退出游戏后仅备份/替换addon，不改显示设置、ReShade运行时或原失败目录。

下文为v1离线资格与实现历史；“尚未实机”指该阶段，最新实机结果以上段为准。

已实现独立D3D11两帧采样核心，并在RTX5080硬件上验证两组不同合成输入的异步快照、停止边界和拒绝路径。可选ReShade诊断插件已构建，**已通过三角probe加载/缺签名拒绝及派生AC7捕获重放的两帧传输测试**，原15检查点逐位不变。仍未部署到游戏，也未取得真实连续帧。本轮没有启动真实AC7或Nsight，没有模型求值。

## Nsight公开入口核对

一个只读scout（workflow `0728c929-f7ec-4968-b1e5-27f1c8a218cf`，child `692380c1-7366-4558-ab64-8c7fceecb207`）检查2025.5公开文档及本机SDK，报告保存在[有界控制核对](../artifacts/research/temporal-preflight/nsight-bounded-control-audit.md)。主会话独立读取`NGFX_Injection.h`209–239行，确认公开接口是`InjectToProcess(installation, activity)`和无参`ExecuteActivityCommand()`；后者只触发活动主命令，没有开始/停止/帧数参数。项目JSON入口本身不提供已文档化的D3D11帧数设置。

在已查公开范围内，未找到D3D11 Frame Debugger有界采集或从巨大采集中截取若干帧导出C++的明确接口。这个结论不否认实际1363帧事件采入，也不证明私有功能不存在；不探查私有ABI。D3D12/Vulkan Graphics Capture参数继续排除，不套给D3D11。

## 核心采样行为

代码：[two_frame_sampler.h](../src/d3d11/two_frame_sampler.h)、[实现](../src/d3d11/two_frame_sampler.cpp)、[硬件自检](../tests/d3d11/validate_two_frame_sampler.cpp)。

- 首次Present后的两个区间分别收集prewarp原始HDR、depth/stencil、velocity、4096字节View，以及同帧原tonemap使用的1×1曝光。
- 两个区间收齐立即停止采入。GPU复制进入独立staging，最后一个区间提交event query；之后Present用DONOTFLUSH轮询，完成后才用DO_NOT_WAIT Map保存。不逐资源同步等待、不修改游戏渲染绑定/矩阵/着色器。
- 固定上限2帧，logical资源数据最多256MiB（5120×1440每帧约112.5MiB）；驱动实际分配可能包含行对齐等额外开销。只支持单层/单mip/单采样、最多5120×1440、精确已知格式与View尺寸。
- 以新GUID和实例token在device的公开private-data存储中验证底层身份，析构清除；兼容ReShade代理设备，另一底层device没有此标记则拒绝。未靠放宽校验、适配器LUID相同或私有unwrap来通过。
- 缺少场景/曝光、重复匹配、尺寸/rect变化、跨device/context或deferred context均终止，不跳帧补齐。最多120次Present回调/回调时发现超过10秒则失败；**没有回调时不能保证墙钟定时终止**，但不会继续采入/分配。写盘仍有同步开销，发生在两帧GPU快照入队之后，不能当性能验证。
- `INCOMPLETE`保留到所有文件和manifest写完；失败写`failure.txt`，输出目录已存在则拒绝。
- manifest明确区分`transportComplete`和`temporalQualified:false`。候选计数+1、前后时间串接只作诊断，独立矩阵/资源语义核验前不验收时域。PreExposure=1仍是暂用假设，jitter保留实际View字节。

硬件自检：[sampler-gpu-v3.log](../artifacts/research/temporal-preflight/sampler-gpu-v3.log)。两组不同合成内容在同一context中依次更新、复制：10份资源的有效分量准确保留，viewport/View绑定保持，第三帧不采入，已有目录/缺帧/重复/越界rect/deferred context/另一底层device拒绝，D3D11 debug error=0。首次检查误把R32G8X24的X24填充当有效数据而失败；修正为深度32+stencil8，保留[v1失败](../artifacts/research/temporal-preflight/sampler-gpu-v1.log)，没有更改实际深度数据来适应测试。

## 可选插件与来源

[插件源码](../tools/capture-addon/capture_addon.cpp)通过ReShade公开API20的init/bind pipeline、draw/indexed和Present回调观察原始流程，所有绘制返回false，原draw继续。仅请求文件存在时启动一次，不自动重复采样；不带Luma/模型、不调用NGX、不改游戏配置，不选择最终宿主。

使用官方ReShade `v6.8.0`公开头，commit `18deaa52de0c425a78b329e9cb3c497281cd00ec`；9个文件和MIT/BSD许可来自固定GitHub源码，[URL/哈希记录](../artifacts/research/reshade-sdk-6.8/provenance.json)。头文件置ignored artifacts，未复制Luma实现进本项目。现有Luma子模块同版本头去除CRLF后与官方文件相同；它仍不作为构建依赖。测试运行时为先前取得的官方6.8 Addon载荷，仅复制到ignored build测试目录。

三个实际像素shader完整DXBC SHA256由原capture对应blob提取，且在该capture各唯一出现一次：

| 角色 | 字节 | SHA256 |
| --- | ---: | --- |
| 辅助motion consumer，depth t0/MV t3/View b1 | 4240 | `85ec4635ea1fee2851d5979da08a1013609ead56312747cc4f54ba2e80620af0` |
| 原warp，绘制前取HDR t0、实际DSV/View | 628 | `af1247e3a473861934047c3cabe849dae6577dc61189bc56ba225f3351294ec0` |
| 原tonemap，VS t0曝光 | 3220 | `4a12c78e0f169b42270edae1fea1f1dcd4a3a618f278b7e8de230a1577f6dbf0` |

来源[签名证据](../artifacts/research/temporal-preflight/live-signatures.json)。这些是当前样本的诊断签名，变体不匹配会失败，不用Draw465或资源UID充当游戏hook。实际游戏资源对应和View生命周期尚未实测。

构建（项目根目录，VS2022 x64；不复制文件到游戏）：

```powershell
& 'D:/Application/Qt/Tools/CMake_64/bin/cmake.exe' -S . -B build/two-frame-sampler -G 'Visual Studio 17 2022' -A x64 -DAC7_RESHADE_INCLUDE_DIR="$PWD/artifacts/research/reshade-sdk-6.8/include"
& 'D:/Application/Qt/Tools/CMake_64/bin/cmake.exe' --build build/two-frame-sampler --config Release --target validate-two-frame-sampler ac7-capture-addon
& ./build/two-frame-sampler/Release/validate-two-frame-sampler.exe ./artifacts/research/temporal-preflight/NEW_TEST_DIRECTORY
```

插件产物`build/two-frame-sampler/Release/ac7-capture-addon.addon64`已通过下面的离线资格检查；仍非已在真实游戏验证的部署包，未复制到游戏。

## 已修复的运行问题与对照

全部测试在独立build目录执行，未操作真实游戏目录或原捕获源码。早期带插件的probe/重放均exit253，只有register/unregister及相同7种回调全部no-op的控制插件均跑完120帧。逐回调入口日志在早期退出前均未出现。将DllMain里32768个wchar（64KiB）的栈缓冲改成堆字符串后，probe正常跑完120帧并按预期拒绝无AC7 shader的采样请求；这支持大栈帧在宿主加载路径引发退出的定位，未采集Windows异常dump，不把253直接定为已确认异常码，也不关联用户Next Frame崩溃。

之后派生重放因device指针不同被严格拒绝：所有格式/尺寸正确，但ReShade代理与原设备的接口指针不同，IUnknown指针比较也没有消除这一层差别。采用上面的公开device private-data标记校验后通过；没有关闭设备一致性验证。保留v1–v4失败日志和v5成功证据。

| 最终测试 | 结果 |
| --- | --- |
| 同probe+同ReShade、无插件 | 120帧/Present S_OK、exit0 |
| 同probe+只register/unregister控制插件 | 120帧/Present S_OK、exit0 |
| 同probe+相同7类no-op回调控制插件 | 120帧/Present S_OK、exit0 |
| 修正后的正式采样插件+probe | 120帧/Present S_OK、exit0；请求启动后缺少场景/曝光，安全终止，不跳过补帧 |
| 正式插件+派生AC7捕获重放（6次上限） | exit0，识别三个精确shader，保存2组×5资源后停止采入，完成manifest |
| 重放输出与原无ReShade基线 | 15项raw全部逐位，包括pre/postwarp、depth/stencil/MV、曝光、3UI及final |
| 采样资源与对应检查点 | 两份scene/depth/velocity/exposure共8个raw逐位；View另对原blob123核验 |
| 重放时域标记 | 两组候选计数995/995；step1=false、timeChain=false、temporalQualified=false，正确拒绝将重放当相邻 |

成功原始输出在ignored `build/sampler-addon-replay-v5/AC7-two-frame-102272-120684421/`；logical数据235,937,808字节，rect(1280,0,2560,1440)。独立结果见[重放校验](../artifacts/research/temporal-preflight/sampler-replay-v5-checks.json)。Probe缺签名拒绝见`build/sampler-addon-triangle-v4/`；历史失败、无插件/最小插件/no-op回调控制均保留在各build目录。`INCOMPLETE`成功时移除，失败时保留。

当前独立包位于`artifacts/packages/ac7-two-frame-diagnostic-v1/`，包含6个载荷/说明文件及SHA256 manifest；没有预置请求文件。只读游戏根目录清单未见`dxgi.dll`、`ReShade.ini`或addon，既有dinput8/dwmapi/ue4ss/AC7RumbleTrace.asi保持原样；此清单不是共存兼容性证明。触发与回退步骤见[包说明](../tools/capture-addon/README.md)，未执行部署。

下一步才是准备**无Nsight/无Luma/无模型的真实游戏两帧采样**：先核对当前游戏目录已有加载器、备份与回退范围，不覆盖现有dxgi/ReShade；只有用户启动游戏进入正常飞行后才发一次请求。需要接受同一独立诊断ReShade宿主的兼容性仍未知，不能从离线重放成功推出真实游戏不崩溃。暂不改任何游戏文件，不让用户重试Next Frame或F11。拿到真实序列后独立核验时间/矩阵/资源身份，再扩展8–16帧，不把固定2帧实现预先扩成通用录制框架。
