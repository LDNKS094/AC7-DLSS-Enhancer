# AC7 Native Adapter

普通屏幕下的AC7原生HDR、guide、NR/SR与后续FG适配。当前仓库包含可复用D3D核心和开发原型，尚无完整可发布游戏插件。

可用`Ctrl+Shift+F10`在游戏前台切换NR，默认开；NR关闭时SR与原post继续，切换重置相关历史，快捷键版已获用户测试无问题。ASI同目录`AC7NativeModels.ini`在启动时读取NR五项参数，`Ctrl+Shift+F11`手动重载；缺失/非法项保留当前有效值，变化在成功提交时Reset，相同值不重复Reset。`Ctrl+Shift+F12`请求同帧NR前后HDR的共同映射BMP预览和元数据，保存到ASI目录`AC7NativeCaptures/`；均在SR之前，不是最终屏幕截图。不监控INI或每帧读回图像。当前仍为全宽均匀NR，未启用FoveatedDLSS5式分区压缩。集成主线：已恢复的D11→D12→D11入口，接入现有“官方Remix式SDR代理→NR→HDR恢复→SR”，再交回游戏原warp/post。当前NR与Pre-SR HDR位置需要颜色适配；现有ACES/gamma/高光默认值本就来自官方，本地只保留FP16量化基线等最小修正，详见[路线](development/docs/ROUTES.md)。优先完成模型输入/历史与实际回接，不再扩大初始化优化或时间测量。

开发进度先保存于检查点 **af7725e**，随后完成目录与文档整理；最新提交以 Git HEAD 为准。实际全宽View观察及有界中性复制已通过执行/停止/退休门槛（8合格、5复制、3busy、error0，用户无感知变化）；独立测量版及首轮实机计时已通过完整性检查：GPU复制中位0.0669ms，而单在途CPU退休门控使两个128帧窗口均仅66次提交。沿最终D11→D12→D11路径的中性版也已通过首轮实机：普通/计时各128合格View全部提交、busy0，error0/正常退休，用户无明显问题。分段实机已定位：起点至场景回写结束中位0.777ms，D12内部工作0.043ms；整体13.56ms中约94%在回写后的尾部间隙。初始化138.6ms约95%耗在D12设备/队列创建。提前初始化detail2在实机加载阶段崩溃，已撤回shader创建回调内的D12准备，恢复先前实机通过的首个合格View初始化时机；仍保留设备/队列跨布局复用。detail3本轮实机已恢复：448合格View完整结束、256次处理提交、busy0、error0，用户确认无崩溃；保留首View约126ms初始化峰值作为已知限制。当前开发候选`AC7NativeModels.asi`已接真实guide→NR→HDR恢复→同尺寸SR→原跨API返回；旧限时模型版的debug核心中央/全宽各3帧已验，现持续版又通过普通ASI512帧：首View启用、超过448仍运行；已获首轮持续模型实机结果：全宽至少2560对NR/SR、busy0/error0、持续启用，用户无明显问题，仅反馈可能轻微屏幕空间/后处理瑕疵待观察。仍保留detail3中性回退，不提前初始化或追加时钟门槛。原崩溃具体指令仍未知。

```text
src/                    可复用HDR、guide、共享桥与提交/退休核心
CMakeLists.txt          正式核心构建，默认不构建诊断工具
development/docs/       唯一活跃开发文档（5份）
development/src/        诊断ASI的临时核心
development/tools/      独立模型探针、数据准备、实验ASI
development/tests/      当前回归宿主与检查器
development/history/    旧报告、停用测试/工具、恢复清单
development/artifacts/  本地捕获/模型/证据（不入Git）
development/build/      本地构建/SDK（不入Git）
```

核心构建：`cmake -S . -B development/build/core`，然后`cmake --build development/build/core --config Release`。开发宿主须显式开启`AC7_BUILD_DEVELOPMENT=ON`，详见复现说明。当前不制作ZIP，不自动部署或启动游戏。

[状态/下一步](development/docs/STATUS.md) · [路线](development/docs/ROUTES.md) · [数据契约](development/docs/DATA-CONTRACT.md) · [构建/复现/验证](development/docs/VALIDATION.md) · [游戏操作](development/docs/GAME-RUNBOOK.md)
