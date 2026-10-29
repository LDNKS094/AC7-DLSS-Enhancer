# D3D11 输入规范化与同尺寸旁路：首次代码验证

## 已完成的范围

本轮新增了首个项目内D3D11处理模块：[guide_normalizer.h](../src/d3d11/guide_normalizer.h)、[guide_normalizer.cpp](../src/d3d11/guide_normalizer.cpp)。它不识别游戏pass、不注入游戏、不调用NR/SR/FG；消费由适配器明确提供的深度、打包速度、有效区域及当前到前帧clip矩阵，生成裁剪后的独立深度和分析用速度。

已在硬件D3D11设备上通过独立测试，并实际插入2026-10-28 AC7捕获的独立重放副本。同尺寸场景复制回写后，最终GPU输出与原重放基线**逐位相同**。这是离线重放中的资源/状态与旁路验证，不是实时游戏适配或高分辨率回接完成。

输入来源和语义依据见 [正常飞行捕获](AC7-Nsight-Normal-Flight-20261028.md)。保持后端未选定，不提前切换为任何候选宿主。

## 模块契约

| 项目 | 当前明确支持/要求 |
| --- | --- |
| API | Windows D3D11 immediate context，需ID3D11Device1/ID3D11DeviceContext1；调用方串行使用 |
| 深度输入 | 单层、单mip、单采样R32G8X24_TYPELESS，允许SRV；通过R32_FLOAT_X8X24_TYPELESS视图只读深度部分 |
| 速度输入 | 单层、单mip、单采样R16G16_UNORM，允许SRV；R>0表示有效物体编码 |
| 有效区域 | 显式x/y/width/height，必须同时位于两个输入资源内；当前参数1280,0,2560,1440 |
| 相机变换 | 调用方提供4×4当前clip到前帧clip候选矩阵，禁止非有限值；不从shader编号/资源尺寸猜测 |
| 输出 | 有效区域尺寸的R32_FLOAT深度、R32G32_FLOAT速度、R32_UINT有效性标记 |
| 速度单位 | 观察到的游戏current-minus-previous NDC；不是已经确认的DLSS motion单位/方向 |
| 有效性 | 1=深度/矩阵相机回退，2=有效打包物体速度，3=无效深度/重投影，禁止把3当零速度送进模型 |
| 状态隔离 | 计算阶段使用独立ID3DDeviceContextState，运算后SwapDeviceContextState恢复原状态；没有手写不完整的几个Set调用冒充完整状态块 |

无效深度（NaN、Inf、范围外）或无效相机分母会输出mask3；数据中的0只是占位，不被声明为有效。原深度和速度只读，不覆盖游戏输入。

计算逻辑从已观察到的shader提取：物体速度解码`encoded*4.008016-2.003978`；无有效物体编码时，依据当前像素中心、view rect、硬件深度及显式矩阵重投影，再取current-minus-previous。输出裁掉左右黑边，而非把5120资源宽度误当2560场景的速度尺度。

本模块不负责jitter生成/去除。当前验证帧的相关jitter为0；非零jitter时必须先建立同一时间/坐标契约，不能仅因此代码可运行就直接接时域模型。

## 独立GPU测试

测试：[validate_guides.cpp](../tests/d3d11/validate_guides.cpp)。项目根[CMakeLists.txt](../CMakeLists.txt)只构建输入模块与验证程序，不依赖NGX/Streamline SDK、模型或外部框架。

实测输出：

```text
PASS D3D11 hardware normalization; state restored; depth bit-exact;
cropped=2560x1440 packed=185417 camera=3500983 invalid=0
maxNdcError=1.37137e-07 synthetic=passed
```

- 使用实际GPU shader计算，对照此前从游戏消费shader逻辑得到的CPU参考；最大NDC差1.37137e-7（按本view水平方向约0.000176像素），断言阈值2e-6。
- 所有3686400个有效区域像素的独立R32深度，与原双平面资源内float深度逐位一致，不包含stencil/填充位。
- 有效性计数与原数据一致：185417物体编码、3500983相机回退、无invalid。
- 测试在调用前把原深度和速度绑定为DSV/RTV，并预设viewport和CS常量缓冲；调用后逐项检查这些图形/计算绑定已恢复。
- 合成用例覆盖恒等相机、正负非零物体速度、相机平移方向、零编码sentinel、NaN深度、奇异变换和越界view拒绝。它们验证代码分支，不代替实际游戏相邻帧证据。
- 本轮未启用D3D debug runtime、未改驱动/系统设置，没有把同步读回的执行时间当性能结果。

