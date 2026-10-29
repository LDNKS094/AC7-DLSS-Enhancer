# 首轮游戏日志、宽屏布局与有界入口诊断

用户提供的首轮实际日志：[first-game.log](../artifacts/research/game-entry-diagnostics-v1/first-game.log)，原文件AC7NativeNeutral-195544-202548281.log保持不变。

## 实际已确认与尚未确认

- InitializeASI自然启动，719ms后installed；安装后3.656秒捕获guide shader，61.546秒捕获prewarp shader。本次实际启动中，hook已早于两类目标shader创建；不能再把“loader没调用初始化/目标shader已错过”列为这次的已知阻塞。
- 四行均enabled0/applied0/error0，两类shader最终均1；prewarp创建时累计259347次原DrawIndexed继续。
- **没有view行**，所以实际View/guide关联未通过。最后一行uploads0只是prewarp创建时快照，不能推断之后没有上传，也不能据此断言ReShade冲突、driver故障或宽屏就是唯一根因。

## 用户补充：测试启用了宽屏补丁

只读核对本地[scene_projection.cpp](../../AC7-Ultrawide-Fix/src/scene_projection.cpp#L42)：投影调整会暂时解除相机aspect constraint，明确以full view rectangle作为constrained输出，再恢复相机原字段。HUD居中部分不能与场景投影部分混为一谈。

旧ASI仅认可View/viewport中央1280,0,2560,1440。若场景投影补丁得到全宽0,0,5120,1440，旧协议必然拒绝；这是ASI观察协议没覆盖布局，不是补丁应被关闭的证据。投影矩阵数值变化本身只需有限，并未要求等于旧捕获矩阵。

用户当前指示：保留补丁状态，直接本地ASI验证，不创建ZIP/发行包，不自动部署/改模组。既有历史ZIP不更新、不作为当前操作入口。

## 新版改动

- 观察协议在固定5120×1440分配、原零jitter/时钟/counter条件下，只允许两个明确矩形：中央16:9和全宽32:9。实际RS viewport必须精确对应当前View自身矩形，guide/prewarp也必须一致。
- **全宽仅观察**。若启用复制，全宽首次合格匹配触发error13、stop且applied0，原Draw继续；没有把现有中央2560复制或固定NR/SR尺寸冒充全宽处理链。中央中性行为不变。
- 日志增加owner/other Draw计数、Map/Update总调用及owner计数、未知CL计数。各类拒绝只记录一次，最多32条补充event=diagnostic，总行数仍64。
- 目标draw在非owner context时只诊断，不自动接受：记录context type、GetDevice与IUnknown是否匹配、vtable公开Draw方法位于已知d3d11/dxgi/dwmapi还是other模块。模块分类用VirtualQuery及启动时缓存的模块句柄，避免在render锁里做loader模块名查询；deferred context不调用不支持的state getters。
- 绑定诊断包含CB大小/BindFlags/范围、实际viewport、最近8个上传对象中的命中与decode失败类别、6个rect数字和rawCounter。所有这些都只是诊断；rawCounter不作为合格帧，缓存miss不会因为诊断而放行。8记录保留有界对象引用，不转储完整View/图像或绝对模块路径。
- View decoder可返回size/rect/jitter/matrix/clock原因，接受条件与上述双矩形规则明确分离；旧非法覆盖作废、GPUcopy/未知CL作废、完整View字节比较、gap/wrap/stop/原draw延续保留。

## 验证

最终本地构建等于证据中的diagnostic-v2.asi，未复制进游戏。

| 测试 | 结果 |
|---|---|
| wide-disabled，debug | 以原698–705 View时钟/counter修改矩形为全宽的合成边界fixture；观察8、复制0，8幅全幅scene/draw逐位原数据、GPU/CPU View字节相同，12拒绝、stop/drain/debug0 |
| wide-guard，debug | 同一全宽fixture误启用得到error13/applied0/stopped/drained，scene及原draw逐位未改，debug0 |
| central-ordinary | 原始未改View/scene八帧，中央中性8次、12拒绝/原图状态与GPU View相同 |
| central-pending，debug | 698在途、699丢弃且退休后不晚补、700恢复，原scene/draw/stop/debug0 |
| 诊断foreign-context控制 | 另一真实D11设备的合法1像素原draw确实渲染；ownerContext/device/IUnknown均不匹配、contextType immediate，未自动收编、未改变owner帧身份 |

此前严格中央诊断版的disabled-debug及其binary/source也保留；最终宽屏/中央测试用host-v2/diagnostic-v2，构建零warning。全宽View只改矩形的fixture是**合成协议边界测试**，不是新游戏捕获，也没有全宽投影/GuideNormalizer/NR/SR质量结论。

[check_entry_diagnostics.py](../tests/d3d11/check_entry_diagnostics.py)独立核首轮实际事件顺序/状态、每进程日志归属/上限/去重/foreign字段、全宽与中央图片字节/原counter、error13前零复制、pending回归；[independent-checks.json](../artifacts/research/game-entry-diagnostics-v1/independent-checks.json)。源size/matrix失败理由也有小assert。

## 下一步用户操作

完全退出游戏后，仅手动替换为[本地新构建ASI](../build/two-frame-sampler/Release/AC7NativeNeutral.asi)，旧同名文件备份到游戏/插件搜索目录之外。**保持宽屏补丁、其它模组和设置；不创建AC7NativeNeutral.enable**。正常进入相同飞行场景退出后，带回新.log。完整操作说明：[GAME-TEST.md](../tools/neutral-asi/GAME-TEST.md)。

- 若出现view及bounded-stop，进一步核真实rect/帧计数和零复制；不能只凭布局声明NR/SR可用。
- 若foreign-guide/prewarp，按实际context/module证据评估最直接的入口隔离，不现在盲删ReShade。
- 若view-cache-miss，结合ownerMaps/ownerUpdates、cbSeen/decode定位上传覆盖或内容校验；若viewport/range/关联原因，按其实际数字处理，不放宽为假帧。

NR0/SR0，Opti累计198/218不变；不运行游戏或AC7重放，不部署、不清模组、不写游戏/NVIDIA设置、不新建ZIP、不提交。实际主View/guide语义、全宽主动复制、完整NR/SR/FG及性能仍未验。
