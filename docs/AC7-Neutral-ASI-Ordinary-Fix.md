# 普通 D3D11 设备 ASI 复制路径修复

> **后续View协议与观察包**：[View/guide/loader验证](AC7-View-Identity-ASI-Validation.md)已接CPU上传中的真实counter/时钟及完整View匹配，独立普通/debug八帧和pending丢帧恢复通过；已安装UAL副本自动Initialize已验。现有默认关闭观察包待用户实际游戏日志，不再只有显式测试token；本篇保留复制路径修复时的边界。

结论：将中性复制从Draw hook内直接发出的immediate命令，改为私有deferred context录制、`ExecuteCommandList(..., TRUE)`执行后，**普通设备与debug设备的ASI完整矩阵都通过**。此前“普通设备hook不合格”的具体阻塞已解除；这仍是独立合成宿主资格，不是游戏接入或可部署包。

本轮NR0/SR0；无游戏启动、部署、模组清理或新增提交，仍81021a0。

## 分步骤定位

同一独立宿主、普通D3D11 FL11.0设备、真实捕获pixel shader/scene，在相同MinHook入口依次编译以下诊断版本。每个版本单独进程，不覆盖失败：

| 阶段 | Draw前操作 | 结果 |
|---|---|---|
| 0 | 只转发原函数 | 原draw3/query4、输出/状态通过 |
| 1 | 加入shader/scene/depth/View/viewport读取与校验 | 同上通过 |
| 2 | 再加入私有context state swap/恢复，不复制 | 同上通过 |
| 3 | 两次区域复制，再恢复；尚未Signal退休fence | GPU查询超时退出10；GetDeviceRemovedReason=0x887A0006（DXGI_ERROR_DEVICE_HUNG） |
| 5 | deferred list录制两次复制，以RestoreContextState=TRUE执行并Signal fence | 小矩阵通过，普通设备完整矩阵随后通过 |

所以可以把首次失败缩小到**此ASI回调中的直接复制执行路径**，而非透明转发、纯getter、state swap单独操作或退休fence。它不证明底层根因一定在driver、D3D11 runtime、MinHook或某个寄存器；没有继续做driver/私有ABI修改。

stage3的drained=1只因为该诊断版本故意不提交fence，不是GPU完成证明。原日志还保留applied1/intercepted2/original2；调用方确实继续调用了原draw，但该次GPU没有正常完成。

各诊断宏/临时CMake开关的源码已冻结在[证据目录](../artifacts/research/neutral-operation-v1)的probe-source-*，正常代码和构建中不再保留这些切换。旧失败ASI、INCOMPLETE和stage3错误仍完整保留。

## 当前默认实现

[NeutralEntry](../src/d3d11/neutral_entry.cpp)保留原来的匹配、布局/设备/viewport检查和默认关闭规则，只替换命令执行方式：

1. 创建owner时创建一个私有deferred context；不是每帧创建设备或context。
2. 验证通过后，录制scene有效区→scratch→同一scene有效区两次CopySubresourceRegion。
3. `FinishCommandList(FALSE)`生成命令列表；主immediate context执行`ExecuteCommandList(list, TRUE)`，由运行时恢复完整管线状态。
4. 在同一immediate context Signal退休fence。场景、scratch和命令列表保留到fence完成；drained才释放该帧引用。

没有CPU等待、图像读回、模型、临时shader或ReShade调用。每帧GPU仍是两次区域复制，不改变颜色。该路径的CPU/driver开销尚未测量，不能把功能通过当性能通过。

[ASI](../tools/neutral-asi/neutral_asi.cpp)继续沿此前debug通过的双公开入口MinHook＋线程局部重入保护，不再使用失败的vtable替换。原DrawIndexed仍无条件继续；进程固定模块/owner、无热卸载的边界不变。

## 最终复验

所有最终测试使用同一份final.asi/final-host.exe，等于最终构建产物：

| 测试 | 结果 |
|---|---|
| ordinary-final，普通设备实际LoadLibrary/安装hook | 原Draw8次、7次pipeline statistics；中性2次、拒绝2次、6份绘制输出及scene逐位；关闭/停止/pending8/drain通过，设备正常 |
| debug-final，开启D11 debug layer | 相同完整矩阵通过，error/corruption0 |
| core-final，无ASI直接调用新中性核心 | 全幅scene/中央crop逐位、六stage等状态快照、隐藏CS UAV counter5且原CS继续后6、拒绝/pending/stop通过，debug0 |
| replacement-final，debug设备shader换对象 | error5停用并锁住owner，原Draw3次继续、scene不变/正常drain/debug0 |

普通设备没有开启debug layer，因此其checks中的debugErrors=0不是“普通设备收集了debug消息”；API正确性由同实现的debug复验补充。两者都检查实际GPU查询、绘制字节、状态及设备状态。

精确shader hash、原view形状、有效区外保持、重复token/错viewport、启用/关闭、在途停止/拒新、原Draw继续都保持原控制。getter快照仍是合成绑定集合，未穷尽非空SO隐藏计数和动态shader class实例；不把所测集合扩大成所有可能状态的实测。没有因为失败而关闭断言、缩减普通设备矩阵、去掉退休fence或跳过原draw。

## 证据与后续边界

- [neutral-operation-v1](../artifacts/research/neutral-operation-v1)包含stage0/1/2/3/5诊断、普通完整试验、四个最终回归、全部binary/构建/退出/日志和状态。
- [check_neutral_operation.py](../tests/d3d11/check_neutral_operation.py)独立核阶段结果、失败保留、两设备全幅绘制与scene字节、核心/替换控制；最终[independent-checks.json](../artifacts/research/neutral-operation-v1/independent-checks.json)明确syntheticQualified=true、deployableGamePackage=false；伪造少一次原draw的负对照已被拒绝，未写成功报告，也没有额外GPU运行。
- 上轮[ASI失败报告](AC7-Neutral-ASI-Validation.md)保留其当时结论和所有失败。本报告/PLAN覆盖其“普通设备仍阻塞”的当前状态。

下一步：连接真实View/guide身份与连续帧token，核用户ASI loader是否调用InitializeASI以及是否早于shader创建，再准备默认关闭、固定尺寸、仅中性模式的可审阅游戏包。没有直接用Draw计数造帧、没有主view语义资格，不因合成宿主成功宣布AC7已兼容。

设备丢失恢复、resize、动态shader重建、热卸载、多模组共存、模型接入与FG/性能/画质仍未验。用户已要求兼容后置，目前不需要清理游戏模组；只有后续具体冲突才提出可恢复清理步骤。配置/NVIDIA autoOPS0/22个AC7登记路径及游戏插件哈希复核不变。
