# 真实八帧离线NR→HDR→SR历史链

## 范围

**后续进展：** 已新增同adapter D3D11↔D3D12共享纹理/fence传输原型，通过真实八帧逐位往返及pending/晚帧回退；另做太阳运动方向和局部细节检查。见[共享桥验证](AC7-Shared-Bridge-Validation.md)。新桥还未接NR/SR，不能将本页文件模型链与传输原型组合叙述为已经完成实时模型桥。

使用[真实动态八帧](AC7-Dynamic-Eight-Frame-Validation.md)`698→705`，固定8帧，不拼接独立对、不复制单帧伪装时间序列。输入`artifacts/captures/live-eight-58000-130125640/`，本轮实验`artifacts/research/live-nr-sr-eight/`，所有新输出单独目录。

沿用已核正式公共NR/SR310.9.16.0，不改模型/SDK/NGX，P=1继续显式暂用。仍为2560×1440同尺寸SR/DLAA、actual jitter=0，不宣称超分/FG/完整时域重建质量。文件链为：native prewarp HDR→D3D11 SDR proxy→D3D12 NR→D3D11恢复HDR→D3D12 SR。未启动AC7本体、派生重放或Nsight，用户已关闭NVIDIA自动优化并只读确认0，不写游戏/NVIDIA配置。

## 最小扩展

- 既有gpu-live-pair只追加`--eight`，旧调用默认2，复用HdrProxy/GuideNormalizer原GPU shader；固定2/8而非任意长录制。
- `prepare_live_sr_pair.py`的audited_frames复用双schema，8帧使用新audit.frames及adjacentInputSupported/dynamicInputSupported，2帧保留旧viewMetadataCandidates。核源哈希、布局、每个真实View计数/+1，8帧还核index/前后时间字段。
- 原NR/SR prepare脚本按实际已审计2/8帧循环；每帧使用自己的HDR、depth、motion、View与exposure。SR还包含`frame-time.bin`，8帧通过公共NVSDK_NGX_Parameter_FrameTimeDeltaInMsec传实际simulation delta（毫秒），不假设固定16.67ms。NR未找到对应feature-specific时间合同，未虚构参数。
- preflight追加`d3d12-nr-eight-history`/`d3d12-sr-eight-history`/`d3d12-sr-eight-reset-control`。sequenceMode3/4对应八帧history/control，原1/2双帧模式保留。每帧输入、输出目录冲突都在Init前检查；Evaluate继续复用同一feature/textures/uploads/readbacks，逐帧提交fence/读回/调试检查/清理。
- 复用并扩展SR/完整链checker，新增小NR序列checker。旧两帧完整链与SR结果重新检查通过，不为回归再调用模型。

## 数值检查修正与失败证据

最初八帧NR准备被旧“两份FP16 motion差>.01px”断言拒绝，结果保留于`nr-input-rejected-fixed-001/`及`prepare-nr-process.log`。该断言在此前小位移样本成立，但在64px附近FP16相邻可表示值间距为.0625px，正常舍入边界也可导致两份half相差.0625。

主会话按原View、native depth与物体MV重算所有GPU未舍入值与CPUhalf差>.009px的位置，共4,827,867像素；原始浮点最大差仅.0002741813659667969px。详见[边界复算](../artifacts/research/live-nr-sr-eight/fp16-edge-reference-checks.json)。新的检查比较GPU未舍入像素值是否位于CPUhalf的半量化bin加.001px float算术余量中；每个像素均检查，没有修改输入值、限幅motion或用模型输出反推阈值。最大half→half差.0625，GPU自身float→half舍入最大.03125符合该表示精度；zero/subnormal自检和错误.1px拒绝通过。一次zero分支自检发现frexp(0)边界并在准备前修正，失败日志保留。

只读Luna审查dispatch/reset/公共参数，原报告与父会话复核在[eight-frame-contract-review.md](../artifacts/research/live-nr-sr-eight/eight-frame-contract-review.md)、[parent-review-notes.md](../artifacts/research/live-nr-sr-eight/parent-review-notes.md)。其“logs/output不同导致覆盖检查缺失”结论被主会话实码纠正：logs就是传给Evaluate的output，已有guard覆盖同路径；last-frame7冲突与原输出重用均在Init前拒绝。对元数据错序/时间绑定的建议已落实。父会话保持全部写入/GPU/验收责任。

