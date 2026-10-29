# Overglaze 对 AC7 原生适配的参考价值

## 判断

**值得作为 NR 输入规范化、颜色交接和生命周期管理的源码参考；当前不作为 AC7 的现成后端或完整宿主。** 它在已有 DLSS SR/RR 的 DX12 游戏中，把NR插到上采样器输出之后，依靠已有DLSS接口拿到原生输入。AC7目前确认是D3D11，尚未接入DLSS；目标是Pre-SR NR→SR→FG及超宽屏周边压缩。两者的缺口不能用换DLL或填一份游戏配置解决。

仓库：[HANsoA-KevinO/overglaze](https://github.com/HANsoA-KevinO/overglaze)。本次固定提交`8bdf21e51abe0c19448a3b7340665b980cb47cc5`（README标记0.2.0-preview.11），只读克隆到项目外`../research/overglaze/`。没有安装、注入游戏、加载NR模型或引入项目依赖。后端选型继续开放。

## 对照目标

| 问题 | Overglaze源码已有 | 对AC7的意义/缺口 |
| --- | --- | --- |
| 图形API | NR runtime和游戏hook为D3D12 | 不能直接消费AC7的ID3D11Texture2D；未见D3D11/D3D12资源桥。桌面viewer/console使用D3D11，不等于游戏NR支持D3D11 |
| 插入位置 | NGX/Streamline SR/RR返回后，同一DIRECT command list内运行NR，再回写SR/RR输出 | 属于Post-SR NR。改为Pre-SR须另接入口、按渲染尺寸重建NR、核对后续SR契约，不是切一个标志 |
| 原生输入 | 从DLSS公开参数/标签取得颜色、深度、MV和常量 | 不能自动识别AC7渲染pass，也不生成其缺失输入 |
| SR/FG | 使用游戏原有SR/RR；其他NGX feature透传 | 不向AC7补充SR/FG；与FG联用尚非完整验证结论 |
| 周边优化 | 矩形区域裁剪与线性尺寸重采样 | 没有非线性周边压缩/解包、运动矢量坐标变换或多区历史实现 |
| 颜色 | 浮点工作色→压缩/sRGB→NR→比值回填 | 对AC7的R11G11B10_FLOAT场景候选有参考价值，需验证工作色域与曝光，不能直接默认 |
| 深度 | R32G8X24_TYPELESS depth plane提取到R32_FLOAT | 与AC7当前Texture2D199格式直接相关；D3D11视图/资源交接仍需实现 |
| 速度 | 已有float MV的裁剪、显示网格到guide网格采样和尺度换算 | 不接受AC7当前R16G16_UNORM原样输入，不解码、不重建相机/物体速度；不能解决当前全零样本 |

## 优先参考的具体实现

### 1. 先规范化输入，不能按底层存储相似就直接传模型

[depth_codec.cpp:25](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/src/depth_codec.cpp#L25)通过`R32_FLOAT_X8X24_TYPELESS`、depth plane0、mip0读取硬件深度，并写入独立R32_FLOAT。它保留深度数值，不把模板字节/填充位送入模型，也不在这一模式里做深度线性化。

[nr_live_core.hpp:209](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/src/nr_live_core.hpp#L209)注释记录：作者曾将双平面深度未经转换送入NR，首个Evaluate触发显示驱动故障；这是作者记录，不是本次复现。当前实现以显式转换避免该路径。

AC7已读回的Texture2D199正是R32G8X24_TYPELESS，因此这个处理值得在输入边界采用。D3D11同样需要合法的深度SRV和独立模型输入纹理；若后端是D3D12，还要明确资源共享、格式、同步与状态。不能推导为“直接共享整张深度纹理给NR即可”。

### 2. NR颜色域与回填方案

[lab_prepost_ratio_shader.hpp:32](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/include/lab_prepost_ratio_shader.hpp#L32)中的当前颜色方案：

1. 对工作色X施加曝光E和pre矩阵，得到H。
2. 用保持通道排序的压缩算子映射到S，再sRGB编码送入NR。
3. NR输出解码为N，以`clamp((N+1e-6)/(S+1e-6),0.01,10)`求每通道修改比值。
4. 将比值施加到保存的H，再post矩阵和除曝光，回填工作色；保留原alpha。

这是把NR修改传回HDR工作域的策略，**不是游戏原有tonemap的精确逆变换**。输入输出相同的理想条件下比值为1，有助于验证旁路；实际RGBA16量化、epsilon、比值钳制和低亮度噪声都可能影响结果。无效/负值像素有旁路保护，不应删掉。

AC7当前有两个不同用途的颜色候选：早期R11G11B10_FLOAT场景210，以及色调映射/空间AA后的BGRA8场景208。Overglaze的这套HDR交接更贴近前者；不能把后者当linear HDR再套一次压缩和sRGB。它的live入口明确允许R11G11B10_FLOAT，并把模型内部输入转为RGBA16_FLOAT，见[nr_live_core.hpp:201](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/src/nr_live_core.hpp#L201)。

### 3. 分开处理颜色、深度、MV的有效尺寸

[motion_codec.cpp:15](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/src/motion_codec.cpp#L15)在目标guide像素中心选择源MV像素，输出R16G16_FLOAT。它不插值平均速度，不把颜色分配尺寸等同guide尺寸。

[nr_live_core.hpp:467](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/src/nr_live_core.hpp#L467)在速度从另一网格重采样后，按`guideExtent / motionRegionExtent`换算传给NR的scale；保留原符号。这个规则依赖其已知输入单位，不能脱离DLSS调用契约套到AC7。

裁剪有边界：motion shader按**左上角为原点**读取，NGX observer明确检查非零subrect base并拒绝，不支持任意居中偏移，见[ngx_observer.cpp:473](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/controller/src/ngx_observer.cpp#L473)。不能把README的letterbox说明扩展为任意view rect支持。更不能把这些矩形裁剪视为我们所需的非线性周边压缩；后者还要变换MV端点、guide坐标和历史。

### 4. 明确MV/jitter前提，拒绝假数据

[lab_sl_guide_contract.hpp:24](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/provider/include/lab_sl_guide_contract.hpp#L24)要求`cameraMotionIncluded=true`、`motionVectorsJittered=false`、2D MV和有效常量。已去jitter的MV不再减一次jitter。NGX路径也拒绝jittered MV，见[lab_ngx_frame.hpp:117](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/controller/include/lab_ngx_frame.hpp#L117)。

这值得借鉴为AC7适配契约。AC7 shader中的`value*4.008016-2.003978`解码和深度/矩阵相机回退必须由我们确认/实现；Overglaze现有motion codec只读取float xy，不能替代它。目前AC7速度候选全零，不能因为此项目能读取MV就视为问题已解决。

### 5. 模型会话、GPU完成和历史重置

- [lab_nr_session.hpp:74](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/include/lab_nr_session.hpp#L74)：feature18、D3D12接口、自定义NGX参数ABI；Create/Evaluate返回前不假设GPU已完成，失败也可能已记录工作。
- [lab_completion_timeline.hpp:94](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/include/lab_completion_timeline.hpp#L94)：区分reserved/recorded/submitted/complete/discarded；只在已提交的同队列fence确认后回收。COM AddRef只保护对象寿命，不保护资源内容不被游戏覆写。
- [nr_live_core.hpp:525](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/src/nr_live_core.hpp#L525)：断帧触发历史处理；尺寸/网络变化走重建；录制未提交即被Reset的工作不能记作完成。
- [ngx_observer.cpp:488](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/controller/src/ngx_observer.cpp#L488)：先调用原NGX，再插NR并恢复游戏继续录制所需状态。这是其Post-SR位置的直接证据。

这些原则对未来D3D11/D3D12桥有用，但D3D12的root signature/descriptor heap状态跟踪不能直接作为D3D11状态恢复代码使用；queue fence也不能自动证明外部内部队列已全部完成。当前不复制整套host/安装器/控制面板。

### 6. 曝光来源与可信度

[lab_game_exposure.hpp:105](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/source/native/runtime/include/lab_game_exposure.hpp#L105)优先使用有效游戏曝光，以`E_game * scale / preExposure`转换，GPU测光作回退，并用滞回防止曝光来源反复切换。无效曝光是有名字的回退原因，而非伪造为1。

这是可参考的来源管理与诊断设计，不是AC7曝光契约。AC7的Texture2D204虽然是1×1 R32G32_FLOAT，R通道的实际含义、曝光应用位置和pre-exposure仍未知；匹配格式不等于值能直接套用。Overglaze对曝光合理区间、初始+5 stops和one-frame-lag的选择也需按我们的图像与时序独立验证。

## 本次实际验证

没有构建/安装完整Overglaze，也没有准备NGX/Streamline SDK或NR模型。只从原始源码独立构建两个不需要模型的测试，源码未修改：

| 上游测试 | 执行方式 | 本机结果 |
| --- | --- | --- |
| runtime/tests/depth_codec_tests.cpp + depth_codec.cpp | D3D12 WARP软件设备，stencil与depth分离、mip0/错误格式检查 | 退出0，PASS 9 checks |
| runtime/tests/region_crop_tests.cpp + depth/motion/color_handoff | D3D12 WARP软件设备，逐像素裁剪、MV重采样和区域回写 | 退出0，PASS 174 checks |

证据在[checks.json](../artifacts/research/overglaze/checks.json)，编译和输出日志位于同目录，临时构建工程在忽略的`build/overglaze-reference-*`。这183项检查只证明这些独立模块在WARP上的测试通过，**不是RTX5080 NR求值、AC7接入、动态画质或GPU性能验证**。

README报告的RTX5090约5K额外9–12ms是作者条件下的观测，不外推为本机5080或Pre-SR压缩链的开销。

## 许可与采用顺序

原创代码为MIT；复用时保留版权和许可证。`runtime/host/binding_state.hpp`改编自ReShade，使用BSD-3-Clause，须保留其独立声明。Streamline公开头、NGX SDK及NR模型各有边界：仓库MIT不授予NVIDIA模型/SDK的分发许可；不能把自定义NR调用视为官方稳定API。见[LICENSE](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/LICENSE)和[第三方声明](https://github.com/HANsoA-KevinO/overglaze/blob/8bdf21e51abe0c19448a3b7340665b980cb47cc5/THIRD_PARTY_NOTICES.md)。

建议顺序：

1. 下一份AC7正常飞行捕获仍优先确认速度、相机常量、颜色/曝光和稳定的scene/UI边界。
2. 需要写输入规范化时，优先参考单平面深度、颜色交接和有效区域测试；按最终后端API实现必要部分。
3. 选定NR宿主/调用后，再比较其Session/生命周期方案与已有候选，避免为复用此项目而先移植整个DX12 host。
4. Pre-SR周边压缩、DLSS SR和FG仍需分别从现有候选/AC7证据解决。本次调查不改变目标顺序或后端未选定状态。
