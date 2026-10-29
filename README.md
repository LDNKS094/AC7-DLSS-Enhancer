# AC7 Native Adapter

普通屏幕下的AC7原生HDR、guide、NR/SR与后续FG适配。当前仓库包含可复用D3D核心和开发原型，尚无完整可发布游戏插件。

开发进度先保存于检查点 **af7725e**，随后完成目录与文档整理；最新提交以 Git HEAD 为准。实际全宽View绑定观察已通过，场景复制和NR/SR尚未在游戏中启用；下一阶段是全宽中性复制验证。

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
