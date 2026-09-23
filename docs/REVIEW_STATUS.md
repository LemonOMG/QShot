# 审查问题状态索引

> **这是「某个问题现在是什么状态」的唯一权威来源。**
>
> 三份审查报告（`CODE_REVIEW.md` / `_ROUND2.md` / `_ROUND3.md`）是**某个时间点的快照**：
> 它们记录「当时发现了什么、当时修了什么」，**不再更新**。任何关于「现在修没修」的问题，
> 答案只在这张表里。
>
> 为什么这么定：这个问题已经连续踩了三次。P1-3/P1-6/P1-8、P2-2/P2-3、R3-9 六条 —— 每一项
> 都在别的轮次里顺手修掉了，而审查文档一直挂着「未处理」。状态散落在 8 个小节里，
> 每个小节都在自己的时间点上是对的，合起来就是错的。代价是双向的：在已修好的项上重复劳动，
> 或者以为活干完了而实际没干。
>
> **契约（改代码时照做）**：
>
> 1. **修完一项 → 改这张表的一行。** 不要在审查文档里改状态 —— 那是快照。
> 2. **新一轮审查产生新编号 → 先在这张表登记一行**，再写进报告。
>    `tools/verify_review_status.py` 会在编号未登记时报错。
> 3. 改完跑一次 `python tools/verify_review_status.py`，它会检查编号双向一致、
>    状态词合法、以及「已修」行的落点文件真的存在。
>
> 本文由 `tools/verify_review_status.py` 校验，不是靠自觉。

## 状态词汇（闭集，校验器只认这五个）

| 状态 | 含义 |
| --- | --- |
| `已修` | 代码已改，且落点里能看到 |
| `部分已修` | 主体已改，剩余部分写在备注里 |
| `未修` | 确认仍未处理 |
| `不做` | 有意识地不做，理由在 `ROADMAP.md` 决策记录里 |
| `待人工确认` | 代码已改，但只有人能确认效果（本机环境测不了） |

## 索引

「落点」必须是**真实存在的文件路径**（校验器会查）。「依据」区分**本次复核**（2026-09-23 逐条读代码确认）
与**第 N 轮记录**（沿用当时报告里的结论，本次未重新验证）—— 两者可信度不同，不要混为一谈。
`build-review/` 下的证据图不在版本控制内（该目录被 `.gitignore` 覆盖），只在本机可查。

### 第一轮 —— `CODE_REVIEW.md`

