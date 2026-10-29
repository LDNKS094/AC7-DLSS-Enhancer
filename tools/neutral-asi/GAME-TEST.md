# AC7 中性入口：先观察，不改场景

> 第四轮实际观察已通过：8个合格全宽View、零复制、error0并正常停止，详见[实机观察报告](../../docs/AC7-Live-View-Observation-Validation.md)。当前无需重复同一观察测试或额外文件操作。下一阶段的全宽中性复制尚未启用，需要先完成独立验证；以下保留前几轮操作记录。

> 当前第三轮：用户已完成ReShade隔离，原生owner已通过。新版本修复View缓冲轮换容量早停。现在保持现状，退出后仅换本地新ASI，正常飞行退出取日志即可；观察模式不需要创建或配置额外文件。见[轮换修复](../../docs/AC7-View-Cache-Rotation-Validation.md)。下方启用机制仅为未来阶段说明，不是当前步骤。

> 第二轮实际日志已证实ReShade代理context未命中owner，当前有一项具体可恢复隔离：用户退出后仅把dxgi.dll改名dxgi.dll.disabled-neutral，宽屏/dinput8/dwmapi和其它模组保留，复用现有ASI、不开.enable。测试后退出改回即可。见[具体证据与步骤](../../docs/AC7-ReShade-Context-Isolation.md)。这覆盖下文早期“无具体冲突时不动模组”的当前操作；不要扩大清理。

这是固定5120×1440分配的有界诊断。当前直接使用本地构建的ASI，不创建ZIP/发行包。默认仅观察最多八个匹配的真实View帧，支持中央1280,0,2560,1440和宽屏补丁可能产生的全宽0,0,5120,1440两种布局；不执行中性复制，也不调用NR/SR/FG。全宽布局只允许观察，启用复制会error13停用；实际游戏View关联仍待新日志确认。

## 第一轮：只观察

1. 完全退出游戏。从 `build/two-frame-sampler/Release/AC7NativeNeutral.asi` 复制本地新构建文件到 `F:\SteamLibrary\steamapps\common\ACE COMBAT 7`，与Ace7Game.exe、现有dinput8.dll放在一起。如果该目标ASI已存在，先将旧文件备份到游戏目录及其插件搜索目录之外，不能直接覆盖，也不要在游戏目录留下仍以.asi结尾的备份。
2. 保留现有dinput8.dll、dxgi.dll、dwmapi.dll及其它模组。本包不包含替换它们的DLL。**不要创建AC7NativeNeutral.enable**；该标记文件存在才会启用中性复制。
3. 正常启动游戏，进入与既有八帧捕获相同的普通屏幕飞行场景。无需改变游戏/NVIDIA设置，不要运行旧AC7 replay。
4. 完全退出游戏，查看新生成的 `AC7NativeNeutral-进程号-时间.log`。将这份日志交给开发端检查即可，不需要新图像捕获。

正常日志会出现installed、guide-shader、prewarp-shader；view行显示八个递增的真实counter，最后bounded-stop。首轮应始终enabled=0、applied=0、error=0。observed=8说明两类shader、View上传、depth/View/viewport关联和帧时钟满足此固定协议；仍不代表模型或画质验收。

如果只有bootstrap/installed而没有两类shader，或observed始终不到8，保留完整日志。新版还有event=diagnostic行：foreign-guide/foreign-prewarp表示目标在不同context；view-cache-miss配合cbSeen/decode与maps/ownerMaps/updates/ownerUpdates区分上传覆盖和View校验；viewport/cb-range指出具体绑定问题。每类只记一次，最多32条补充诊断，总日志64行。module只标记已知d3d11/dxgi/dwmapi或other，不输出私有路径；rawCounter是尚未接纳的上传数据，不是合格frame身份。

若使用宽屏补丁，可以保持启用。不要因为未匹配就自行移除补丁、ReShade或其它模组，也不要改分辨率或创建enable。

## 之后的中性复制试验

**当前全宽布局禁止启用，继续只观察。** 仅中央布局的身份/时序确认后，才评估手动创建空文件 **AC7NativeNeutral.enable**（注意不是.enable.txt）。下次启动会在最多八个已关联的真实View帧中执行scene→scratch→scene中性复制，随后自动停止并非阻塞退休。没有模型，也不是持续画质模式。开始时若GPU仍在途会保留原图并跳过，不等到同帧后续draw再补交。

启用标记仅在启动时读取；运行中删除不会热切换。需要中止时正常退出游戏。重新运行前删除标记即可回到只观察。各次日志独立创建、不覆盖；上限64行，没有图像、完整View或个人数据转储。

## 撤回

完全退出游戏后，仅移走本次添加的AC7NativeNeutral.asi及可选的AC7NativeNeutral.enable，日志另存。若此前另存了同名旧ASI，再恢复原文件。其它DLL、模组和配置不动。

本包会将模块/回调固定到进程退出，不支持运行中卸载。含少量有界状态日志，性能未验。真实设备失效恢复、resize、shader动态重建、多模组兼容以及NR/SR/FG均未验；遇到error保留日志并退出即可，不自动清理环境。

MinHook许可随包保留于MinHook-LICENSE.txt。manifest.json记录二进制与来源校验，当前本地提交仍81021a0，之后的实现尚未提交。