## 运行与验收结果

**完整八帧链已通过执行与数据身份资格，首次实现本项目真实8帧NR→HDR→SR同特性历史求值。** 新增NR8次、SR16次（history8/control8）；非完整画质验收。

| 检查 | 结果 |
| --- | --- |
| GPU prepare | 8份全场景中性HDR往返逐位，depth32对CPU原生crop逐位，validity仅1/2，debug0 |
| GPU guide→NR | 所有代理RGB有限SDR，8帧自己的GPU guides；输入原始source/hash配对通过；quantization-aware float检查逐像素通过 |
| NR history | 同feature/4纹理，Reset[1,0,0,0,0,0,0,0]；8Evaluate+fence成功，24输入读回逐位，所有RGB有限且在[0,1]，debug0，释放/参数/Shutdown成功 |
| GPU restore | 8帧重新生成proxy与NR实际源逐位才发布，原有HDR/高光恢复、区外0，debug0 |
| SR history/control | 各自持久feature/5纹理；history首Reset后0，对照每帧1，16Evaluate/fence成功；每支32输入逐位，16×11,059,200 RGB全有限、负RGB0、alpha有限，debug0/清理成功 |
| SR帧时间 | 首帧17.477989ms，其后15.926838/16.668797/32.851696/88.590622/23.591518/90.177536/18.362999ms；两支实际公共参数日志均匹配原View |
| 首帧控制 | history与reset-control整个raw逐位一致 |
| 第2–8帧 | 两分支均出现差异：改变RGB像素约337–359万；全通道mean abs HDR差约.0426→.0947（末帧），仅诊断不是误差/分数 |
| 输入/输出拒绝 | 缺晚帧输入、frame7输出冲突、旧输出复用均Init前拒绝；错proxy在恢复场景发布前拒绝；篡改最后计数/时间的audit拒绝 |
| 回归 | 旧两帧SR checker、完整链checker均消费原证据通过；无额外模型重跑 |

**本次高峰不是链路新增的异常亮点证据。** 原始HDR峰分别为12288×前4帧、12160×后4帧，位置随画面太阳亮点从active(1423,35)移动到(1437,277)，每帧>1000约1049–1139像素。NR恢复后峰逐帧相同，SR history峰12408→12136、reset峰12408→12128；仍是数值/稀疏高光诊断，不能据峰接近或均值判稳定性合格。原始统计见[original-highlight-stats.json](../artifacts/research/live-nr-sr-eight/original-highlight-stats.json)。

[八行三列预览](../artifacts/research/live-nr-sr-eight/comparison.png)按698–705排序，每行依次为原始HDR、NR恢复HDR、NR+SR history，同一ACES/gamma显示变换；主会话已查看。可见飞机/海面/天空随真实视角推进；缩略图不能验证细小拖影、闪烁或原游戏显示观感。没有原warp/LUT/后处理/UI，不把此图当最终画质。初次预览进程15秒预算不足被终止，没有完整图片；随后允许50秒成功，日志两次保留，无模型重跑。

主要证据：[NR执行](../artifacts/research/live-nr-sr-eight/nr-checks.json)、[SR两支](../artifacts/research/live-nr-sr-eight/sr-checks.json)、[全链身份/区域](../artifacts/research/live-nr-sr-eight/chain-checks.json)、[覆盖拒绝](../artifacts/research/live-nr-sr-eight/overwrite-guards.json)。文件/目录仍有历史pair命名，实际contracts帧数组长度8与循环边界是验收依据。

## 限制与下一步

只有约0.286秒实际输入，写盘造成不均匀delta；不能从history/reset结果差异本身判定哪支更好，也不能把NR引起的灯光/材质变化算缺陷。NR公共Reset1→0执行不证明内部算法究竟如何使用历史。jitter真实0，完整guide遮挡/透明/粒子语义、切镜头/reset/动态曝光稳定性还未验收。

实时D3D11↔D3D12桥、同步、原warp/postprocess接回、失败回退仍未实现；此阶段不能让用户在游戏内开启模型。下一步应基于本序列看具体稳定性/对应问题，再做最窄实时桥设计，不因期望NR改变外观而压制其输出。