| 编号 | 标题 | 级别 | 状态 | 落点 | 依据 / 备注 |
| --- | --- | --- | --- | --- | --- |
| P0-1 | 复制/保存丢失全部标注（数据丢失级） | P0 | 已修 | src/annotation/AnnotationLayer.cpp、src/overlay/SnapOverlay.cpp | 第 1 轮记录；`renderToImage()` 落地，复制/另存/贴图共用同一入口 |
| P0-2 | `Annotation` 存在未初始化成员（未定义行为） | P0 | 已修 | src/annotation/Annotation.h | **本次复核**：`lineWidth`/`mosaicSize`/`fontSize` 均有默认值 |
| P0-3 | 右下角缩放手柄命中区域偏移 | P0 | 已修 | src/overlay/SelectionGeometry.cpp | 第 1 轮只修了 `bottomRight`；`topRight`/`bottomLeft` 由 N-5 统一修完 |
| P0-4 | 隐藏工具栏时子面板残留 | P0 | 已修 | src/overlay/ToolbarWidget.cpp | 第 1 轮记录；`hideToolbar()` 级联 |
| P0-5 | Undo 按钮与 Ctrl+Z 行为不一致 | P0 | 已修 | src/overlay/ToolbarWidget.cpp、src/overlay/SnapOverlay.cpp | 第 1 轮记录；信号直连 `handleUndo` |
| P0-6 | 混合 DPR 多屏用 maxDpr 统一合成 | P0 | 已修 | src/app/ShotApplication.cpp、src/platform/windows/WinScreenCapture.cpp | M1：per-screen 捕获 + 每屏一个 overlay |
| P0-7 | 选中椭圆/箭头/马赛克/文字后子面板不弹出 | P0 | 已修 | src/overlay/ToolbarWidget.cpp | 第 1 轮记录；白名单删除 |
| P1-1 | 平台抽象被绕过，`core/` 接口形同虚设 | P1 | 已修 | src/core/PlatformFactory.cpp | M0：UI 层不再 include Windows 头 |
| P1-2 | `IWindowDetector::windowRectAt()` 的参数被忽略 | P1 | **部分已修** | src/platform/windows/WinWindowDetector.cpp | **本次复核**：正常路径已正确使用入参并做逻辑↔物理换算；`edata.found == false` 时仍回落 `GetCursorPos()`（:84），**该路径下参数依旧被忽略**。低危防御路径，未修 |
| P1-3 | 老式字符串信号槽连接 + 多余的 `dynamic_cast` | P1 | 已修 | src/platform/windows/WinGlobalHotkey.cpp | **本次复核**：`dynamic_cast<QObject*>` 全文 0 处；现用 `&IGlobalHotkey::hotkeyPressed` |
| P1-4 | 无单实例保护 + 热键失败无限重试 | P1 | 已修 | src/core/ISingleInstance.h、src/platform/windows/WinSingleInstance.cpp | M6；重试早已有上限 `kMaxHotkeyRetries` |
| P1-5 | 文本输入结束后快捷键可能失效 | P1 | 已修 | src/overlay/TextInputWidget.cpp | **本次复核**：`activateWindow()` 在 `setFocus()` 之前（与 R3-1 同根因） |
| P1-6 | 热键只支持 A-Z/0-9，且用了魔数 | P1 | 已修 | src/platform/windows/WinGlobalHotkey.cpp | **本次复核**：`virtualKeyFor()` 显式映射表 + `kHotkeyId` 常量（:100） |
| P1-7 | 单击即复制导致双击分支不可达 | P1 | 已修 | src/overlay/SnapOverlay.cpp | 第 1 轮记录；单击改为「进入编辑」 |
| P1-8 | 资源管理小问题（`trayMenu_` 泄漏等） | P1 | 已修 | src/app/ShotApplication.cpp | **本次复核**：`trayMenu_` 已是 `unique_ptr`；`globalHotkey_` 的所有权表达同步收敛 |
| P2-1 | 马赛克每次鼠标移动都全图扫描 | P2 | 已修 | src/annotation/AnnotationLayer.cpp | 探针 `probe_mosaic_incremental` 10 checks / 0 failures（98ms → 7ms）；M7 补缓冲区复用。建议里的 16ms 节流**刻意不做** |
| P2-2 | Idle 状态每次鼠标移动都全屏重绘 | P2 | 已修 | src/overlay/SnapOverlay.cpp | 探针 `probe_partial_repaint`：脏区 264×204 = 8.55% |
| P2-3 | 同一份全屏数据存了两份 | P2 | 已修 | src/overlay/SnapOverlay.h | **本次复核**：`backgroundPixmap_` 已不存在，只剩 `backgroundImage_` |
| P2-4 | 每次拖动/缩放结束都重新裁切三张大图 | P2 | 已修 | src/annotation/AnnotationLayer.cpp | 马赛克图层改懒分配；M7 补 `mosaicInkPresent_` 把关（原先清空后仍每帧 blit 全透明图） |
| P3 | 仓库残留 / 编译开关 / 测试 / 文档 | P3 | **部分已修** | CMakeLists.txt | **本次复核**：① 残留文件已清（`main.cpp`/`Main.qml`/`build_output.txt`/`err.txt`/`out.txt` 均不存在）；② `docs/ARCHITECTURE.md` 与 `docs/TASKS.md` 已不存在（描述失配的问题随之作废）；③ `.workbuddy-ai/` 已纳入版本控制；④ **`-Wall -Wextra -Wshadow` 已开**（`target_compile_options`，全量重建 24 个 TU、**零警告**）。**仍无测试、无 CI** |

### 第二轮 —— `CODE_REVIEW_ROUND2.md`