日志：[normalizer-test.log](../artifacts/captures/nsight-flight-20261028/checkpoints/normalizer-test.log)。

## 插入AC7重放后的验证

独立源码副本在`build/insertion-20261028-source`；原始捕获未修改。[prepare-insertion-20261028.cjs](../tools/nsight/prepare-insertion-20261028.cjs)把验证限定到已核对的捕获名称、Draw528后和对象UID，拒绝将这个检查点当成通用游戏hook。

[replay_insertion.cpp](../tests/d3d11/replay_insertion.cpp)在真实重放context/资源上做：

1. 用当前深度/物体速度和显式参数运行GuideNormalizer。
2. 读回输出，核对与独立GPU结果一致。
3. 将本帧AA后效果场景Texture2D2062复制到同格式同尺寸私有纹理，再完整复制回原纹理。
4. 让原捕获继续执行UI滤波、场景/HUD合成及Present。

检查：[check_replay.cjs](../tests/d3d11/check_replay.cjs)对原始raw文件进行逐字节断言；[结果](../artifacts/captures/nsight-flight-20261028/insertion/checks.json)：

| 项目 | 结果 |
| --- | --- |
| 复制回写后的场景 | 与未插入版本逐位相同 |
| UI合成后 | 与未插入版本逐位相同 |
| 最终交换链图像 | 与未插入版本逐位相同（比只对8-bit截图容差验证更严格） |
| 重放内规范化速度 | 与独立GPU运行逐位相同 |
| 重放内规范化深度 | 与独立GPU运行逐位相同 |
| 有效性计数 | camera3500983、packed185417，全部有效 |

重放退出码0。测试使用staging/Map同步读回，这是验证工具的有意阻塞；未来实时调用不能照搬这些读回、每次重建shader和资源的测试写法。

## 复现入口

先用现有捕获分析流程生成checkpoints中的depth.raw、velocity.raw及CPU参考，再运行`analyze-motion-20261028.cjs`；它现在同时输出80字节`normalizer-parameters.bin`（view rect + 4×4矩阵）。

在项目根运行：

```powershell
cmake -S . -B build/native-validation -G "Visual Studio 17 2022" -A x64
cmake --build build/native-validation --config Release --parallel 4
.\build\native-validation\Release\validate-guides.exe artifacts/captures/nsight-flight-20261028/checkpoints artifacts/captures/nsight-flight-20261028/checkpoints/normalizer-parameters.bin
```

重放插入流程由prepare脚本创建全新副本后，用此前相同的CMake命令构建。运行副本EXE时cwd必须是原捕获目录，设`AC7_REPLAY_DUMP_DIR`为新结果目录，`AC7_GUIDE_PARAMETERS`为参数文件绝对路径；使用`-repeat 1 -automated -watchdog -log <file>`。最后执行`node tests/d3d11/check_replay.cjs <capture-artifacts> <insertion-results>`。所有构建、2GB捕获和GPU原始数据均保持Git忽略；未提交。

## 对后续实现的实际意义与边界

已经有可构建、可验证的D3D11输入处理代码，以及在真实捕获消费链中不改变结果的旁路。可据此定义AC7适配器的输入与状态隔离接口。

但还没有完成以下门槛：

- 相邻帧对物体MV时间方向、精确尺度、相机矩阵角色及jitter的独立确认；CPU/GPU一致只证明实现一致，不证明目标模型契约正确。
- 透明云、水、粒子、遮挡/显露及其他动态物体的速度覆盖。
- NR使用的颜色域、曝光和模型前后处理；当前旁路在AA后场景末尾，不证明该位置适合作Pre-SR NR。
- 低分辨率SR输入、高分辨率结果、有效rect/采样常量修改及后处理回接。本轮全分配同尺寸CopyResource刻意保留区域外像素，不代表已实现任意区域回写。
- 持续运行的pass识别、帧身份、资源池/生命周期和游戏加载/相机/分辨率变化处理。
- D3D11→候选后端资源桥、NR/SR/FG调用，以及最终开启超宽屏补丁后的全幅验证。

下一步应把真实格式、rect、速度规范化、无限远投影候选及颜色/曝光未知项带入后端输入契约比较，确定最窄可复用接口。无需为了让模型先跑而把未知项填成默认常量。
