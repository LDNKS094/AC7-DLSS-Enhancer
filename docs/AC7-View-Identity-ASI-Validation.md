# View上传身份、guide关联与默认关闭包验证

> 后续实际日志与宽屏更新见[首轮入口诊断](AC7-Game-Entry-Diagnostics.md)：本次实际初始化/双shader时序已确认，View仍未确认；用户启用宽屏补丁，新本地ASI允许中央/全宽只观察、全宽主动复制error13禁止。当前不创建ZIP，使用本地构建；本篇保留旧中央协议/旧包资格。

结论：ASI已接入真实View上传观察协议，不再要求游戏调用者手填帧token。普通/debug独立宿主用保存的真实View/scene八帧698–705，通过实际D3D11上传hook完成关联；默认关闭观察8帧/零复制、启用后8次中性复制、在途丢帧/不晚补/下一帧恢复均通过。当前安装的Ultimate ASI Loader副本可自动加载并初始化ASI，独立宿主目标shader未漏。

**已准备供用户第一轮默认关闭观察的包，不等于实际AC7启动时序、主View或启用复制已验。** 本轮仍NR0/SR0；不启动/部署/清模组/改设置/新提交，仍81021a0。

## 实现和协议

- [view_identity.h](../src/d3d11/view_identity.h)解析固定4096字节View：126/127/128行的rect/size/allocation、114行零jitter、110–113有限矩阵、133.zw→136.zw两套有限非负时钟、137.y原始uint32帧计数。首token就是原始counter；有符号半区判定单调、扩展32bit wrap、拒绝重复/倒退，连续counter要求前时钟数值等于上次当前时钟。允许counter跳帧，不把缺帧补成假数据。
- CPU元数据来自owner immediate context的完整UpdateSubresource/UpdateSubresource1，或成功Map写入后、原Unmap调用前的映射数据。没有新增GPU→CPU读回或等待；CPU读写映射的4KiB观察及日志开销未作性能验收。非法新上传会使已有缓存失效，不能回用旧合法View。
- [ASI](../tools/neutral-asi/neutral_asi.cpp)额外捕获精确4240字节guide shader SHA256 `85ec4635ea1fee2851d5979da08a1013609ead56312747cc4f54ba2e80620af0`；仍以精确628字节prewarp shader为唯一复制入口。捕获已证实两者都是DrawIndexed(3,6,0)，不新增其它Draw类型/Present hook，不用捕获UID或Draw序号标识实况。
- guide draw读取PS t0原depth、t3原RG16_UNORM velocity、CB1 View和viewport。复用[GuideCandidates](../src/d3d11/guide_candidates.h)按depth/View/viewport选择；prewarp还必须匹配**完整View字节**。新counter清guide epoch，同counter内View冲突或多velocity歧义拒绝。PS CB1必须offset0、覆盖完整4096字节，viewport/min/max深度一致。
- source缓存最多16个实际布局匹配的View对象、16个在映射对象、16个guide候选；超限停用、原函数继续。不是全进程无限资源/bytecode日志。
- 对所覆盖的GPU CopyResource/CopySubresourceRegion/CopySubresourceRegion1写View，作废CPU缓存；未知ExecuteCommandList作废View/guide。自有中性command list有线程局部范围保护，避免回调重入拿同一mutex或误作废正在提交的身份。未观察到的deferred uploads不会被借作immediate数据。
- frame只在精确prewarp上被消费。busy同样消费真实帧身份并保原图，GPU退休后同counter再次draw也不能补交；下一个真实counter可恢复。关闭时也只计合格真实View帧，没有GPU场景复制；无论关闭/拒绝/异常/停止，原API和原Draw继续。
- 达到8个合格观察帧自动stop；启用时最多复制8次，busy可能少于8。stop仍非热卸载，进程固定owner，非阻塞退休沿用上轮已验路径。
- 状态ABI显式更新为2（新增上传/guide/观察/真实counter/token字段，size+version校验）；AC7NeutralSetFrame保留独立旧矩阵诊断用途，实际包使用View模式。实际View已消费后不允许切换外部帧源。

## 宿主结果

最终ASI为[final.asi](../artifacts/research/view-identity-v1/final.asi)，等于构建与包内文件；没有改变失败断言。

| 测试 | 结果 |
|---|---|
| ordinary-v2-final | 8真实View counter/token698–705、Update/Map/Update1轮换；中性8次；8scene＋8draw逐位正确、状态不变、GPU View与观察CPU字节相同 |
| debug-v2-final | 同一完整八帧通过，D11 error/corruption0 |
| disabled-v2-final | 同一八帧被观察、applied0、达到8后stop/drain；scene和draw仍逐位 |
| pending-final（debug） | GPU gate使698在途，699被消费且pending1；退休后699重复draw不晚补；700恢复，observed3/applied2/pending1；scene/draw/stop/drain/debug0 |
| explicit-ordinary/debug-final | 原有显式token矩阵回归：原Draw8、query7、中性2、拒绝2、像素/状态、stop pending8和退休均通过 |

