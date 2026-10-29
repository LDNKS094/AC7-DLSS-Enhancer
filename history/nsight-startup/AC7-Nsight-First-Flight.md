# Nsight 第一帧飞行现场

> 历史记录：Nsight启动资格与首帧导出阶段，非当前操作计划。后续两份C++ Capture、GPU旁路及正式NR查询已完成；当前状态以 [项目README](../../README.md) 和 [API核对](../../docs/AC7-Graphics-API-Support.md) 为准。下文进程ID、等待导出和恢复步骤均为当时现场。

用户已提供任务1 Free Flight、机外视角的实际捕获截图。Present事件16152返回S_OK，交换链Windowed=TRUE、BufferCount=1、BufferUsage为SHADER_INPUT|RENDER_TARGET_OUTPUT。事件过滤开启，根marker待展开；截图状态栏帧30161与marker Frame30162需区分。具体字段及边界见 [第一帧证据索引](AC7-First-Flight-Evidence.md)。当前要求先导出Present状态JSON，再展开事件分组；尚无离线完整捕获。

## 当前证据

用户确认在AC7实际飞行时保持游戏焦点按F11，成功捕获，Nsight可显示事件列表和飞行画面。此前失焦暂停已通过目标捕获热键避免。此处是用户实测报告；助手尚未检查具体事件/资源，也未保存可独立重放的AC7捕获。

当前目标：保留live analysis会话，先建立可持久化的事件/资源证据，再定位pass。不能因进入捕获状态就宣布原生数据已确认。

## 第一批材料

输出目录：`E:\CodingProject\AC8_MOZA\AC7-Native-Adapter\artifacts\captures\nsight-flight-baseline`。

1. 保持当前捕获，不Resume、不结束调试、不重新捕获，不关闭目标。
2. 记录任务名、机外/座舱等视角、截图中是否有HUD、游戏报告的分辨率。保存值或窗口外观不能替代实际资源尺寸。
3. 用户发一张Nsight主界面截图，包含Events（优先显示UE标记分组）、当前选中事件/编号和飞行画面。此时先不要求用户盲搜TAA/深度/速度。
4. 根据截图定位最后一项场景/UI绘制和呈现事件，再通过Frame Debugger菜单中的API Inspector检查D3D11 Output Merger(OM)的render targets、depth-stencil与viewport。若截图不足以识别某个控件，先确认界面，不编造按钮。
5. 在该事件的API Inspector使用官方 **Export All Pages to Json** 功能导出，保存为`event-实际编号.json`；一个导出是该事件状态，不是整帧，也不能等同于独立可重放捕获。
6. 需要后续离线持久化整帧时，再核对该版Frame Debugger的Generate C++ Capture流程及当前状态是否需要resume，不能先宣称live capture可以直接另存为RenderDoc RDC或新版Graphics Capture格式。首轮不在保全材料前切换活动。

## 第一帧分析顺序

先看事件/marker结构，再沿帧尾UI向前找无UI场景颜色与后处理边界；记录每个候选生产/消费事件、资源ID、格式、尺寸/有效区域。深度与速度只是候选，须结合资源绑定、shader和后续运动对照确认；不能凭外观认定。

首轮应回答：当前捕获实际API/目标尺寸、场景与UI的顺序、干净颜色入口候选、同帧深度/速度/视图常量线索。jitter/曝光/历史生命周期需要进一步采样，暂不锁定后端。
