# 固定8帧采样升级包 v4

这是只读诊断采样升级，**不会在游戏内运行NR/SR/FG**。已通过硬件合成8帧、断档/失败拒绝及已有AC7捕获重放的传输检查，真实游戏8帧仍需用户本次测试。

适用于已经安装v3采样环境的当前AC7目录。包里只有新版addon、八帧触发命令、说明和哈希；不重复安装/改动已工作的ReShade运行时。

1. 完全退出AC7。保留此前所有采样目录。
2. 游戏目录：`F:\SteamLibrary\steamapps\common\ACE COMBAT 7`。将旧`ac7-capture-addon.addon64`移到游戏目录之外备份（避免留下另一个可被加载的`.addon64`），复制本包新版同名文件进去。
3. 同时复制本包`Request-Eight-Frames.cmd`到游戏目录。既有`dxgi.dll`、`ReShade.ini`、其他加载器、mod、画质和分辨率均不改。
4. 启动前将游戏目录内残留的`AC7-Capture.request`和`AC7-Capture8.request`移到备份处（若有）。新插件也会归档启动前的旧请求；不要运行旧两帧命令。
5. 正常从Steam启动，不经Nsight。进入可控制飞机、场景正常运动的飞行，保持普通追尾视角，不切镜头或暂停。
6. Alt+Tab切出，双击**游戏目录内**`Request-Eight-Frames.cmd`，**立即切回游戏，不等窗口关闭**。命令等待5秒后才发送请求。
7. 继续正常飞行约10秒，再退出游戏。采样器固定8帧自动停，不要再次运行命令；期间可能因写盘短暂卡顿，这不是性能测试。
8. 查看新建的`AC7-eight-frame-PID-TICK`目录。`capture.json`存在且无`INCOMPLETE`才是传输完成。若有`failure.txt`/`INCOMPLETE`或没有新目录，也只把目录路径/现象告诉助手，不反复触发。助手会读取ReShade.log，逐对核验7段时间/矩阵链和实际运动。

当前5120×1440分配下，8帧资源约900MiB，建议游戏所在磁盘至少空余1.1GiB。最多两个在途staging槽，逻辑GPU复制数据上限256MiB；驱动自身分配/行对齐、原游戏/ReShade内存另计。磁盘总量限制1GiB（含metadata预留），GPU队列拥塞/缺帧/写失败/10秒回调期限会终止，不跳帧、不同会话拼接或无限录制。无回调时无法强制墙钟计时退出，但也不会继续采入。

若游戏启动或触发异常：退出游戏，移除新版addon及Request-Eight-Frames.cmd，恢复备份的旧addon；只移动本次新增文件，保留日志/输出。不要删除或覆盖已有dxgi/ReShade/mod文件。本包未自动部署。