| 编号 | 标题 | 级别 | 状态 | 落点 | 依据 / 备注 |
| --- | --- | --- | --- | --- | --- |
| N-1 | 多屏 overlay 的键盘焦点错位 | P1 | 已修 | src/overlay/SnapOverlay.cpp、src/app/ShotApplication.cpp | 第 3 轮定位到真根因（R3-2：`toolbar_->show()` 让 `focusWindow` 变 null，与屏幕数无关）并修复。**多屏实际行为需真双屏人工确认** |
| N-2 | 工厂返回 `nullptr` 未在调用方处理 | P1 | 已修 | src/app/ShotApplication.cpp | **本次复核**：`onCaptureTriggered()` 已判空并 `qWarning()`；M6 |
| N-3 | 保存功能的三个缺陷 | P1 | 已修 | src/overlay/SnapOverlay.cpp | 第 2 轮记录；`saveToFile()` 检查 `QImage::save()` 返回值、失败保留 overlay、补扩展名 |
| N-4 | `setDevicePixelRatio()` 触发深拷贝 | P1 | 已修 | src/overlay/SnapOverlay.cpp、src/annotation/AnnotationLayer.cpp | 第 2 轮记录；去掉每帧 `setDPR`，改为懒分配时设一次 |
| N-5 | `topRight`/`bottomLeft` 手柄命中区仍然偏移 | P1 | 已修 | src/overlay/SelectionGeometry.cpp | **本次复核**：`hits()` lambda 统一 8 个手柄为「锚点 ± m 的 2m×2m 方块」 |
| N-6 | 提交了草稿注释 + 硬编码的刷新区域魔法数 | P1 | 已修 | src/overlay/SnapOverlay.cpp、src/overlay/FloatingPanel.h | **本次复核**：`MagnifierLayout` + `kMag*` 常量，`paintEvent` 与 `mouseMoveEvent` 共用同一份几何计算 |
| N-7 | 马赛克「增量」不彻底：包围盒是累积的 | P2 | 已修 | src/annotation/AnnotationLayer.cpp | M6；探针顺带抓到一个**既有 bug**：掩码按被裁剪的物理原点平移 |
| N-8 | `hoverTimer` 仍是无参 `update()` | P2 | 已修 | src/overlay/SnapOverlay.cpp | 第 2 轮记录；改为 `update(oldHover ∪ newHover)` |
| N-9 | 其余细节（5 条） | P3 | **部分已修** | src/platform/windows/WinWindowDetector.cpp、src/core/IScreenCapture.h | **本次复核**逐条：① `setGeometry` 重复调用 ✅ 已删（全仓只剩 `SnapOverlay` 构造函数一处）；② `currentOverlays_` 失效 `QPointer` ✅（`closed` 时整体 `clear()`）；③ `AnnotationLayer` 两处绘制逻辑 ✅ 已去重（`paintAnnotation()` 被 `paint()` 与 `renderToImage()` 共用）；④ **`EnumData` 未清零 + 仍用 `GetClassNameA`/`GetWindowLong` 未修**；⑤ **`IScreenCapture.h` 仍 include `<QScreen>` 未修** |

### 第三轮 —— `CODE_REVIEW_ROUND3.md`