每个新版View运行均有12个拒绝控制：缺guide、不同View对象、不同depth对象、相同counter但完整View变化、非法新jitter替换缓存、CB range偏移、三类GPU copy失效、未知command list失效、歧义MV、过期guide counter。所有拒绝仍执行原draw，12份输出与baseline逐位相同。

另有计数gap/wrap/backward/duplicate、时钟断链、短包/非有限矩阵的小型CPU断言。wrap等人为改包仅为负/边界控制，正例698–705来自已审计原始文件。pending试验保持RGB frame0，重放真实View698–700，只证明调度/身份，不做时间画质结论。

[独立checker](../tests/d3d11/check_view_identity.py)核24个正例帧的counter和token均等原View 137.y、48个scene/draw对比及48个拒绝图、pending恢复、原矩阵、loader结果和日志上限。GPU View字节核对发生在宿主assert，没有假称另存了GPU CB文件；独立checker核其成功状态及对应运行。伪造第一条counter为700的负对照被拒绝，未写成功报告，无额外GPU运行。

证据：[view-identity-v1](../artifacts/research/view-identity-v1)；最终[independent-checks.json](../artifacts/research/view-identity-v1/independent-checks.json)。首次普通设备初步fixture未建立guide专用VS/512字节CB0，后来据真实guide输入签名补齐；初步结果与中间binary仍保留，不作为最终debug或最终12项资格。最终新View矩阵均用完整guide fixture、真实捕获两份PS字节码、合成depth/MV和真实八帧View/scene。一次缺string include构建失败也保留，之后零warning构建通过。

## 当前安装loader的实际测试

只读识别游戏dinput8.dll：Ultimate ASI Loader，版本10.0.0.0-01eb152；dwmapi.dll为UE4SS Injection Proxy。没有把仅含InitializeASI字符串当作加载顺序证明。

将**现有dinput8.dll副本**与最终ASI放在独立host目录，调用公开GetStartupInfoW/DirectInput8Create，不手动LoadLibrary ASI、不手动调用InitializeASI，实际loader完成二者。等待installed后再由host创建D11设备，先guide后prewarp，两者都被截获。默认off/显式启动marker两种loader试验均通过，stop/drain正常。

[loader-defaultoff](../artifacts/research/view-identity-v1/loader-defaultoff)/[loader-enabled](../artifacts/research/view-identity-v1/loader-enabled)保存实际执行loader/ASI/host与日志/JSON；[loader-provenance](../artifacts/research/view-identity-v1/loader-provenance.json)核副本与游戏文件SHA256一致。上游源码未核实（固定版本raw地址404/API请求超时），没有以新版源码替代现有binary证明。未修改现有loader/UE4SS/ReShade，不需要先清模组。

这证明**现有loader能调用本接口**，仍不证明实际AC7什么时候创建目标PS。InitializeASI只启动worker，DllMain仍只DisableThreadLibraryCalls，不在loader lock内同步建D3D设备或强行等待游戏。真实游戏的shader是否早于worker完成，留第一轮默认关闭日志判断；不加启动延迟、不临时切回ReShade。

## 可审阅游戏包与下一步

- [历史包目录](../artifacts/packages/neutral-view-v1)；当时生成的AC7NativeNeutral-view-v1.zip当前已不在工作区，按用户指示不重建：仅AC7NativeNeutral.asi、README、MinHook许可、带hash的manifest。没有模型/loader替换DLL/enable标记；未部署。
- [独立操作说明](../tools/neutral-asi/GAME-TEST.md)：用户先退出游戏、仅放ASI，不创建enable标记，正常进飞行后退出，把唯一新日志带回。首轮检查两shader/真实counter/observed8、applied0/error0。
- 日志在ASI旁新建进程号/时间唯一文件、CREATE_NEW拒覆盖、最多64行，不转储图像或完整View；日志不可写则停用error12。启用标记仅启动时读取；首轮确认前不建议建立。之后可做最多8个匹配帧的中性复制，仍非NR/SR/FG。
- 若实际未匹配，先根据日志定位loader/对象/协议边界，不自动移动其它模组、改游戏/NVIDIA设置或要求新全图捕获。配置/8插件hash/22个AC7登记路径应保持原值。

限制：固定布局、零jitter/未校准P1不扩展、已覆盖的immediate上传/改写协议。未穷尽所有D11资源写法或非空SO/class实例；实际main-view语义、depth/MV内容及期间覆写、实际游戏线程/启动、设备错误恢复、resize、动态shader重建/卸载、多模组共存、性能、模型/FG/画质仍未验。默认关闭包用于取得当前缺失的实际证据，不是强行宣布游戏已接通。
