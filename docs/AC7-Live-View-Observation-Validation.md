# 第四轮实际游戏：默认关闭观察通过

输入：[fourth-game.log](../artifacts/research/view-cache-rotation-v1/fourth-game.log)，原文件AC7NativeNeutral-227992-209080265.log保留。用户维持ReShade禁用、宽屏补丁启用；本轮没有要求任何额外控制文件。

## 已通过的实际门槛

- 两类shader均捕获，ownerDraws518109、otherDraws0，实际原生owner分流持续正常。
- 固定5120×1440全宽viewport与View矩形一致；guide与prewarp的depth/View/viewport及完整View字节已按当前严格协议关联。
- 八个view事件counter与identity分别相等，依次为62、63、64、66、68、69、70、71，严格递增，首尾156ms。
- 最后bounded-stop：observed8、error0、applied0、stopped1、drained1；这是成功达到观察上限，不是异常停用。
- uploads57823、cacheEntries16、cacheEvictions1024，mapped0/mapEvictions0。正常轮换未再触发旧error10，缓存仍有界。
- 所有记录enabled0/applied0。本轮确认观察协议，没有执行场景复制、读回图像或模型。

## 拒绝和边界

**八个合格帧不是八个连续帧。** 62至71间缺65和67，日志明确给出65的frame-clock-counter拒绝；67未被接受，且该区间refused增加，但去重日志不单独重报原因，所以不能把67的精确原因当已知。

启动阶段存在counter1的guide-view-conflict/guide-epoch-conflict，及首次同hash prewarp draw的PS CB1仅512导致cache miss；随后实际4KiB View匹配成功。不能再把那条512字节记录推断成所有相关draw都没有View，也不需要据此马上改为宽松slot fallback。

当前严格时钟检查保持不变。进入时间模型前应进一步核具体两套时钟差异、历史断链和Reset；没有因此容忍不明数据、补假帧或把丢帧当连续。该日志未记录逐分量时钟值，无法据它确定浮点舍入、时钟回退或其它断链细节。

观察成功意味着“实际上传身份和绑定关联”通过；不等于depth/MV像素内容、原生HDR语义、全宽复制的状态/退休/同帧warp消费、NR/SR/FG、性能或画质已验。

## 下一步

固定当前观察版本及此实际日志为通过基线，不必重复相同观察测试。下一阶段才是：给现有只支持中央2560复制的NeutralEntry补齐全宽5120中性路径，在独立宿主验证原图/状态/退休/原Draw继续后，再进入用户操作的最多8帧实际中性复制。全宽复制在当前版本仍明确禁止，不因观察成功而自动启用。

当前不新建ZIP，不自动部署/启动游戏/调整模组或设置；本轮仅解析归档并更新状态，无新二进制、GPU/model运行或提交。NR0/SR0，Opti累计198/218未变，仍81021a0。