| 编号 | 标题 | 级别 | 状态 | 落点 | 依据 / 备注 |
| --- | --- | --- | --- | --- | --- |
| R3-1 | 文本标注框根本无法输入文字 | P0 | 已修 | src/overlay/TextInputWidget.cpp | **本次复核**：`startInput()` 里 `activateWindow()`（3 处） |
| R3-2 | 选完区之后 Esc / Enter / Ctrl+Z 全部静默失效 | P0 | 已修 | src/overlay/SnapOverlay.cpp | **本次复核**：`reclaimKeyboardFocus()` 8 处调用点，覆盖工具栏显隐、工具切换、文本提交/取消、颜色与粗细变更 |
| R3-3 | `Qt::Tool` 子窗口的按键不会向父窗口传播 | P1 | 已修 | src/overlay/ToolbarWidget.cpp、src/overlay/TextInputWidget.cpp | **本次复核**：3 处 `Qt::WindowDoesNotAcceptFocus`，改为主动取回激活而非依赖父窗口兜快捷键 |
| R3-4 | 多屏下「局部坐标 / 全局坐标」混用 | P1 | 已修 | src/overlay/SnapOverlay.cpp、src/platform/windows/WinWindowDetector.cpp | M1：`screenGeometry_` + 在边界处 `translated(-globalOrigin())` 归一化一次。**真双屏行为需人工确认 —— 本机只有 1 块屏** |
| R3-5 | 右键取消马赛克笔迹后，马赛克仍出现在成品图里 | P1 | 已修 | src/overlay/SnapOverlay.cpp | **本次复核**：`rebuildMosaicCache()`（:620）。**遗留决策**：马赛克仍是「拖动即写入」，见 `ROADMAP.md` 第八节决策 2 |
| R3-6 | 除 Idle 外所有状态都是整屏重绘 | P2 | 已修 | src/overlay/SnapOverlay.cpp | M7；探针 `probe_partial_repaint` **119 checks / 0 failures**；脏区 1%~27%，Dragging 单帧 0.92ms → 0.34ms |
| R3-7 | 工具栏没有 `leaveEvent`，悬停高亮会残留 | P2 | 已修 | src/overlay/ToolbarWidget.cpp | **本次复核**：`leaveEvent` 在声明与实现里都在 |
| R3-8 | 死代码 / 遮蔽 / 未用参数 | P2 | 已修 | src/overlay/ToolbarWidget.cpp、src/overlay/SnapOverlay.cpp | **本次复核**：`isAction` 参数已删（0 处）；`-Wall -Wextra -Wshadow` 静态检查零警告 |
| R3-9 | 五条清理项 | P3 | 已修 | src/overlay/FloatingPanel.cpp、src/app/ShotApplication.cpp | **本次复核**六条逐条确认：`qDebug` 噪音、`setBaseImage()` 空矩形、放大镜字体缓存、`resources/` 与硬编码 `Alt+A`、重复 `setGeometry` |
| U-1 | 马赛克子面板去掉颜色行 | — | 已修 | src/overlay/ToolbarWidget.cpp | 证据 `build-review/panel_mosaic_sub.png`（宽 174px，仅尺寸方块） |
| U-2 | 文字尺寸/颜色变得可读 | — | 已修 | src/overlay/ToolbarWidget.cpp | 证据 `build-review/panel_text_sub.png`（三档数字 + 颜色点） |
| U-3 | 输入提示从「看不见」到「显眼」 | — | 已修 | src/overlay/TextInputWidget.cpp | 证据 `build-review/text_editor_empty.png`、`build-review/text_editor_typed.png` |

## 汇总

| 状态 | 条数 |
| --- | --- |
| 已修 | 38 |
| 部分已修 | 3（P1-2、P3、N-9） |
| 未修 | 0 |
| 不做 | 0 |
| 待人工确认 | 0 |
| **合计** | **41** |

**「部分已修」三条的剩余部分，就是当前全部未完成的审查项**：

1. **P1-2** —— `WinWindowDetector::windowRectAt()` 在 `edata.found == false` 时回落 `GetCursorPos()`，该路径下入参被忽略。
   只在「按屏幕名枚举不到显示器」时触发，正常路径不会走到。要修的话，正确做法是把入参直接按 DPR 换算后使用（或返回空矩形让调用方放弃），而不是去找光标。
2. **P3** —— 只剩「无测试、无 CI」。`-Wall -Wextra -Wshadow` 已在 2026-09-23 打开
   （审查报告自己写着「开启后这类问题下次会自己暴露」，所以它和「不再重复劳动」是同一件事）。
3. **N-9** —— ① `EnumData` 未整体清零；② 仍用 `GetClassNameA`/`GetWindowLong`（非 Unicode / 非 Ptr 变体）；③ `IScreenCapture.h` 仍 include `<QScreen>`（前向声明即可）。
   三条都是低危卫生问题，没有已知的功能影响。

另有两条**代码已完成、但效果只有人能确认**（不列入上表，因为它们的状态是「已修」）：
`N-1` 与 `R3-4` 都需要真双屏环境 —— 本机只有 1 块屏，探针也拿不到系统前台。

## 与本文有关的其它文档

- `ROADMAP.md` 第八节 —— 仍需**用户裁决**的事项（审美判断、产品取舍），那不是「未修」，是「等你定」。
- `SETTINGS.md` —— 设置项语义与默认值。
- `tools/verify_review_status.py` —— 本文的校验器，也是这套契约的执行者。
