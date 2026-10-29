# 带补丁 C++ Capture：首次重放与资源证据

后续正常飞行数据已取得，见 [2026-10-28正常飞行证据](AC7-Nsight-Normal-Flight-20261028.md)。该新帧未开补丁，已确认物体速度写入和相机回退；本文“全零”仅指本任务失败帧，末尾补采请求已由新帧完成，不再是当前待办。

## 结论

捕获 `Ace7Game__2026_10_27__07_41_15` 已用原始生成源码构建并连续重放3次，退出码0。随后在独立源码副本插入5个读回检查点，重放1次，读回关键纹理；最终输出与原始 `screenshot.bmp` 的8-bit RGB比较最大误差1、平均绝对误差0.0004709201，超过1的通道数为0。该捕获的最终画面已验证可重现。

但保存画面是 **飞机爆炸 / MISSION FAIL / 字幕**，不是稳定飞行。可以用来确认本帧的处理链和UI分离，不能代表正常飞行的速度、TAA、jitter或历史重置条件。补丁文件在捕获前已恢复；本帧场景处理及最终输出viewport均5120×1440，图像无中央16:9黑边限制，但投影是否完全正确仍待普通飞行对照。

原始捕获目录、data.bin及生成源码未改动。编译产物及仪器化副本在忽略的`build/`；读回和日志在忽略的`artifacts/replay/`。捕获最初在用户Documents目录导出，后续检查发现已移至项目内`NVIDIA Nsight Graphics/CppCaptures/`（并非助手移动），现已更新 [捕获索引](../artifacts/captures/nsight-ultrawide-patched/capture-reference.json)。没有游戏注入、后端接入或Git提交。

## 构建与重放

- VS2022 Community、MSVC19.44.35227、Windows SDK10.0.26100.0、CMake3.30.5。
- 原始工程：`cmake -S <capture> -B build/nsight-ultrawide-replay -G "Visual Studio 17 2022" -A x64`，再`cmake --build build/nsight-ultrawide-replay --config Release --parallel 4`。
- 在原始捕获目录作cwd运行项目内`build/nsight-ultrawide-replay/Release/Ace7Game.exe -repeat 3 -automated -watchdog -log <absolute-log-file> -verbose`。这里是独立重放程序，不是游戏目录的同名EXE。`-log`参数要求文件路径；首次误传目录导致退出码9，改为日志文件后正常退出0，不能将参数错误记为图形重放失败。
- 仪器化重放用`tools/nsight/prepare-checkpoints.cjs`创建新副本，原始data.bin按cwd读取，不复制2GB资源数据。NVAPI目录为指向原目录的junction；保留原捕获后可重建。运行设置`AC7_REPLAY_DUMP_DIR`，`-repeat 1`，退出码0。GPU staging readback不改变原渲染资源内容。
- 两种重放均为执行/正确性检查，不是性能测试；没有使用API debug runtime。

## 索引与检查工具

`tools/nsight/inspect-capture.cjs`只处理当前Nsight生成语法，记录183张Texture2D、341个视图、141个VS/PS/CS和1444个Draw。检查了单context、连续Draw编号、绑定数组长度、blob记录边界、字节数和DXBC头。索引为`artifacts/captures/nsight-ultrawide-patched/analysis/source-index.json`。

这是有边界的源码索引，不是完整D3D11状态解释器：没有建模FrameSetup初始绑定、隐式SRV hazard清空、compute状态和所有资源拷贝；部分视图描述未解析。绑定存在不等于shader实际读取。后续候选必须交叉核对反汇编及GPU读回。数字均为**生成源码中的Draw序号，不是Nsight Events事件ID**。

`disassemble-shaders.ps1`用系统D3DDisassemble反汇编全部141份DXBC，输出在上述analysis/shaders目录。`check-readbacks.cjs`检查原始raw长度/格式、生成预览并比较最终输出；断言没有大于1的RGB误差。它只支持当前选定格式及24-bit未压缩BMP，异常格式直接停止。

## 当前帧的处理链

