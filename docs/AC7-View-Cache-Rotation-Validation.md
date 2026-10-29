# 原生入口已命中：View缓冲轮换容量修复

第三轮用户日志：[third-game.log](../artifacts/research/view-cache-rotation-v1/third-game.log)。用户已禁用dxgi.dll，也明确从未创建任何启用文件；当前观察流程不需要额外文件，后续不再反复提醒。

## 实际日志结论

owned-guide来自d3d11.dll，ownerContext/deviceEqual/identityEqual均1，contextType0。到错误时ownerDraws405、otherDraws0、ownerMaps652、ownerUpdates697，uploads78。可确认原生owner分流已经工作，前一轮ReShade代理不匹配已被隔离。

观察器随后在error10停用，当时prewarp尚未捕获、observed0/applied0。旧error10共用catch且没有异常文本，不能仅凭该日志宣称精确异常栈已确定。检查发现两个不适合正常RHI轮换的容量假设：View缓存整个进程最多16个对象、同时Map跟踪最多16个，超限都直接停用。

## 可复现错误

使用同一份旧ASI，在独立宿主通过真实UpdateSubresource上传32个不同合法View buffer，准确复现error10/stopped1、uploads只到16。旧binary/header、repro-host、失败INCOMPLETE与rotation-check.json全部保留于[view-cache-rotation-v1](../artifacts/research/view-cache-rotation-v1/old-rotation-failure)。不是靠扩大上限或跳过断言通过。

## 修复

- [ViewUploads](../src/d3d11/view_identity.h)继续最多16条，按最近上传维护：优先复用失效条目，否则替换最久未更新的条目。只缓存当前窗口，不把RHI整个进程的对象数量当上限。
- 更新已存在条目的新旧顺序；淘汰的对象查找返回unknown，不返回别的对象或旧packet。非法新上传仍作废旧值，完整新上传才可恢复。CPU内存/COM引用仍有界。
- [Map跟踪](../tools/neutral-asi/neutral_asi.cpp)继续最多16条。遇到第17个同时映射，丢弃最旧的跟踪记录并作废其旧View元数据；不会读取已丢弃映射指针。原Unmap仍继续，下一次完整新上传可重新识别。
- 一般日志增加cacheEntries/cacheEvictions/mapped/mapEvictions，映射淘汰首次有独立事件，日志总64行不变。

这些只是元数据保留策略；没有放宽View矩形/时钟/jitter/完整字节/资源关联，也没有造帧token、GPU读回、关闭原Draw、增大无界缓存或改变全宽只观察限制。

## 验证

同一新ASI/host，构建零warning：

| 控制 | 结果 |
|---|---|
| 32个合法View轮换 | 新版uploads32/error0、不停用；旧版同API序列在16后error10 |
| 17个同时Map/Unmap | 只观察仍跟踪的16份，不读丢弃的首份；首份完整重传后恢复，error0 |
| 直接缓存断言 | 容量16、更新过的条目保留、旧条目查不到、非法/作废不回用、完整刷新恢复、clear生效 |
| wide-disabled，debug | 轮换压力后全宽8帧观察、复制0、scene/draw逐位、12拒绝、GPU/CPU View相同、stop/drain/debug0 |
| central-ordinary | 同压力后原中央8帧复制/输出/12拒绝通过 |
| central-pending，debug | 698在途/drop699/退休不晚补/700恢复，stop/drain/debug0 |
| wide-guard，debug | 全宽误启仍error13、复制0、原scene/draw逐位 |

[check_view_cache_rotation.py](../tests/d3d11/check_view_cache_rotation.py)独立核实际第三轮日志、旧失败保留、新轮换/映射断言与每进程有界日志、16帧图像/scene字节与原counter、pending/全宽禁止控制。[independent-checks.json](../artifacts/research/view-cache-rotation-v1/independent-checks.json)明确actualGameAfterFixQualified=false。伪称丢弃映射也被读取的负对照被拒，未额外GPU运行。

新版本本地输出仍[AC7NativeNeutral.asi](../build/two-frame-sampler/Release/AC7NativeNeutral.asi)，等于证据fixed.asi，未部署。

## 下一步

用户保持当前状态：ReShade仍暂时禁用、宽屏补丁保留。完全退出游戏后只替换本地新ASI，正常进入飞行、退出并带回新日志；不需要创建或配置任何额外文件。先核不再早停、缓冲淘汰统计、随后prewarp/View身份，再处理可能的真实CB绑定问题。

本轮修复了可复现的容量设计错误，同时消除另一合法Map轮换边界；旧实机error10具体由哪一个throw触发未被旧日志记录，不能以合成复现代替修复后实机验收。实际主View/guide内容/全宽主动处理/模型/性能仍未验。

NR0/SR0、Opti累计198/218不变，无自动游戏启动/部署/模组调整/配置写入/ZIP/新提交，仍81021a0。