| Draw序号 | 输出 / 实际读取线索 | 判断和边界 |
| --- | --- | --- |
| 1359、1360、1363以及1372–1398若干draw | Texture2D210 / RTV41，5120×1440 R11G11B10_FLOAT | 浮点场景合成目标；用途依据绑定与后续采样，不仅看格式 |
| 1361 | Texture2D215；输入含场景/深度及Texture2D212 | 屏幕空间效果候选，不标记为最终场景TAA |
| 1362 | Texture2D216；输入深度199、当前215、另一纹理240及RG16纹理212 | shader有速度解码、相机矩阵重投影及历史邻域约束，符合时域效果处理；不能由此认定为主场景TAA |
| 1400→1401 | 场景210→2242→210 | 1401后读回场景HDR、深度及速度候选 |
| 1402–1404 | 场景下采样，16×920→16×2→1×1 R32G32_FLOAT(Texture2D204) | 曝光/亮度统计候选，具体比例和pre-exposure契约未知 |
| 1405–1426 | 多级降采样、滤波及3D LUT输出 | bloom/颜色处理候选；尚未逐个命名 |
| 1427 / PS294 | Texture2D206；读取场景210、半分辨率2246、3D LUT及其他输入 | 色调映射/颜色合成阶段，已读回可见场景；确切颜色空间待核实 |
| 1428 / PS264 | 206→208，5120×1440 | shader只声明并读取单张颜色t0，包含边缘方向/邻域搜索，符合FXAA类空间抗锯齿。此阶段不读取历史或速度；不标记为TAA |
| 1429 / PS451 | 218与1920×1080的111→219 | UI合成候选 |
| 1430–1436 | 219的多级下采样/滤波 | 后续UI复合用到其中两级 |
| 1437 / PS265 | 208与219、1016、1017、103→交换链RTV2 | 读回图像第一次包含MISSION FAIL，尚无字幕；确定本帧存在场景/UI分离边界 |
| 1438–1443 / PS267 | 字形纹理829(A8_UNORM)→交换链 | 最终读回出现AWACS/中文字幕；这6个draw改变23190个像素 |

关键图像：

- [抗锯齿后的独立场景](../artifacts/replay/checkpoints/1428-post-spatial-aa-preview.png)：没有MISSION FAIL和字幕。
- [UI合成后](../artifacts/replay/checkpoints/1437-composite-preview.png)：出现MISSION FAIL。
- [最终输出](../artifacts/replay/checkpoints/1443-final-preview.png)：再出现字幕，与原捕获图一致。
- [数值核对](../artifacts/replay/checkpoints/checks.json)。

这为同尺寸旁路与高分辨率输出回接提供具体候选，但不表示已经能安全替换此pass；视图尺寸常量、颜色/alpha约定、后续采样和跨帧资源身份都要核实。当前Draw序号/UID只适用于此捕获，不应硬编码为游戏运行匹配条件。

## 深度与速度

- 深度候选：Texture2D199，`pID3D11Texture2D_uid_1548`，5120×1440、R32G8X24_TYPELESS，DEPTH_STENCIL|SHADER_RESOURCE，场景多处以深度读取，DSV5/6/8。1401后GPU读回深度float有限，范围约0.000304682–0.001004637。近远平面、反向Z/投影线性化和透明物体覆盖未确认。
- 速度候选：Texture2D212 / RTV54 / SRV265，`pID3D11Texture2D_uid_1698`，5120×1440、R16G16_UNORM。
- `Frame0Part01.cpp:979`绑定速度RTV，986清零。当前索引未找到以此RTV为输出的draw；1401后GPU读回7372800像素全部为零，确定本帧这个资源不能直接充当有效物体速度。
- PS250（UID1728）反汇编明确对t3.xy使用`value*4.008016-2.003978`解码；以原始x>0作为有效条件，否则使用深度及CB1[110..113]矩阵重投影得到的位移。这是本帧相机回退路径的证据，不足以确定DLSS所需方向、单位、jitter处理及动态物体覆盖。
- 没有用零填充或猜测矩阵将输入契约标记为通过。

## 补采：一次正常飞行样本

假设：任务失败过渡导致当前可见对象和时域状态不代表正常飞行；正常飞行可能出现不同的velocity写入或AA分支。

只改变游戏情境：保持当前补丁、画质、5120×1440与Nsight启动设置，进入任务1 Free Flight，保持足够高度、飞机存活、稳定飞行且有正常HUD，使用F11捕获并导出整个C++ Capture，保留原目录即可。无需先手工导出事件JSON或大量截图，也不要求此时改变抗锯齿选项。

预期检查：确认普通HUD是否仍与scene分离；比较主场景AA shader/输入、velocity清零后的写入与实际像素、深度和视图常量。停止条件：取得一份有效稳定飞行捕获后先分析，不要求反复重抓；若仍全零，转向具体速度生成条件/相机重建调查，而非盲目提高AA或套用默认运动矢量。

jitter和跨帧历史无法由当前单帧可靠确认；等普通飞行样本定位明确后，再决定是否需要相邻帧或受控相机运动对照。
