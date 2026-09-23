# QShot 全能路线实施计划

> 本文回答一个问题：从「能用」走到「全能」（对标 PixPin / ShareX 的功能面），
> 具体要动哪些文件、按什么顺序、每步怎么验收。
>
> 结论先给：**先修地基（多屏 + 拆文件），再补出口（贴图 / 历史 / 序号高亮），最后打磨（图标 / 安装包）。**
>
> **本轮范围已确定（2026-09-21）**：滚动长截图与 OCR **暂缓**，两项的评估结论保留在第四节，需要时可随时重启。
> 因此本轮的交付边界是 **Phase 0 + Phase 1 + Phase 3**。

---

## 一、现状盘点

下表为 **M7（性能项收尾）完成后**的实测数字（M0 前：4108 行，`SnapOverlay.cpp` 914 行；M0 后：4369 行 / 683 行；M3 + 悬停样式后：5068 行 / 42 文件；M4 后：6017 行 / 46 文件；M6 图标 + 部署后：6227 行 / 46 文件；M6 清理后：6933 行 / 51 文件 / 755 行）。

| 项目 | 数值 |
| --- | --- |
| 源码总量 | 7210 行（`src/` 下 51 个 `.h`/`.cpp`） |
| 最大文件 | `overlay/SnapOverlay.cpp` **965 行**（M0 前 914 行；本轮 +210，几乎全是解释「为什么这个脏区是对的」的注释） |
| 次大 | `overlay/ToolbarWidget.cpp` 555 行、`annotation/AnnotationLayer.cpp` 406 行、`pin/PinWindow.cpp` 394 行、`app/ShotApplication.cpp` 380 行 |
| 已有能力 | 区域选区 / 整窗点选 / **八种标注**（矩形·椭圆·箭头·画笔·马赛克·文字·**序号**·**高亮**）/ 撤销 / 复制 / 另存 / **贴图**（含悬停提示样式）/ **历史记录**（托盘子菜单 + 缩略图）/ **真图标 + 自包含部署包** / **单实例保护** |
| 已有基础设施 | `Settings`（QSettings + 信号）、`Strings`（中英 70 条）、`PlatformFactory`（平台装配 + 判空，5 个实现）、`ImageExport`（复制 + 另存 + 静默保存）、`HistoryStore`（落盘 + 双预算淘汰）、`ToolbarIcons`（八工具字形，代码绘制） |
| 缺失能力 | 滚动长截图、OCR |

**已覆盖 2 / 6 个用户场景**（选中区标注导出、单击抓整窗）。

---

## 二、Phase 0：地基（必须先做，否则后面全是白工）

### 0.1 修多屏坐标空间混用（R3-4）—— ✅ 已完成

这不是「少个功能」，是**双屏用户的核心路径会直接出错**。单屏下原点恰好是 `(0,0)`，所以每一处都碰巧正确；一上副屏全部失效。

**根因**：`WinWindowDetector::windowRectAt()` 返回的是**桌面全局**逻辑坐标（`WinWindowDetector.cpp` 显式加了 `screen->geometry().x()/y()`），而 overlay 把它当**窗口局部**坐标用。更根本的是：构造函数收了 `screenGeometry` 参数，只用于 `setGeometry()` 之后就**丢掉了**，类里根本没有这个成员 —— 于是所有需要「屏幕原点」的地方都无处可查，只能靠「原点就是 (0,0)」的假设蒙过去。

**契约决定**：detector 继续返回全局坐标（它回答的是「桌面上这个点下面是哪个窗口」，全局才是正确契约）。**在 overlay 边界处归一化一次**，其余代码全部留在窗口局部空间。

实际改动清单：

| 文件 | 位置 | 改法 |
| --- | --- | --- |
| `SnapOverlay.h` | 66-82 | 新增成员 `QRect screenGeometry_;` 与 `QPoint globalOrigin() const` |
| `SnapOverlay.cpp` | 41 | 构造函数初始化列表补 `, screenGeometry_(screenGeometry)` |
| `SnapOverlay.cpp` | 143-145 | `globalOrigin()` = `mapToGlobal(QPoint(0,0))`，**故意不写 `screenGeometry_.topLeft()`**：万一 WM 把窗口摆到了别处，`mapToGlobal` 仍然对 |
| `SnapOverlay.cpp` | 65-66 | hover 定时器：`detector_->windowRectAt(p).translated(-globalOrigin())`，**唯一一次归一化**，其余代码继续用局部坐标 |
| `SnapOverlay.cpp` | 137-138 | `showToolbar()` 传全局坐标：`updatePosition(selectionRect_.normalized().translated(globalOrigin()), screenGeometry_)` |
| `SnapOverlay.cpp` | 109 | 文字标注落点：`mapFromGlobal(textInput_->pos()) - selectionRect_.normalized().topLeft()`，两边统一到局部空间 |
| `SnapOverlay.cpp` | 383 | `textInput_->startInput(mapToGlobal(event->pos()), ...)` |
| `ToolbarWidget.h/.cpp` | 31 / 314-326 | 形参改名 `globalSelectionRect` 并加注释；新增 `screenRect_` 成员；`if (targetY < 0)` → `if (targetY < screenRect.top())`（0 只在主屏才是上边界） |
| `TextInputWidget.h/.cpp` | 14-18 / 49,72 | 形参改名 `globalPos` 并加注释说明 `move()` 对顶层窗口读的是全局坐标 |
| `WinWindowDetector.cpp` | 66 / 102 | 顺带修掉 8 个**先前就存在**的 `-Wmissing-field-initializers`：`MONITORINFOEXW mi = { sizeof(...) };` → `mi = {}; mi.cbSize = sizeof(...);` |

**一个容易搞反的例外**：`SnapOverlay::paintEvent()` 里判断文字是否被裁到选区上方用的 `textY < 0`，**0 是对的**，因为那是 widget 自己的绘制空间（屏幕局部）。这与顶层窗口（`ToolbarWidget` / `TextInputWidget`）必须用全局坐标恰好相反。代码里已就地加注释区分。

**验收结果**：
1. ✅ 全树静态检查 `STATIC_EXIT=0`，零告警（含上面 8 个历史告警）；完整构建 `[100%] Built target qshot`，零告警。
2. ✅ `build-review/probe_multiscreen_coords.cpp`：**20 条断言全通过**。做法是一个 `OverlayStandIn : QWidget`（复刻 `SnapOverlay` 的 windowFlags 与 `globalOrigin()` 实现，**从不 show**），把屏幕原点合成成 `(1920,0,1920,1080)`（右屏）和 `(0,-1080,1920,1080)`（上方屏），断言新旧调用行为差异。**探针刻意调用真实的 `mapToGlobal` 而不是自己重算偏移** —— 否则探针和产品代码会各自漂移，验证就失去意义。
3. ✅ 回归：`probe_selection_geometry` 93 条断言仍全过；`probe_magnifier_render` 5 个用例仍 `differing=0 maxDelta=0`。
4. ⏳ **最终双屏行为必须由用户在双屏机器上人工确认**。本机只有 1 块屏，合成原点能抓住坐标空间错误，但抓不住「窗口管理器的实际摆放」这类只有真环境才暴露的问题。

### 0.2 拆 `SnapOverlay.cpp` —— ✅ 已完成（914 → 683 行）

加贴图 / 历史 / 序号高亮之前必须拆，否则这个文件会到 1500 行。

**按职责拆，不按行数拆**，且**不引入无谓抽象**（遵守 YAGNI）：

1. **`SelectionGeometry`（纯函数，无 Qt 窗口依赖）** —— `Handle` 命中测试、矩形归一化、八向 resize 数学、拖拽阈值、逻辑→物理坐标映射。放 `src/overlay/SelectionGeometry.h/.cpp`（79 + 116 行）。
   - 收益兑现：这是**纯计算**，已写 93 条真正的单元断言（`build-review/probe_selection_geometry.cpp`），全部通过。这是当前环境下唯一能确定性验证的一类逻辑。
2. **`FloatingPanel`** —— 放大镜布局与绘制 + 「点击输入」徽标共用的贴光标摆放逻辑 + 缓存的字体/度量。放 `src/overlay/FloatingPanel.h/.cpp`（86 + 228 行）。
   - 计划里叫 `MagnifierPanel`，实际改名 `FloatingPanel`：徽标和放大镜共用「避让屏幕边缘」的摆放规则（原本这段翻转逻辑在两处逐字重复），也共用字体度量缓存，合在一起比拆成两个单函数文件更合理。
   - 顺带修掉 R3-9 的性能问题：`magnifierLayout()` 每次 mouseMove 被调 2 次、paintEvent 再调 1 次，每次都构造 `QFontDatabase::systemFont()` + `QFontMetrics` → 现已缓存。
   - 顺带修掉一处潜在不一致：徽标原本**测量用 `QApplication::font()`、绘制用 `painter.font()`**，若样式表改过 widget 字体两者会不一致（文字溢出）。现在测量与绘制共用 `panel::uiFont()`。
3. **`SnapOverlay` 保留**：事件路由、状态机、工具栏/标注编排、导出。

**实际结果与偏差（诚实记录）**：
- 目标 450 行，实际 **683 行**。偏差来自事件处理本身：`mousePressEvent` / `mouseMoveEvent` / `mouseReleaseEvent` 三个加起来 245 行，是状态机的本体，搬不走。
- **未进一步拆 `SnapExporter`**（`physicalSelectionRect` / `copyToClipboard` / `saveToFile` 约 75 行）。理由：它现在只有一个消费者。等 M3 贴图或 M4 历史记录真的需要「拿到最终合成图」时再抽，那时才是有两个消费者的真实需求，而不是投机。
  - **M3 兑现了这条**：贴图确实需要同一张合成图，于是抽出了 `src/core/ImageExport.h/.cpp`（复制 + 另存），并新增 `SnapOverlay::renderSelectionImage()` 作为唯一的合成入口。`SnapOverlay.cpp` 因此从 683 降到 663 行。注意抽出来的**不是** `SnapExporter` 而是 `ImageExport`：真正重复的是「复制到剪贴板」与「弹对话框另存」，裁剪与合成留在 overlay 里 —— 只有它拥有背景图、选区和 dpr。
- **0.2 不是纯重构**：写单元测试时发现 `dragRect` 有一个真缺陷并修掉了，见下。

#### 顺带修掉的真缺陷：拖拽方向导致选区差 1px

原实现是 `QRect(startPos_, event->pos()).normalized()`。`QRect(QPoint, QPoint)` 把第二个点当**含端点**的角，而 `normalized()` 对反向矩形的处理会各边缩 1px。实测：

| 手势 | 原实现结果 |
| --- | --- |
| 从 (10,10) 拖到 (50,40)（右下） | `41 x 31` |
| 从 (50,40) 拖到 (10,10)（左上） | `39 x 29` |

同一个手势，因方向不同得到不同大小的选区。已改为基于 `qMin/qMax` 构造，四向一致，并加了四向断言锁住。

**顺序**：先做 0.2 再做 0.1 更好 —— 拆完 `SelectionGeometry` 后坐标空间只有少数几个边界点要改，比在 914 行里找 7 处容易。

### 0.3 多屏焦点路由 —— ✅ 已完成（原假设「面板抢焦点」经查已过期）

**原假设**：`showToolbar()` 里的 `toolbar_->show()`（`Qt::Tool` 子窗口）本身就清空 `QGuiApplication::focusWindow()`，导致 Esc / Enter / Ctrl+Z 全丢。

**复核结果（2026-09-21）**：这个假设**已经过期**。`Qt::WindowDoesNotAcceptFocus` 早在设置那一轮（commit `aa16bf5`）就加到了 `ToolbarWidget` 与 `ToolbarWidget::SubPanelWidget` 上，而它确实生效了。新探针 `build-review/probe_noactivate.cpp` 直接读原生窗口的 `GWL_EXSTYLE`（`windowFlags()` 只是请求，平台插件可以不理；扩展样式才是事实）：

| 窗口类 | `GWL_EXSTYLE` | `WS_EX_NOACTIVATE` | 结论 |
| --- | --- | --- | --- |
| `qshot::SnapOverlay` | `0x00000088` | 否 | 可激活 ✓ |
| `qshot::ToolbarWidget` ×2 | `0x08080088` | **是** | 点它/`show()` 都不会被激活 ✓ |
| `QWidget`（= 私有嵌套的 `SubPanelWidget` ×2） | `0x08080088` | **是** | 同上 ✓ |
| `qshot::TextInputWidget` ×2 | `0x00080088` | 否 | 用户要打字，必须可激活 ✓ |

`WS_EX_NOACTIVATE` 意味着 Windows 在点击和 `ShowWindow` 时都**不会**激活该窗口 → 这两个面板**结构上不可能**抢走 overlay 的键盘焦点，与 Qt 内部的焦点记账无关。

同时确认候选 **A 也已覆盖**：`showToolbar()` / `hideToolbar()` / `textInput_` 的 `editingFinished`+`canceled` / `toolbar_` 的 `colorChanged`+`sizeSettingChanged` / `handleToolSelection()` 全部调用了 `reclaimKeyboardFocus()`；`ToolbarWidget::handleToolClick()` 里的 `showSubPanel()` / `hideSubPanel()` 之后紧跟 `emit toolSelected` → `handleToolSelection()`，也在覆盖内。

**探针的写法**：不手写一份窗口清单，而是对进程内**所有**顶层窗口施加一条通用不变量 ——

> 除「用户要往里打字的类」（`SnapOverlay` / `TextInputWidget`）以外，每个顶层窗口都必须带 `WS_EX_NOACTIVATE`。

这样以后新增任何面板都自动被覆盖，不会漏。分类靠 `metaObject()->className()`，只对带 `Q_OBJECT` 的类有效；`SubPanelWidget` 没有 `Q_OBJECT`，于是报 `QWidget` 落进「展示型」桶 —— 结论恰好正确，但这是巧合，探针注释里已注明。结果：**9 checks, 0 failures**。

**因此真正剩下的只有一件事**：`ShotApplication::onCaptureTriggered()` 的循环里，**只有最后一个 overlay 拿到 `activateWindow()`**（`ShotApplication.cpp:196-220`）。多屏时只有一块屏的 overlay 拥有键盘。注意 Esc 其实已经是全局的 —— 它会 `close()` 当前 overlay，再经 `closed` 信号关掉其余全部；受影响的是 Ctrl+Z / Enter 落在哪块屏上。

**已采用 D（2026-09-21）**：按光标位置决定激活哪块 overlay —— `QCursor::pos()` 落在哪个 `screen->geometry()` 里就激活哪个（光标所在屏就是用户正在看的屏），光标不在任何屏上时回退到第一块。原实现激活的是循环里最后访问到的那块，在多屏下就是「最后一块屏」，纯属偶然。改动在 `ShotApplication::onCaptureTriggered()`，编译与冒烟通过。

**验收**：必须**人工交互确认**（探针拿不到系统前台，见第七节）。机制层面已由 `probe_noactivate.cpp` 锁住（9 checks / 0 failures）。⚠️ **本机只有 1 块屏，D 的实际多屏效果无法在此验证。**

---

## 三、Phase 1：出口能力（决定「截完能拿它干什么」）

关键判断：截图工具的泛用性不取决于「能不能截」，而取决于**截完之后能拿它干什么**。现在出口只有剪贴板与存盘两条。

### 1.1 贴图（Pin）—— ✅ 已完成

把截好的图钉在屏幕上做参考对照。这是六个缺口里**工作量最小、复用最多**的一个。

**实际落地**：

| 文件 | 行数 | 内容 |
| --- | --- | --- |
| `src/pin/PinWindow.h/.cpp` | 139 + 362 | 无边框、置顶、`Qt::Tool`（不进任务栏）的贴图窗口 |
| `src/core/ImageExport.h/.cpp` | 32 + 77 | `copyImageToClipboard()` / `saveImageWithDialog()` —— 复制与另存从 `SnapOverlay` 里抽出来，现在有两个消费者（overlay 与 pin） |

**交互**（已实现）：拖拽移动（`OpenHand`/`ClosedHand` 光标）· 滚轮缩放 25%–400%，**以光标为锚点** · `Alt+滚轮` 调透明度（0.2–1.0，每档 0.1）· 双击关闭 · `Esc` 关闭 · `Ctrl+C` 复制 · `Ctrl+S` 另存 · 右键菜单（复制 / 另存为… / 关闭）· **悬停显示内阴影 + 右上角关闭按钮**（见 1.1.1）。

**入口**（已实现）：
- 工具栏「贴图」按钮，放在「保存」与「取消」之间 —— 工具栏宽度 376 → **412**，5 个动作按钮的中英文案都已断言能塞进 32px 按钮；
- overlay 内 `Ctrl+T`（**加了 Ctrl 修饰键**，因为文字工具武装后任何无修饰键都可能是输入）；
- ⏳ 托盘菜单「贴最近一张」**未做** —— 它需要「最近一张」这个概念，而那属于 M4 历史记录，提前做会造一个只能存活一步的临时状态。等 M4 落地后再接。

**与原计划的偏差（诚实记录）**：

1. **没有让 `ShotApplication` 持有 `QList<QPointer<PinWindow>>`**。`PinWindow` 以 `WA_DeleteOnClose` 无父创建，自己管生命周期；`main.cpp` 已有 `setQuitOnLastWindowClosed(false)`，所以「一个窗口都没有」本来就是托盘应用的正常状态，退出时也不需要谁去清理。多一个列表只会多一处需要保持同步的状态。
2. **新增设置项全部没做**（默认不透明度 / 细边框 / 是否置顶）。目前是固定值。理由：YAGNI —— 没有用户反馈说需要，加了就得同时维护设置对话框、持久化与即时生效三条链路。
3. **`renderToImage()` 这个名字在计划里是错的**，实际叫 `SnapOverlay::renderSelectionImage()`。原来 `physicalSelectionRect()` + `copy()` + `annotationLayer_.renderToImage()` 这三行在 `copyToClipboard()` 和 `saveToFile()` 里**各抄了一遍**，现在收敛成一个方法。

**一个被探针抓到的真缺陷（值得记）**：边框原本用 1px 描边笔画 `drawRect`。**描边笔骑在路径上**，在 DPR 2 时有一半落进图像，把内容最后一列染成 `#FF0000A5` —— 正好是 255 × (1 − 90/255) = 165，即 alpha 90 的黑覆盖在纯蓝上。已改成**四条填充色带**。同时把边框宽度从 1 改成 **2 逻辑像素**：DPR 1.5 时 1px 边框会让内容起点落在设备坐标 1.5 上，整幅图被平移半像素并重采样；2px 边框在 DPR 1 / 1.5 / 2 下都落在整数设备像素上。

**验收结果**：
- ✅ 全树静态检查（18 个 `.cpp`）零告警；完整构建零告警；冒烟 `SMOKE_EXIT=124`（稳定运行），无残留进程。
- ✅ `build-review/probe_pin_geometry.cpp`：**28 条断言**。锚点缩放的纯算术 —— 含「锚点下的图像坐标缩放前后不变」（独立复算意图，不只对公式）、**平移不变性**（整体平移 (1920,0)/(0,−1080)/(−800,600) 结果同步平移，用来排除「假设原点为 0,0」）、12 次放大 + 12 次缩小后**精确回到原位**。
- ✅ `build-review/render_pin.cpp`：**51 条断言**。DPR 1 / 1.5 / 2 三档，源图是 1px 红蓝条纹 —— **内容行只有 2 种颜色**才算 1:1，任何重采样都会混出中间色，这比目视可靠得多。另含滚轮事件走真实路径（缩放、尺寸联动、上下限夹紧、`Alt+滚轮` 不改缩放只改透明度）、5 个动作文案在**中英两种语言**下都塞得进 32px 按钮。
- ✅ `build-review/probe_pin_wiring.cpp`：**13 条断言**。驱动真实事件链：在 overlay 上拖出选区 → `Ctrl+T` → 断言发出的图**逐像素**等于背景的对应裁剪（背景用坐标编码图，任一像素错位都能查出来），且放置矩形的左上角经 overlay 自己的 `mapFromGlobal()` 映回选区原点。
- ✅ 回归：`probe_selection_geometry` 93/0、`probe_magnifier_render` 等价、`probe_multiscreen_coords` 20/0、`probe_noactivate` 9/0 全部照旧。
- ⏳ **拖拽、滚轮、右键菜单、双击关闭的实际手感必须人工确认**（探针只能验证事件处理与渲染结果，验证不了「拖起来跟手不跟手」）。

#### 1.1.1 悬停提示样式 —— ✅ 已完成（用户反馈驱动的补充）

**问题**（用户原话）：「贴图我觉得可以加入一些鼠标移入的样式，例如内阴影+右上方关闭图标，**不然截最大化窗口时用户都不知道底下是一张图片**。」

这个反馈点中了一个真实的可用性死角：贴图最常见的用法就是把一张**最大化窗口**的截图钉在屏幕上，此时贴图与它底下的窗口逐像素相同、且覆盖整个屏幕 —— 屏幕上没有任何东西能告诉用户「你现在看到的是一张静止的图片，不是一个能点的窗口」，更不用说怎么把它关掉。原有设计里唯一的提示是 2px 边框变色，而边框在**不悬停时**是 `#818181` 中灰（见下），识别度太低。

**做法**（`PinWindow.h/.cpp`）：

| 元素 | 参数 | 说明 |
| --- | --- | --- |
| 内阴影 | 深 14 逻辑像素，边缘 alpha 150，**二次衰减** | 同心 1px 色带逐层向内，共 14 条。边缘 150 → 129 → 110 → 93 → … → 1 |
| 关闭按钮 | 右上角 18×18，距内容边 3px | 深色圆盘（静止 alpha 140 / 悬停 205）+ 白色圆头 X |

**三个刻意的设计决定**：

1. **阴影只画在内容内侧的窄带上，且只画一次**。色带是嵌套的「口」字形，横向条与纵向条**互不重叠**（纵向条跳过横向条已占的首末行）。两条半透明填充叠在同一像素上会让四角比四边暗一倍 —— 这是手写内阴影最典型的破绽，探针专门断言了「四角与四边等暗」。
2. **衰减而不是一个实心内嵌矩形**。实心矩形读起来是「第二条粗边框」，而阴影要读起来像阴影。
3. **贴图太小就不画按钮**。`closeButtonRect()` 在放不下时返回**空矩形**（需要 18 + 2×3 = 24 逻辑像素），此时点击**穿透到拖拽**。返回一个被裁切的矩形会画出一个被压扁的 X，看起来像渲染 bug；而在那么小的贴图上，边框本身已经够当提示了。

**顺带修掉的真缺陷**：`ShotApplication::onPinRequested()` 的注释写着「It is created with WA_DeleteOnClose」，但 `PinWindow` 的构造函数里**从来没有设过这个属性**，也没有别的地方设 —— 也就是说**每关掉一张贴图就漏掉一个窗口和它持有的 QImage**（全屏截图约 16MB）。加了关闭按钮之后 `close()` 从「Esc / 双击 / 右键菜单」变成**鼠标一键可达的主路径**，漏的概率大幅上升，所以这次一并补上，并把属性放在构造函数里当成不变量（`close()` 现在有四个入口，靠调用方记住是不可能的）。

> 副作用：**不能对栈上的 `PinWindow` 调用 `close()`** —— `WA_DeleteOnClose` 会延迟 `delete this`，而对象在栈上。生产代码全部堆分配；探针里凡是要测 `close()` 的也一律堆分配 + `QPointer` 观察，并用 `pin.isNull()` 断言「窗口真被回收了，而不只是隐藏」。

**验收结果**：
- ✅ `build-review/render_pin_hover.cpp`：**40 条断言**，全绿。
  - **穷举式**断言「悬停样式一点都没碰到画面」：idle 与 hover 两次渲染逐像素比对，内容区**去掉四周 14px 窄带与按钮区之后**的 **27903** 个像素必须**完全相同**（DPR 1.5 下 27708 个设备像素，同样 0 差异）。这是能抓住「色带循环多跑一圈」的唯一手段，肉眼看不出。
  - 把混合反解出来验证衰减形状：源图是已知的纯色，于是 `alpha = 255 × (1 − 观测值/源值)`。实测 149.8 / 128.6 / 110.5 / 93.5 / … / 7.4 / 3.2 / 1.1，逐带单调不增，第 14 带恰好为 0。
  - 结构性不变量：按钮之外**没有任何一个像素比源图更亮**（内阴影只可能压暗，出现亮点就说明画错了）。
  - 按钮：不悬停时那个位置**就是源图本身**（证明按钮只在悬停时存在）· 圆盘 alpha 141.3 → 光标移上去 206.1 · 光标形状 `OpenHand` ↔ `PointingHand` 双向切换 · 移开后像素与静止态**完全相同**。
  - 交互：按在按钮上 → 窗口关闭**且被回收**；按在中间 → 不关闭且**按位移精确拖拽**；20×20 的小贴图没有按钮、那个角落是拖拽。
  - `pin_chrome_before_after.png`：同一张贴图的 idle / hover 并排图，供人工判断「一眼能不能看出是图片」。
- ✅ 全树静态检查（18 个 `.cpp`）零告警；完整构建零告警；冒烟 `SMOKE_EXIT=124`。
- ✅ 回归：`probe_selection_geometry` 93/0、`probe_pin_geometry` 28/0、`render_pin` 51/0、`probe_pin_wiring` 13/0、`probe_multiscreen_coords` 20/0、`probe_noactivate` 9/0、`probe_magnifier_render` 等价。
- ⏳ **阴影深浅与按钮大小是审美判断，需要人工看一眼 `pin_chrome_before_after.png` 再定**（当前 14px / alpha 150 / 18px 是拍的值）。

### 1.2 历史记录 —— ✅ 已完成

- **新增 `src/core/HistoryStore.h/.cpp`**（`qshot::HistoryStore`）：环形缓冲 + 落盘，单例 `instance()`，同时开放构造函数注入目录与字节预算供测试。
  - 条目：`{int id, QDateTime capturedAt, QSize size, QString filePath, QImage thumbnail, qint64 bytes}`。
  - **落盘策略**：全图写 PNG 到 `QStandardPaths::AppDataLocation/history/<id>.png`，**缩略图也落盘**（`<id>.t.png`）。原计划「内存只留缩略图」照做，但缩略图不落盘会导致启动时要解码 20 张全图才能出菜单 —— 那是几秒的卡顿。索引里存缩略图路径，启动只读小图。
  - 上限：条数（默认 20）+ 总字节数（默认 200MB）双约束，超限删最旧，**含删文件**。字节预算**永不驱逐最新一条**（否则一张超大图会让历史直接空掉）。
  - 索引文件 `history/index.json`，启动时校验文件存在性，丢弃失效条目并扫掉孤儿文件。
  - **索引写入用 `QSaveFile`**（临时文件 + rename），写一半崩溃不会把索引截断成空。
  - **索引损坏不删图**：解析失败就当作空历史重建索引，绝不动磁盘上的截图 —— 用户的历史不该因为一个 JSON 语法错误消失。
- **UI**：`src/ui/HistoryMenu.h/.cpp`，托盘菜单加「历史记录」子菜单，每项带缩略图 `QIcon`。
  - 每条是**子菜单**（不是「动作挂 submenu」—— Qt 会把那种动作渲染成只有子菜单、本体不可触发），标题 `HH:mm:ss  W×H`（非当天改用日期），内含 复制到剪贴板 / 另存为… / 贴到屏幕上 / 删除。
  - **菜单用 `aboutToShow` 重建**，不维护平行副本：历史会被 overlay 在菜单关着的时候改动。
  - **动作按条目 `id` 闭包捕获，不按行号**。这是实测抓到的真 bug：行号在删掉一条之后会整体前移，「先删一条再复制另一条」是极普通的操作序列，按行号就会静默复制错条目。回归测试见 `probe_history_menu.cpp` 第 [6] 节。
  - 「清空历史」用 `QMessageBox::question(..., No)` —— 破坏性按钮不能是回车默认命中的那个。
- **接入点**：`SnapOverlay::copyToClipboard()` 与 `saveToFile()` 成功后各写一条。存盘那条**直接复用已写好的文件**（`ImageExport::saveImageWithDialog` 返回值由 `bool` 改为 `QString`），不再重新编码一遍 JPEG。
  - **用户另存的文件是「拷贝进来」而不是「引用」**，且是**文件级拷贝不是重新编码**：前者保证用户清理「图片」文件夹不会把历史清空，后者保证 JPEG 的既有有损质量不被二次损失。
- **新增设置**：是否启用历史（`history/enabled`）、条数上限（`history/limit`，1~200，默认 20）、是否在复制后弹托盘气泡（`history/notifyOnCopy`）。
  - `setHistoryLimit()` **故意不触发驱逐**：设置对话框的「取消」要是真取消，超限的收缩留到下次 `add()`/`load()` 时做。
- **新增文案 15 条**（47 → 62）：`TrayHistory*` / `History*` / `GroupHistory` / `HistoryEnabledLabel` / `HistoryLimitLabel` / `HistoryNotifyLabel`。
- **一个真缺陷顺带修掉**：`PinWindow` 的注释声称「靠 `WA_DeleteOnClose` 自管生命周期」，但**那个属性从来没设过** —— 每关一个贴图就泄漏一个窗口 + 一张 `QImage`（全屏截图约 16MB）。M3 补悬停样式时发现并补上。

**验证结果**：

- ✅ 全树静态检查（20 个 `.cpp`）零告警；完整构建零告警（`[100%] Built target qshot`）；冒烟存活 6 秒无崩溃。
- ✅ **全量回归一键跑通**：`bash build-review/build_probes.sh --run` → 13 个目标全部构建成功（**零告警**）、全部 `exit=0`，合计 **363 条断言 0 失败**。
  - `probe_selection_geometry` 93/0、`probe_pin_geometry` 28/0、`probe_pin_wiring` 13/0、`probe_multiscreen_coords` 20/0、`probe_noactivate` 9/0、`render_pin` 51/0、`render_pin_hover` 40/0、`probe_magnifier_render` 5 用例全 `differing=0 maxDelta=0`。
  - `probe_history_store` **81/0**：13 节，覆盖空库、add 落盘、另存文件是拷贝而非引用、条数超限**连文件一起删**、字节预算驱逐但**永不驱逐最新一条**、索引往返（缩略图**逐像素一致**）、失效条目丢弃、孤儿清扫、**索引损坏时截图仍在**、禁用态不写任何文件、缩略图长边 160 且保比、`removeAt`/`clear`、`changed()` 发出时磁盘已一致。
  - `probe_history_menu` **28/0**：7 节，含第 [6] 节的 id-vs-行号回归。
- ✅ 目视：`history_menu.png`、`history_menu_entry.png`；设置对话框四种语言/格式组合下「历史记录」组几何一致（`x=11 y=447 w=438 h=122`）。
- ✅ `HistoryStore::indexOfId()` 是死代码，已删（YAGNI）；`probe_pin_geometry.cpp` 一个未使用的 `checkEq`、`render_pin.cpp` 一处 `%d` 打 `qsizetype` 也一并清掉 —— 全量构建现在**零告警**。
- ⏳ **三个默认值（20 条 / 200MB / 复制后提示）是拍的值，待人工确认**。

### 1.3 序号 / 高亮标注 —— ✅ 已完成

- `AnnotationType` 加 `Number`、`Highlight` 两个枚举值；`Annotation` 加 `int number` 与 `int badgeDiameter`。
- **序号**：点击落一个自动递增的圆形徽标，按住可微调位置，松手提交。**计数器每次重新画选区时重置**（存在 `SnapOverlay::nextNumber_`，不是全局）。
- **高亮**：荧光笔式自由涂抹（复用 `points` 机制，只改绘制）。
- 两个工具各有自己的尺寸档与出厂默认色，见下面「偏差」一节。

#### 先纠正一个前提：工具栏宽度**不是**问题

原计划把「6 个工具 → 8 个，小选区下工具栏会溢出屏幕」列为**必须先解决的前置约束**。实测下来这个约束不存在，而且估算本身也是错的：

- 原估「4 个动作按钮」，实际 M3 已经加了 `Pin`，是 **5 个**。
- 工具栏是**顶层窗口**，不隶属于选区。`updatePosition()` 把它**钳制在屏幕矩形内**，与选区大小无关。所以真正的问题从来不是「装不装得进选区」，而是「装不装得进屏幕」。
- 实测（`probe_toolbar_layout.cpp`，13 条断言）：6 个工具时宽 **412px**，8 个工具时 **484px**。本机逻辑屏宽 1707px，**余量 1223px**；换到 1366px 的小屏仍有 **882px** 余量。

所以**没有**做两排布局，也没有做「更多」溢出菜单 —— 那会为了一个不存在的问题改动所有人的工具栏。取而代之，把这条不变量写成断言：**任何选区下，摆放后的工具栏都必须完全落在给定屏幕矩形内**（含四角 1×1 选区、以及原点是 `(1920,0)` 的第二块屏 —— 后者正是「拿 0 当屏幕边缘」会出错的地方）。

#### 偏差 1：高亮只做自由涂抹，没做「半透明矩形」

原计划写的是「半透明黄色矩形 + 荧光笔式自由涂抹」。只实现了后者：矩形高亮与现有的矩形工具几乎是同一件事（只差一个填充），而自由涂抹没有任何现有工具能替代，是真正缺失的能力。若日后要补矩形高亮，正确的做法是给矩形工具加一个「填充」开关，而不是再开一个工具。

#### 偏差 2：高亮的端点用 `FlatCap` 而不是 `SquareCap`

第一版用了 `Qt::SquareCap`，探针立刻抓到问题：**方帽会在两端各多伸出半个笔宽**（默认 18px 宽即每端 9px），沿着一行文字拖过去，会在左右两侧各糊 9px 到页边距上 —— 而「对齐一行文字」恰恰是这个工具的主要用法。改成 `FlatCap` 后笔画严格止于拖拽的两端。

代价是**零长度线段在 `FlatCap` 下没有面积**，单击就什么都画不出来，工具看起来是坏的。所以单点的情况单独处理，画一个半径为半笔宽的圆点（这也正是真实荧光笔点在纸上的样子）。

#### 偏差 3：颜色存储改用 `HexArgb`

高亮的全部身份就是它的 alpha（默认 110）。原来 `Settings::setToolSettings()` 存的是 `color.name()`，即 `HexRgb`，**会把 alpha 丢掉** —— 存一次、重启一次，荧光笔就变成不透明的黄色了。改存 `QColor::HexArgb`。`QColor::fromString()` 两种格式都认，所以老的配置项仍然读得进来。

#### 偏差 4：出厂默认色按工具区分

`ToolSettings` 原来只有一个共享默认（橙色）。荧光笔需要半透明黄、序号徽标需要实心红，因此新增 `defaultToolSettings(AnnotationType)`，让首跑默认、`restoreDefaults()`、「配置只写了一半」三条路径由**同一个函数**产出，不会各自漂移。顺带修掉一处：关掉「记住工具设置」时 `toolSettings()` 原本返回裸 `ToolSettings()`（橙色），现在返回该工具自己的默认色。

#### 一个真 bug：撤销会永久留空号

序号在**按下时**就占用（这样拖动过程中徽标能显示编号），在**提交时**才消费。撤销如果只把标注从图层里删掉，`nextNumber_` 会继续往前跑 —— 撤销掉 3 号再落一个，得到的是 4 号，中间那个空号用户再也填不上。因此 `AnnotationLayer::undo()` 改为**返回被删掉的类型**，`SnapOverlay::handleUndo()` 见到 `Number` 就把号还回去。右键取消落点则天然不消费（提交才消费），不额外处理。

#### 验证结果

- ✅ 全树静态检查（20 个 `.cpp`）零告警；完整构建零告警；冒烟通过。
- ✅ `probe_toolbar_layout` **13/0**：8 工具下宽 484px，本机与 1366px 屏均放得下；四角 1×1 选区、第二块屏（原点 1920,0）两端都完全落在屏内；有空间时在选区下方，没空间时翻到上方；确认工具栏是独立窗口且允许比选区宽。
- ✅ `probe_annotation_types` **31/0**：
  - 高亮**确为半透明**：对纯蓝底反解混合，红通道还原 alpha 0.4308、蓝通道 0.4307（标称 0.4314），两通道一致 ⇒ 是标准 alpha 混合而非不透明覆盖。
  - 覆盖范围精确：中心覆盖、±12px（半宽 9）之外不覆盖、±7px 覆盖；`FlatCap` 端点处**恰好**不多画（179 覆盖 / 180 不覆盖）。
  - 单击留下半径 9 的圆点。
  - 徽标是**圆**不是方：包围盒角上 `(11,11)` 仍是背景，同距离的轴向 `(11,0)` 在圆内。
  - 数字确实来自 `number`：1 与 2 两张渲染有 139 个像素不同。
  - 白字存在（19 个近白像素）且被深色包围（41 个暗像素）⇒ 在任何底色上都可读。
  - `badgeDiameter` 20/38 实测跨度 22/42px。
- ✅ 目视 `toolbar_eight_tools.png`、`toolbar_eight_tools_2x.png`：8 个图标笔画粗细一致；序号图标读作「①」而不是电源符号；荧光笔图标的黄色斜带清楚。
- ⏳ **两个新工具的图标形状、荧光笔的默认色与 alpha 110、徽标默认红色，都是审美判断，需要人工看一眼再定。**（M6 收尾时这八个字形被抽到 `ToolbarIcons` 重画过，见 3.4 —— 现在该看的是 `toolbar_icons_zoom.png` 而不是上面这两张 1×/2× 图。）

---

## 四、Phase 2：高级采集 —— **已评估，本轮暂缓**

> **决策（2026-09-21）**：滚动长截图与 OCR 均推迟，不在本轮实现。
> 本节保留完整评估结论与技术路线，避免日后重启时重新调研。
> 两项的共同特点是**成本高、且都有本环境无法确定性验证的部分**（滚动截图的拼接质量、OCR 的异步桥），
> 在 Phase 0 地基与 Phase 1 出口能力完成前做它们，收益远低于风险。

### 2.1 滚动长截图（暂缓）

**新增 `src/capture/ScrollCapturer.h/.cpp`。**

算法：选区固定 → 反复「截一帧 → 注入滚轮 → 再截一帧 → 求重叠 → 追加」。
- 滚轮注入：Win32 `SendInput` + `MOUSEEVENTF_WHEEL`。
- 拼接：从已累积图像的**底部取一条模板带**，在**新帧**里做垂直方向 SAD/归一化互相关搜索，得到 `dy` 偏移，追加 `dy` 之后的新增行。
- 停止：用户 `Esc` / 点「停止」；或连续 2 帧 `dy == 0`（到底了）。

**诚实的风险说明**：固定页眉/页脚、懒加载列表、视差滚动都会让模板匹配错位并产生鬼影。这是滚动截图**固有**的难点（ShareX 也会翻车）。所以：
- 建议**先做「纯追加 + 无匹配时停止」的最小版本**，宁可少截也不要错位；
- 界面上明确提示「请勿在含固定页眉的区域使用」。

**这是所有候选功能里最重、最不确定的一项，已确认暂缓。** 若日后重启，建议先只做「纯追加 + 无匹配即停止」的最小版本。

### 2.2 OCR（暂缓）

**先更正我此前的一个错误说法**：我早前提过「Windows 内置 `Windows.Media.Ocr`，符合优先复用原生能力」。实测后**这条对本项目不成立**：

1. **MinGW 无法编译 WinRT OCR**。在 MinGW 头文件树里穷举确认：
   - 有：`roapi.h`、`hstring.h`、`inspectable.h`、`asyncinfo.h`、`activation.h`、`windows.foundation.h`、`windows.media.h`（仅 5 个接口）、`windows.globalization.h`、`wrl/`；
   - **没有**：`windows.media.ocr.h`、`windows.graphics.imaging.h`、`winrt/`（C++/WinRT）；
   - 全树 grep `OcrEngine` / `SoftwareBitmap`：**零命中**。
2. **PowerShell 兜底路线不干净**。实测 `[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType=WindowsRuntime]` 类型可解析，`TryCreateFromUserProfileLanguages()` 成功，语言 `简体中文(中国大陆)`，可用 `en-US, zh-Hans-CN`。**但**异步桥（`AsTask`）与位图转换需要 `Add-Type -AssemblyName System.Runtime.WindowsRuntime` 或 `Reflection.Assembly` 加载 —— 这两类 API **在本机被安全策略直接拦截**（连命令里出现这些字面量都会被拦）。也就是说：这条路线在**受管控环境里会直接失效**，且即便能用也要「起进程 → 落临时 PNG → 解析 stdout → 1~3 秒延迟」，还有闪控制台窗口的问题。

**结论**：
- **已决策推迟（2026-09-21）。** 它在这六个缺口里「价值/成本」最低（通用截图工具用户极少用 OCR），而唯一「零依赖 + 原生」的路线已证明不可用。
- 若日后重启，**用 Tesseract**（`libtesseract` + `leptonica`，MinGW 可编，或预编译 DLL），**不要用 PowerShell**。代价是引入约 30–60MB 语言数据 + 构建依赖，与「优先复用原生能力」的规则冲突，届时需要重新确认。

---

## 五、Phase 3：打磨

| 项 | 内容 |
| --- | --- |
| 托盘图标 | ✅ 已完成（见 3.1）—— 不再是 `pixmap.fill(Qt::blue)` |
| 部署脚本 | ✅ 已完成（见 3.2）—— 暂存目录经闭环校验 + 运行期验证 |
| 安装包 | ⚠️ 脚本已写，**从未编译过**（见 3.3）—— 本机没有 Inno Setup |
| 工具栏图标 | ✅ 已完成（见 3.4）—— 抽出共享图标集 `src/overlay/ToolbarIcons.cpp`，八工具图标按选中态着色；顺手修掉两个真读错的形状 |
| 设置补项 | ✅ 已完成（见 3.5）—— 「保存时不再询问」「复制时同时存一份」两个开关，**均默认关闭**；另抽出 `ImageExport` 的共享编码入口 |
| 清理 | ✅ 已完成（见 3.6）—— N-7 马赛克增量（**探针抓到一个既有 bug**）+ P1-4 单实例保护 + P1-3/6/8 复核 |
| 代码签名 | ⏳ **未做**，且本轮也做不了 —— 没有证书 |

### 3.1 真图标集 —— ✅ 已完成

原先托盘图标是 `pixmap.fill(Qt::blue)` 的纯蓝方块，`resources/` 是空目录、CMake 里也没有资源系统。

- **生成器当工具提交，不当构建步骤**（`tools/make_icon.cpp` + `tools/make_icon.sh`）：图标是构建**输入**，应该被刻意重新生成，而不是每次编译都跑一遍；留着源码意味着以后能改，而不是一个谁也编辑不了的二进制。
- **造型是取景框**（圆角蓝渐变底板 + 四段白色 L 形角标），刻意不用相机或「带把手的矩形」：16px 是托盘实际使用的尺寸，笔画多于几条就糊成一团，而角标是四块粗形状、彼此留白多，能活下来。
- **几何全部按比例**：`inset = s*0.195`、`arm = s*0.225`、`w = max(1, s*0.075)`。臂长/笔宽 ≈ 3:1 —— 2:1 时角标读成四个胖块而不是一个框，在放大预览里一眼可见，这正是那个预览存在的理由。笔宽有 1 设备像素的下限，低于此抗锯齿会把它抹成灰雾。
- **尺寸集** 16/20/24/32/48/64/128/256，容器是 **PNG-in-ICO**（手写小端序列化）。256 那一项若用裸 DIB 单独就约 256KB，比文件其余部分加起来还大；PNG 从 Vista 起合法。256 的宽高字段按格式约定写 `0`，写错会让这一项无法加载。
- **资源接线**：`resources/resources.qrc` 挂进 `add_executable`（AUTORCC 已开），`ShotApplication` 的托盘图标与 `main.cpp` 的 `QApplication::setWindowIcon` 都换成 `:/icons/qshot.ico`。

**验证**（两半，互不重叠）：

- `tools/verify_icon.py` —— **完全不依赖 Qt**，用 `struct` 解析 ICO 容器：头部、8 个 16 字节目录项、`256-as-0` 约定、每段 payload 的 PNG 魔数与 IHDR 尺寸、偏移、总大小（12143 字节）。它证明的是「文件格式对」。
- `probe_icon_resource` 29/0 —— 从**编译进二进制**的资源表里读回来验像素。它证明的是「构建嵌进去了、Qt 解析得了、出来的是取景框而不是蓝方块」。含 `availableSizes()` 八个尺寸齐全、256px 构图占比（白 12.0% / 蓝 72.9% / 透明 15.1%，圆角透明说明 alpha 没在 PNG-in-ICO 编码里被压平成黑）、以及 `.ico` 的 256 项与独立 `.png` **逐字节相同**。

> 两条失败过的断言，都是探针自己的期望值写错（产品无 bug）：① `QIcon::pixmap(16,16)` 的参数是**逻辑**尺寸，Qt 6 里 `QPixmap::size()` 报的是**设备**像素（逻辑尺寸走 `deviceIndependentSize()`），dpr 1.5 下要 16 会得到 24×24；② 拿 `QIcon` 渲染出的像素和 PNG 文件比，差 921 个 —— Windows 上 `QPixmap` 内部是**预乘 alpha**，而 `QImage::pixel()` 取的是存储值，所以每个抗锯齿边缘（alpha<255）RGB 都对不上。这不是编码 bug，是**比较方式**错了；改成字节级比较才是「同一次渲染」真正该断言的东西。

⏳ **图标造型本身是审美判断**，请看一眼 `build-review/icon_preview.png`（八个尺寸统一放大到 128px 高的最近邻拼版）。

### 3.2 部署 —— ✅ 已完成

`tools/deploy.sh`：Release 配置 → 暂存 → `windeployqt` → 复制翻译 → 裁剪 → 闭环校验。

**过程中发现一个真实部署缺陷。** `windeployqt --translations zh_CN` 只部署了 **99 字节**的 `qt_zh_CN.qm`（一个仅仅列出其他目录的元目录），**没有**部署 147KB 的 `qtbase_zh_CN.qm` —— 而 `Strings.cpp` 正是按这个名字加载的。后果是静默的：应用照常启动、看起来正常，但每一句 Qt 自带文案（`QMessageBox` 按钮、行编辑右键菜单）都退回英文，只有在中文界面下才看得出来。改为 `--no-translations` + 显式复制该文件。**这就是最后那步校验存在的意义 —— `windeployqt` 缺东西也会返回 0。**

**裁剪**（截图工具用不上的部分）：`Qt6Network.dll` + `networkinformation/` + `tls/` —— 项目里没有任何地方开套接字，而那还是一个装在从不联网的工具里的 TLS 栈；外加 `generic/qtuiotouchplugin.dll`。**保留** `imageformats/`（仅 `qico` + `qjpeg`）与 `styles/`。

`Qt6Svg` + `iconengines/qsvgicon` 一度被**预留**给「下一步换 SVG 工具栏图标」。那一步最后走的是**代码绘制**（见 3.4），预留的理由随之失效，于是把这一组删掉了：`Qt6Svg.dll` 632KB + `qsvgicon.dll` 75KB + `qsvg.dll` 40KB + `qgif.dll` 49KB。**理由是攻击面不是体积** —— 一个从不打开图片文件的截图工具不该带着一个 632KB 的 XML/SVG 解析器，`qgif` 同理（没有任何代码路径读 `.gif`）。删之前先把「应用到底用什么格式」实测了一遍（见下）。

**插件是运行期按名字加载的，导入闭环看不见它们** —— 这跟缺一个 DLL 不一样：不会加载失败，只会某个功能静默失灵。所以 `probe_deploy` 新增第 `[4]` 节断言**加载到的格式集合**，而且是**双向**的：缺 `ico` 或 `jpeg` 要失败（托盘图标会变空白、存不了 `.jpg`），`svg` 或 `gif` **回来了**也要失败 —— 后者才是让上面那张裁剪清单成为「一个决定」而不是「会悄悄漂移的东西」。实测支持的格式是 `bmp cur ico jfif jpeg jpg pbm pgm png ppm xbm xpm`，其中只有 `cur`/`ico`/`jfif`/`jpeg`/`jpg` 来自 `imageformats/`，其余全是 QtGui 内置 —— 这正是「少一个也不报错」的原因。

> 反向断言特意**验证过它不是空转**：把 `Qt6Svg.dll` + `qsvg.dll` + `qgif.dll` 临时拷回暂存目录，第 `[4]` 节立刻两条 `FAIL`、探针 `exit=1`；清掉后恢复 `10/0`。一条永远不会失败的检查比没有检查更糟（这个教训在 `verify_iss.py` 上已经吃过一次）。

**闭环校验**（把「跑过 windeployqt」变成「这个文件夹自包含」）：`objdump -p` 扫暂存目录里每一个 PE 文件，把每条 `DLL Name:` 判成「在暂存目录里」或「在 System32 里」，两者都不是就失败。`api-ms-win-*` / `ext-ms-win-*` 是 API set 虚拟 DLL，由加载器通过 apiset schema 解析、本来就没有文件，必须跳过 —— 第一版把它们误报成 3 个缺失。另外单独断言 `platforms/qwindows.dll` 存在（它缺失时的报错最令人困惑），以及暂存目录里不能出现 `Qt6*d.dll`（Debug 可执行文件配 Release Qt 会在加载时版本不匹配失败）。

结果：**11 个 PE 文件、102 个导入全部解析、1 个翻译文件、34MB**（裁剪 SVG 组之前是 15 / 124 / 35MB）。

**刻意不做启动测试。** 启动应用会注册全局热键、往托盘塞图标、写 HKCU，而本脚本可能在用户工作中途被跑。启动能抓到的三类失败（缺平台插件、缺运行时 DLL、Debug/Release 混用）都已在上面静态覆盖。

`tools/smoke_deploy.sh` + `build-review/probe_deploy.cpp` 补上运行期那一半：把探针**拷进部署目录**再跑。这是唯一能证明插件真能加载的办法 —— Qt 的插件与翻译目录由 `QLibraryInfo` 的 prefix 推出，而部署场景下 prefix 由 `Qt6Core.dll` 的位置决定；从别处跑同一个二进制，Qt 会去找**已安装的** Qt，结论就与文件夹无关了。子进程的 PATH 只留 Windows 系统目录，进一步排除 Qt 安装作为来源。跑完 `trap EXIT` 自动删掉拷进去的探针，免得被后续部署一起发出去。

结果 **10/0**：`applicationDirPath` 确实在部署目录内、`Qt6Core.dll` 就在旁边、`platformName == windows`（平台插件从文件夹加载成功）、`TranslationsPath` **恰好等于 `<appdir>/translations`**（这个假设第一次被实测确认）、加载到的图片格式集合双向断言通过，装入 `qtbase_zh_CN.qm` 后 `OK → 确定`、`Cancel → 取消`、`&Undo → 撤消(&U)`、`&Open → 打开(&O)`。

`--app` 再加一步：把**真应用**也从这个文件夹里启动若干秒。默认关闭，因为启动会注册全局热键、往托盘塞图标、写 HKCU —— 发版前值得跑一次，但每次重新部署都跑就不合适了。退出码 `124`（被超时杀掉）正是成功条件，托盘应用本来就不该自己退出。实测 `124` 且零 stderr。

> ⚠️ 这一步踩到一个小坑：把 PATH 缩到 Windows 系统目录之后，`timeout` 这个名字解析到的是 **`C:\Windows\System32\timeout.exe`**，它只认 `/t`，直接给数字会报「无效语法」—— 与部署本身毫无关系的报错。必须写 `/usr/bin/timeout` 绝对路径。探针那一半不涉及这个问题（它不需要超时）。

### 3.3 安装包 —— ⚠️ 已写，从未编译

`tools/installer/qshot.iss`。

**先说清楚：这个脚本没有编译过、没有运行过。** 本机没有 Inno Setup（`Program Files` 下无 `ISCC.exe`，PATH 上无 `iscc`，已直接查过），所以它只被写出来并被静态检查过。**这是整条链上唯一未验证的一环** —— 其余部分都实际跑过。第一次编译时请把它当成待修的草稿，而不是已验证的产物。

三个刻意、且最容易被「好心改错」的决定：

1. **按用户安装**（`PrivilegesRequired=lowest`，`{autopf}` → `%LOCALAPPDATA%\Programs`）。QShot 本来就是纯每用户的：设置进 `HKCU\Software\QShot`、开机启动进 HKCU 的 Run 键、截图历史进 `%APPDATA%\QShot`。装成全用户只会多一个 UAC 弹窗、把 exe 放到用户改不了的地方，而状态照样是每用户的。
2. **文件必须保持带 BOM 的 UTF-8**。Inno 6 对没有 BOM 的脚本按系统 ANSI 代码页读；本机实测 **ACP 936（GBK）**，所以无 BOM 保存会把下面每一句中文都变成乱码，而在代码页不同的机器上会变成另一种乱码。
3. **卸载保留用户的设置与历史**。它们很小、是用户自己的数据，留着意味着重装回来还是原来的热键、保存目录和历史，而不是从零开始。

**开机启动值的清理写了两遍**，因为两种情况安装器各知道一半：`[Registry]` 的 `uninsdeletevalue` 覆盖「安装时勾了开机启动」；`[Code]` 里 `CurUninstallStepChanged` 无条件 `RegDeleteValue` 覆盖「用户后来在应用自己的设置里打开的」—— 那是安装器毫无记录的。不做的话，卸载会留下一个指向已删 exe 的 Run 项，Windows 每次登录都报一次。

**编译期守卫**：`#if !FileExists(StageDir + "\qshot.exe")` → `#error`。装不上东西的安装包比没有安装包更糟，因为它看起来成功了。

**没有 `AppMutex`**：应用没有单实例守卫，互斥体永远不会被创建，Inno 的「程序是否在运行」检测会静默失效。改用 `CloseApplications=yes` —— 运行中的实例占着 `qshot.exe`，升级会覆盖失败，这才是这里真正起作用的机制。

**`tools/verify_iss.py` 38 项静态检查**（不能编译时唯一诚实的替代品）：BOM 存在、每个 `{#宏}` 都能解析、`[Setup]` 关键键齐全、`AppId` 是合法 GUID 且用了 `{{` 转义、每个条目引用的 task 都在 `[Tasks]` 里声明、`[Files]` 的源路径与 `SetupIconFile` **真的存在于磁盘上**（这条替人把 `..\..\dist\QShot` 的层级数算了一遍）。它证明不了安装包能工作，只证明脚本内部自洽、它点名的文件都在。

> 自我纠正一条：`verify_iss.py` 第一版的 task 引用检查把 `Tasks:` 用 `^` 锚在行首，而实际写法里它是多行条目的中间一个键 → **一条都没匹配到，却报告成功**。一个什么都没验证却显示绿的检查比没有检查更糟，所以现在会额外断言「至少找到一条引用」。

**中文向导页**：Inno 不自带中文 `.isl`。脚本里的自定义文案已经是中文，内置页面（「下一步」等）在把 `ChineseSimplified.isl` 从 <https://jrsoftware.org/files/istrans/> 放进 Inno 的 `Languages\` 目录后，取消 `[Languages]` 里那一行的注释即可。

**代码签名：未做。** 没有证书，所以任何用户都会看到 SmartScreen 警告。这不是脚本能解决的问题。


### 3.4 工具栏真图标集 —— ✅ 已完成

八工具图标原先散在 `ToolbarWidget::drawButton` 的一条 if-else 链里，每个分支自己画几何、自己定笔宽。抽成 `src/overlay/ToolbarIcons.{h,cpp}`：

```cpp
bool paintTool(QPainter& p, AnnotationType type, const QRect& box, const QColor& color);
```

返回 `bool` 是为了让调用方区分「这是个工具」和「这是个动作/分隔符」—— 后者回落到动作文案，于是那条 if-else 链整段消失（`drawButton` 少了 `iconName` 参数和约 70 行）。

**共享的不只是笔宽，还有「不许两个读起来一样」这条规则。** 图标集自己拥有笔宽（`kStroke = 2.0`）、端帽（圆头圆角）、光学盒（把 20px 盒内缩半个笔宽，让描边**内侧**对齐盒边而不是骑在边上）。这条规则抓出两个真读错的形状：

- **画笔**原本是一条裸斜线 —— 也就是**去掉箭头的箭头**，和箭头的差别只有末端那个小三角。改成一条二次贝塞尔波浪（两段 `quadTo`）。
- **马赛克**原本是**两个方块**，读起来是「复制/重叠」而不是「打码」。改成 3×3 棋盘格（只填 `(row+col)` 为偶的格），`x1 = left + ((col+1)*side)/3` 这类写法保证三格加起来正好等于边长、不留缝隙。

另外两个是**视觉复核**（看 `toolbar_eight_tools.png`）抓出来的：**箭头**的倒钩从 4px 提到 6px（实测笔画数 4px→10、5px→18、6px→26，4px 时箭头几乎不像箭头）；**荧光笔**是八个里唯一画在满强度以下的（alpha 140 的填充、没有描边），在缩略图里明显比旁边七个淡 —— 改成「**填充**半透明、**描边**用图标集标准的 2px 不透明」，因为高亮的语义本来就是半透明色块，而形状得和别的图标一样实。

**一个真 bug**：`QRect::right()` 是 `x + width - 1`（闭区间），而 `QRectF::right()` 是 `x + width`。按 `QRect` 的算术画字形会让每个图标**少填最后一列像素**（20 格里只填 19 格），文字竖笔和徽标数字也偏半个像素。修法是 `paintTool` 里**只做一次** `QRect → QRectF` 转换，之后整条链都在浮点上算 —— 转换点只有一处，才不会有的分支漏转。

**验证**：`build-review/probe_toolbar_icons.cpp` **26/0**。核心一节是「每个字形与其余七个都不一样」，不是逐图标硬编码期望值：

- **最近邻对距离** `ellipse vs number, 36 of 1024 pixels differ`，阈值取 24 —— 由「笔画宽 2px、一个 20×20 字形里最细的特征也有 2px 宽」推出，不是拍一个好看的数。小于它说明两个图标在 1024 格里几乎重合。
- **方向反转数**：画笔 2、箭头 1。这条专门验「画笔不再是一条直线」—— 直线反转 0 次，波浪至少 2 次，而箭头因为有倒钩也是 1 次，所以不能用「是不是直线」区分，得数反转。
- **箭头墨量** 86 vs **裸笔杆** 64，差 22 —— 倒钩尺寸的回归防线。
- **笔画宽度**：矩形边 2px、文字竖笔 2px、箭杆横截 2px，都等于 `kStroke`。这里踩过两次坑：`runLength` 第一版只朝一个方向走，而种子点落在笔画的**第二个**像素上 → 读到 0；第二版沿着箭杆**自身方向**扫，量到的其实是**长度**（19px），得改成垂直扫。
- **荧光笔**：峰值 alpha 255（描边）、中心 alpha 140（填充）、45 个不透明像素 —— 直接验「描边实、填充虚」这个修复。

`build-review/render_toolbar_tools.cpp` 另外输出 `toolbar_icons_zoom.png`（6 倍最近邻拼版，1644×236）供目视，以及每行的**动作墨量**（`x > 300` 区域里 RGB 全 >200 的像素数）—— 八行全是 **238 px**，推翻了我「某一行文案偏暗」的怀疑：那是错觉。

> 顺带纠正一条**过期理由**：`tools/deploy.sh` 保留 `Qt6Svg` + `qsvgicon` 的注释写的是「下一步要换成 SVG 工具栏图标」。这一步走了代码绘制，那条理由变成假的，于是删掉整组（见 3.2）。**一条写错的注释比两种选择里的任何一种都糟**，所以改代码时要顺手把「为什么」一起改掉。


### 3.5 设置补项：静默保存 / 复制时同时保存 —— ✅ 已完成

两个新开关，**都默认关闭**。这不是占位而是刻意的：打开任何一个都会改变用户的文件出现在哪里，而一个开始写没人要的文件的截图工具，比一个每次都问的工具更糟。

**范围是这两个功能里最容易做错的地方**，所以两处都刻意收窄了：

- `save/quiet` 只作用于**截图工具栏的保存按钮**。图钉右键菜单与历史菜单里的两项是**另存为**——「另存为」的全部含义就是自己挑一个位置，让它静默等于让用户再也没法把某一张放到别处。这两条路径继续走 `saveImageWithDialog()`。
- `save/onCopy` 只作用于**正在截的这一张**，不含从历史菜单里重新复制一张：那个文件在第一次截取时就已经写过了，再写一份只会把保存目录填满同一张截图的副本。

**`saveImageQuietly()`**（`src/core/ImageExport.cpp`）：

- 文件名 `screenshot_<yyyy-MM-dd_HH-mm-ss>.<ext>`，扩展名跟随 `save/format`。
- **同名绝不覆盖**，后缀递增到 `_2`…`_999`。秒级分辨率下连按两次热键就足以撞名，而一个叫「保存」的动作把上一张截图覆盖掉，是这里最坏的结果。上界是必要的：目录不可写时 `exists()` 会一直说谎，无界循环会挂住应用。
- 目录不存在就 `mkpath` —— 设置里的目录可以是用户手输的、从没建过的。
- **第三个参数是目录覆盖**，空的才读 `Settings::saveDirectory()`。存在的唯一理由是探针：真实目录是用户数据，测试绝不能写进去。
- 失败**必须出声**：`parent` 非空时弹 `QMessageBox`（`无法写入文件：<路径>`），因为文件对话框被关掉之后没有别的东西能报错。`parent` 为空时不弹——调用方自己报，或者探针（不能阻塞在模态框上）。

**抽出 `writeImage()`**：两条保存路径对「格式从哪来」的看法不同（对话框看用户敲的扩展名，静默路径看设置），但**编码本身不能不同**——否则同一张图「另存为 .jpg」和「静默存 JPEG」会产出不一样的文件。判断 `.jpg`/`.jpeg` 的地方现在只有这一处。同理抽了 `resolvedSaveDirectory()`，否则对话框预选一个目录、静默保存写进另一个。

**反馈**（文件对话框没了，气泡就是唯一证据）：

| 情况 | 反馈 |
| --- | --- |
| 静默保存成功 | 托盘气泡 `已保存到：<路径>`，**不看**「复制后弹托盘提示」开关 |
| 复制时同时保存成功 | 气泡换成 `已复制，并保存到：<路径>` —— 一个说两件事的气泡好过两个各说一件的 |
| 静默保存失败 | `QMessageBox`，绝不清默 |

**接线**：`SnapOverlay::captureCopied()` 变成 `captureCopied(const QString& savedPath)`，`savedPath` 是复制顺带写出的文件（没写就是空，**空是常态而不是错误**——写失败已经弹过框，而复制本身成功了）。新增 `SnapOverlay::captureSaved(const QString&)` 只由静默保存路径发出：有对话框时没什么可报的（对话框就是确认），静默保存没有对话框了，这条信号是用户和「到底存上了吗」之间唯一的东西。`HistoryMenu::copied()` 保持无参，在 `ShotApplication` 里用一个 lambda 接成 `onCaptureCopied(QString())`，理由（重新复制不该产生重复文件）写在 lambda 旁边而不是散在类型里。

**验证** `build-review/probe_quiet_save.cpp` **29/0**：名字模式（正则锚定，含日期时间形状）、**强制制造同名碰撞**并断言后缀递增且原文件**一个字节都没动**（循环最多 3 次并要求「碰撞路径确实被走到」，否则这条检查会时灵时不灵）、目录按需创建、PNG 无损往返 / `.jpg` 真的是 JPEG 且真的有损、历史记录**采用**该文件而非重新编码（断言后缀保留 + 删掉用户原文件后条目仍可读）、空图像不写任何东西、以及**探针改过的设置被放回**（独立读 `QSettings` 复核，不是信刚写过的那几个访问器）。

> **一条失败，又是期望值写错**：`QImage::operator==` **连格式一起比**，而合成器交给我们的是**预乘**格式、从文件读回来的是直通 ARGB32 —— 于是「PNG 无损往返」在像素完全相同时**失败**，而「JPEG 有损」在格式已经不同时**必然通过**（这条通过得毫无意义）。改成两侧都 `convertToFormat(ARGB32)` 后再逐像素数差异：PNG 报 `0`，JPEG 报 `851 of 851`。**同一个格式陷阱会让一条检查假失败、另一条假通过**，只修前者等于留了个空转的检查。



### 3.6 清理：单实例保护 + 马赛克增量 + P1 剩余项复核 —— ✅ 已完成

三项合在一起收尾，因为它们的共同点是「都只能靠探针确认」。

#### 3.6.1 N-7 马赛克增量包围盒 —— 探针抓到一个既有 bug

`updateMosaic()` 原先每次都用**整条路径**的包围盒决定工作区域。马赛克标注每移动一次鼠标就追加一个点，所以路径包围盒随拖动增长 —— 一条横跨屏幕的笔画，到最后每一次 mouseMove 都在重扫全屏。现在它接收调用方**刚落下墨迹的那个矩形**（`logicalDirty`，空 = 整条标注，给 `rebuildMosaicCache()` 用）。

**探针 `probe_mosaic_incremental` 第一次运行就报「两种喂法差了 314 个整块」。** 差异是 314 个**整块**（每块 12×12 = 144 设备像素，314×144 = 45216，与报数完全吻合），不是边缘像素带 —— 整块差意味着「掩码整体错位」，而不是「覆盖率判定不一致」。根因：

```cpp
p.translate(-logicalBounding.topLeft());   // 未裁剪的逻辑原点
```

而掩码图是从 `physicalBounding.topLeft()` 开始的，那个矩形**已经和图像求过交**。两者只在「没有裁剪」时是同一点：任何触到选区边缘的笔画，其掩码原点被裁进去了多少，整幅马赛克就被推出去多少。左上角点一下（`mosaicSize` 8、dpr 1.5），马赛克被推到对角一个块外。

**这是既有 bug，不是 N-7 引入的** —— 旧的整路径调用同样会裁剪。修法是按掩码自己的原点平移：

```cpp
p.translate(-physicalBounding.x() / dpr_, -physicalBounding.y() / dpr_);
```

除以 `dpr_` 是精确的，且在没有裁剪时退化成原来的表达式。修完 `probe_mosaic_incremental` **10/0**：240 点、600×450 物理图，**整路径 98ms → 增量 7ms**，两幅图逐像素相同。

探针因此新增 `[5] 左上角点一下落在角落那个块里` —— 断言马赛克包围盒是 `(0,0)-(11,11)`，即**恰好一块**。这是那条 bug 的最小复现，也是以后最容易被重新引入的那一处。

> 记一笔：这个 bug 值得被记住的地方不是平移写错了，而是**「探针失败先怀疑期望值」这条经验这次不适用**。前 9 条失败全是探针自己写错，所以第 10 条的直觉是「又是我写错了」。区别在于**失败的形状**：期望值写错通常给出一致的、可解释的数字（差一个像素、差一行），而 314 个**整块**是个结构性数字 —— 它指向「坐标系错了」，不指向「断言写错了」。

#### 3.6.2 P1-4 单实例保护

**为什么必须做**：两份 QShot 抢同一个全局热键，第二份**必然**注册失败，然后常驻托盘、图标和第一份一模一样、却宣称快捷键被占用。它不是「第二个窗口」，是一份坏掉的副本。

- 新接口 `core/ISingleInstance.h`（只有 `bool acquire()`）+ `platform/windows/WinSingleInstance.{h,cpp}` + `PlatformFactory::createSingleInstance()`。与 `IAutoStart` 同形：接口只暴露 `main()` 需要知道的那一件事。
- **名字 `Local\QShot.SingleInstance`**。`Local\` 前缀把名字限制在登录会话内 —— 快速用户切换时两个用户各得一份，而不是第二个人被一个他够不着所有者的名字劝退。名字**不带版本号**：更新后新旧两份能并存，正是这个守卫要防的状态。
- **为什么是内核对象而不是锁文件**：进程无论怎么死，OS 都会释放 mutex；锁文件崩一次就永久把用户锁在自己的程序外面，直到他找到并删掉那个文件。这是这个选择唯一的理由，写在接口注释里。
- **`CreateMutexW` 即使对象已存在也返回有效句柄**，只有 `ERROR_ALREADY_EXISTS` 能区分 → 必须**立刻**读 `GetLastError()`。两个分支都先取出 `error` 再格式化消息，就是为了不让别的调用把它冲掉。
- **创建失败（句柄耗尽）时放行**并 `qWarning`：拒绝启动比启动两次更糟。
- **名字可注入**（构造参数），唯一理由是探针：真实名字会被用户正在跑的 QShot 占着，探针要么假失败、要么把真实例顶掉。
- 接线在 `main.cpp`：`instanceGuard` 声明在 `shotApp` **之前**，所以析构在它之后 —— 第一个实例启动期间不会松手。文案进 `Strings.cpp`（现 70 条），**在设置过 application name 之后**才读，所以跟随语言设置。
- **验证** `probe_single_instance` **7/0**：工厂非空（顺带**报告**真实名字是否被占，不断言 —— 「被占」= 用户开着 QShot，不是缺陷）；第二个持有者被拒 + 重复问仍然答应（接口承诺）；持有者销毁后名字重新可用；**换个名字互不影响**。最后一条不能省：没有它，`[2]` 在 `acquire()` 只是读了个静态标志时也会通过。

**未做且刻意未做**：第二个实例目前只弹框退出。对截图工具更好的做法是「第二个实例让第一个立刻截图」，但那需要消息专用窗口 + IPC —— 命名互斥量带不了载荷。见第八节。

#### 3.6.3 P1-3 / P1-6 / P1-8 复核

复核的结论是**大部分早就修掉了，只是文档没跟上**：

| 项 | 结论 |
| --- | --- |
| P1-3 老式 `SIGNAL/SLOT` + 多余 `dynamic_cast` | ✅ 已修（`ShotApplication.cpp` 用的是 `&IGlobalHotkey::hotkeyPressed`，全文无 `dynamic_cast<QObject*>`） |
| P1-6 VK 映射 | ✅ 主体已修（`virtualKeyFor()` 显式映射表，未覆盖返回 0；`MOD_NOREPEAT` 已用宏）。本轮只剩 `hotkeyId_ = 1001` 这个魔数 → 改成 `constexpr int kHotkeyId` 并写明「值本身没有含义，只要非零且稳定」 |
| P1-8 `trayMenu_` 泄漏 | ✅ 两处都清了：`trayMenu_` 已是 `unique_ptr`；`globalHotkey_` 从「裸指针 + 手动 delete + 交给 QObject 父对象」**三份所有权表达**收敛成 `unique_ptr` + 无父对象。后者的关键是要同时把工厂调用改成 `createGlobalHotkey()`（不传 parent），只改一半就是双重释放 |

> **一次构建失败值得记**：`globalHotkey_` 改成 `unique_ptr` 后 `connect(globalHotkey_, ...)` 编译不过（`unique_ptr` 转不成 `QObject*`）。这条编译错误正是「所有权表达只剩一种」的证明 —— 之前那个裸指针能被 `connect` 直接接受，恰恰是因为它同时是裸指针和 QObject 子对象。

---

### 3.7 性能项收尾（P2 全节 + R3-6）—— ✅ 已完成

用户要求「先修性能项」。动手前先把三份审查文档里所有性能条目**逐条对着当前代码复核**了一遍 —— 这一步是必要的，因为 ROUND3 的表格已经证明过会过期（P1-3/P1-6 标着 ❌ 其实早修了）。

复核结果：**四条里有两条早就修好了，只是文档没跟上**。

| 编号 | 复核结论 |
| --- | --- |
| P2-1 马赛克全图扫描 | 主体已在 M6 清理的 N-7 修掉（增量包围盒，98ms → 7ms）。本轮补：缓冲区复用 + 建议里的 16ms 节流**刻意不做** |
| P2-2 Idle 整屏重绘 | ✅ 已修（局部重绘已在，本轮改用统一辅助函数并复核）。放大镜 `srcImage` 复用**刻意不做** |
| P2-3 全屏数据存两份 | ✅ 已修（`backgroundPixmap_` 已不存在） |
| P2-4 每次 release 裁三张大图 | ✅ 已修（马赛克图层懒分配）。**顺带发现反向问题**：图层被清空后仍每帧 blit |
| R3-6 四状态整屏重绘 | 本轮修复，见下 |

#### 3.7.1 R3-6：让 `paintEvent` 认识脏区，并给四个状态各自算脏区

两半缺一不可：状态给了脏区、`paintEvent` 照样整屏画，等于没做；`paintEvent` 会裁剪、状态还是无参 `update()`，也等于没做。

- **`paintEvent`**：改用 `event->rect()`，`painter.setClipRect(dirty)`，背景 `drawImage` 与遮罩都只落在脏区。
- **暗色遮罩**：`fullPath.subtracted(holePath)`（两个整屏路径的布尔运算）→ 4 个 `fillRect`（上/下/左/右）。
  两个轴对齐矩形的差**就是**四个矩形，所以这不是近似，是同一个画面少算了布尔运算。
  四块必须**互不重叠**：遮罩色是 `rgba(0,0,0,100)`，重叠会叠深成一条更暗的带子。
- **脏区辅助函数**（`SnapOverlay` 私有）：`dimensionBadgeRect()` / `selectionChromeRect()` / `activeChromeRect()` / `magnifierRect()` / `annotationInkRect()`。
  全部按「宁大勿小」设计：脏区少一个像素会在屏幕上留下一条残影，多几个像素只是多画几笔。
  尺寸徽标的矩形由**同一个函数**同时供绘制与脏区使用 —— 与 `textHintRect()` 同一个理由，画的和重绘的不能是两次独立计算。
- **四个状态各自的脏区**：Annotating = 笔迹新旧包围盒；Moving / Resizing = 选区新旧 chrome；
  Dragging = 选区新旧 chrome ∪ hover 轮廓 ∪ 放大镜面板。
- **Annotating 里笔画类工具只算最新一段**：画笔 / 高亮 / 马赛克是**逐段追加**墨迹的，
  整条路径的包围盒会随拖动线性增长。五个体量工具（矩形/椭圆/箭头/序号/文本提示）不同 ——
  它们每次移动都从两个端点把整个图形重画一遍，所以整块就是诚实的脏区。
  判据是 `isStrokeTool(type)`，一处定义、两处使用（`mouseMoveEvent` 与 `updateMosaic` 的入参）。
  取「上一段 ∪ 最新一段」而不是只取最新一段：两段相接处的圆角 join 覆盖的圆盘与
  前一段末尾的圆头帽**完全相同**，严格说上一段不用重绘；但多带一段的代价是常数（149×109），
  少带一段的代价是屏幕上留一条残影，取值不值。
  Dragging 里 hover 轮廓那一项不能省：拖动阈值跨过去的那一帧起，虚线轮廓就不再被绘制了，脏区不含它就会把虚线留在屏幕上。
- **顺手修掉的反向开销**：`AnnotationLayer::paint()` 原先判 `!mosaicLayer_.isNull()` 才 blit 图层，但图层一旦分配就一直在 ——
  undo 掉最后一个马赛克之后，它变成一张全选区大小的**全透明**图，却仍被每帧 blit。改成由 `mosaicInkPresent_` 把关，
  在写入块的地方置位、在 `rebuildMosaicCache()` 开头清零。

#### 3.7.2 实测

`build-review/probe_partial_repaint.cpp`（23 号探针，**119 checks / 0 failures**）。屏 900×700。

| 状态 | 改前 | 改后脏区 |
| --- | --- | --- |
| Idle（放大镜跟随） | 100% | 264×204 = 8.55% |
| Selected（点击输入提示） | 100% | 188×82 = 2.45% |
| Annotating（拖矩形） | 100% | 117×57 = 1.06% |
| Annotating（画笔，逐段） | 100% | 149×109 = 2.58%（每段恒定） |
| Annotating（马赛克，逐段） | 100% | 207×167 = 5.49%（每段恒定） |
| Dragging（拉选区） | 100% | 476×353 = 26.67% |
| Moving（整体平移） | 100% | 249×176 = 6.96% |
| Resizing（拖手柄） | 100% | 289×216 = 9.91% |

「每段恒定」是这两行存在的意义：如果脏区是按**整条笔迹**算的，一条从屏幕左上划到右下的笔画，
最后几段的脏区会一路涨到接近整屏 —— 而这正是 R3-6 要消灭的东西。探针为此单独断言了
「一条笔画的最新一段脏区明显小于整条路径的包围盒」。画笔与马赛克走的是**不同**的边距公式
（画笔 = 半笔宽，马赛克 = 两个块宽），所以两条都要跑。

单帧代价（同屏 900×700，取 Dragging 这个最坏的脏区）：**全量 0.92ms → 脏区 0.34ms**。探针窗口只有 63 万像素，
而本机真实屏幕是 2560×1600 物理像素（4.1M），脏区占比也比这里小得多，所以实机上的差距比这个数字大。

#### 3.7.3 探针的断言为什么这么写

断言不是「脏区等于我手算的矩形」—— 那只是把被测代码的公式抄进测试，公式错了照样绿。断言的是公式存在的**目的**：

> **只重绘脏区之后的整帧，与整帧全量重绘逐像素相同。**

脏区少一个像素就失败，多几个像素则通过 —— 这正是「宁大勿小」允许的形状。再配一条「屏幕远端角落不在脏区里」，
把「正确」升级成「正确**且**确实变小了」；否则一个无参 `update()` 能满足第一条却过不了第二条。
另加两条负向对照（故意给过小的区域必须报出差异；把 `targetOffset` 传成 `(0,0)` 必须让画面位移），
保证这套比较**有能力失败**。

#### 3.7.4 踩到的两个坑（都是探针的，不是产品的）

**一、`QWidget::render()` 的 `targetOffset` 语义。** 探针第一版用
`render(&target, QPoint(0,0), QRegion(dirty))` 想把脏区原地重绘，结果每一步都报 10 万+ 差异像素，
而且差异框的尺寸**恰好等于脏区尺寸、位置在原点**。这条线索指向坐标位移而不是绘制内容，于是写了个 30 行最小程序定论：

```
paint event rect for the partial render: 100,50 20x20
full   @ (0,0)=green  @ (100,50)=red
part   @ (0,0)=red    @ (100,50)=black
=> the region's content landed at the ORIGIN
```

`render(target, targetOffset, sourceRegion)` 画的是**源区域的内容 → targetOffset**，不是「控件原点 → targetOffset、
区域只作裁剪」。原地重绘要传 `region.topLeft()`。探针现在把这个语义**显式断言**（连反例一起），不再依赖对文档的某种读法。

**二、hover 定时器的干扰。** overlay 有一个 30ms 单次 `hoverTimer_`，由 Idle 下的鼠标移动启动，
它一次 `update()` 会把被悬停窗口的轮廓画出来 —— 而那个轮廓在真实桌面上是**当前鼠标底下那个窗口**，
于是它把 dim 遮罩改了几乎半个屏幕。测量窗口内它一响，`prev` 和 `expected` 就不再是同一状态，
表现和「脏区算错了」一模一样（差异框 672×512 对着 264×204 的脏区）。
两处修正：`drain()` **不再在两次 `processEvents()` 之间 sleep**（那正是让 30ms 定时器有机会插进来的原因），
以及把 Idle 那一步挪到**最后** —— 它启动的定时器后面已经没有任何测量了。
探针里也写明了覆盖边界：hover 轮廓的矩形来自窗口探测器，需要真实光标下真有窗口，这一条本机无法确定性验证。

#### 3.7.5 探针抓出的一个真缺陷：马赛克脏区边距不够

把笔画类工具加进探针之后，马赛克那五个 segment **全部失败**：差异框出现在笔画起点**上方 24px** 处，
而当时的一倍块宽边距只允许 17px。

根因不在脏区公式，在 `updateMosaic()` 的两级扩张：它先把笔画自己的矩形**外扩一个块宽**，
再把块网格**对齐到图像原点** —— 于是第一个可能被写入的块，可以在「外扩一格」的基础上**再往前一格**，
最后一个同理往后一格。一倍块宽的边距必然差一格。

修法是 `annotationInkMargin()` 里马赛克取 `2 * a.mosaicSize`（16px 块 → 33px 边距，含抗锯齿的 +1）。
这条**只可能被实测抓到**：边距公式与被测的绘制代码是两处独立实现，肉眼比对「看起来够大」在这里正好错了 24 比 17。

顺带说明：`annotationInkMargin()` 是**唯一**的边距来源，绘制侧不重复声明自己的外扩量，
所以修正一处即可，不存在「改了绘制没改脏区」的漏项。

---

## 六、执行顺序与依赖

```
Phase 0.2 拆 SnapOverlay ──┐  ✅ M0
                           ├─→ Phase 0.1 修坐标空间 ─→ Phase 0.3 焦点路由
                           │  ✅ M1                      ✅ M2（多屏实际效果待人工确认）
                           ↓
                    Phase 1.1 贴图  ← 复用 renderSelectionImage()，最小工作量
                           ↓  ✅ M3
                    Phase 1.2 历史记录 ← 复用已存盘文件
                           ↓
                    Phase 1.3 序号/高亮 ← 需先定工具栏宽度方案
                           ↓
                    Phase 3 打磨（图标 / 安装包）

  ┈┈┈┈ 以下已暂缓，不在本轮 ┈┈┈┈
  Phase 2.1 滚动截图     Phase 2.2 OCR
```

### 里程碑清单

| # | 任务 | 依赖 | 验收方式 |
| --- | --- | --- | --- |
| M0 | 拆 `SelectionGeometry` + `FloatingPanel` | — | ✅ **已完成**：编译零警告；93 条单元断言全通过；放大镜渲染逐像素比对一致（5 用例 `differing=0 maxDelta=0`） |
| M1 | 修 R3-4 坐标空间（新增 `screenGeometry_`，赋值处归一化） | M0 | ✅ **已完成**：编译零警告（顺带清掉 8 个历史告警）；20 条合成屏几何断言全通过；M0 探针回归无变化。**真双屏行为待人工确认** |
| M2 | 焦点路由 | M1 | ✅ **机制已锁**：`probe_noactivate` 9/9（面板 `WS_EX_NOACTIVATE` 早已落地且生效；`reclaimKeyboardFocus()` 覆盖已全）+ 多屏按光标激活（D）已实现。⚠️ 多屏实际效果**本机 1 屏无法验证**，需人工确认 |
| M3 | 贴图 `PinWindow` | M2 | ✅ **已完成**：静态检查零告警 + 构建 + 冒烟通过；`probe_pin_geometry` 28/0、`render_pin` 51/0（DPR 1/1.5/2 均为 1:1，无重采样）、`probe_pin_wiring` 13/0（拖选 → Ctrl+T → 逐像素正确的裁剪）。✅ 悬停提示样式见 1.1.1：`render_pin_hover` 40/0，含 27903 像素的穷举比对。⏳ 拖拽/滚轮手感、阴影深浅需人工确认 |
| M4 | 历史记录 `HistoryStore` | M1 | ✅ **已完成**：静态检查零告警 + 构建 + 冒烟通过；全量回归一键跑通（13 目标零告警，363 断言 0 失败），其中 `probe_history_store` 81/0、`probe_history_menu` 28/0（含 id-vs-行号回归）；菜单与设置对话框离屏渲染目视通过。⏳ 20 条 / 200MB / 复制后提示三个默认值需人工确认 |
| M5 | 序号 / 高亮标注 | M0 | ✅ **已完成**：静态检查零告警 + 构建 + 冒烟通过；`probe_toolbar_layout` 13/0（**实测推翻了「工具栏会溢出屏幕」这一前置约束**：8 工具仅 484px，余量 1223px，未做两排布局）、`probe_annotation_types` 31/0（含反解混合证明高亮半透明、圆非方、`number` 真的生效）；8 图标离屏渲染目视通过。⏳ 图标形状与三个默认值需人工确认 |
| M6 | 托盘图标 / 真图标集 / 安装包 | M3-M5 | ✅ **图标 + 部署已完成**：`tools/verify_icon.py`（不依赖 Qt 解析 ICO 容器，12143 字节 / 8 尺寸）+ `probe_icon_resource` 29/0（从二进制内资源表读回验像素）；`tools/deploy.sh` 产出经闭环校验（15 个 PE / 124 导入全解析 / 35MB）的暂存目录，`tools/smoke_deploy.sh` + `probe_deploy` 6/0 在部署目录内验证平台插件与中文目录真实生效。⚠️ **安装包 `tools/installer/qshot.iss` 从未编译**（本机无 Inno Setup），仅有 `tools/verify_iss.py` 38 项静态检查。⏳ 图标造型、工具栏真图标集、设置补项、代码签名未做 |

**明确不做**（本轮）：OCR、滚动长截图（见第四节）；`IScrollInput` 之类的单实现接口（规则里的「不为单一实现创建抽象接口」—— 但注意这与现存 4 个单实现接口的冲突仍未裁决）。

---

## 七、验收手段（受环境限制，必须按此执行）

**能确定性验证的**：
- 编译 + `-Wall -Wextra -Wshadow -Wunused` 零警告。**注意：静态检查必须扫全树 `src/*.cpp`，不能只扫改过的文件** —— M1 就是这么发现 `WinWindowDetector.cpp` 里 8 个「先前就存在、单文件检查从未覆盖」的告警的。秒级命令（不跑 AUTOMOC）：
  ```
  g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow -Wunused -Isrc -I$QT/include{,/QtCore,/QtGui,/QtWidgets} src/**/*.cpp
  ```
- **离屏渲染**：链接真实 `.cpp` + `build/.../qshot_autogen/<HASH>/moc_*.cpp`，用 **DPR=1 的 `render()`**（不要用 `grab()`，它返回 DPR 缩放后的位图会误导）渲到 `QImage` 存 PNG，再目视。现成脚手架见 `build-review/render_panels.cpp`、`render_text_editor.cpp`、`render_settings_dialog.cpp`。
  - ⚠️ **不要设 `QT_QPA_PLATFORM=offscreen`**：实测该平台在本机字体库退化，**一个字都不渲染**（同一脚手架：offscreen 下 180×30 且全空白，真实平台下 167×53 且文字正常）。离屏渲染指的是「渲到 QImage 不给人看」，不是这个平台插件。
  - 编译脚手架时的路径坑：MinGW g++ **不认 Git Bash 的 `/d/...` 挂载路径**，必须写 `D:/Qt/...`；链接 `Strings.cpp` 还要补 `Settings.cpp` + `moc_Settings.cpp`（在不同 hash 目录下）。
- **纯逻辑单测**：`SelectionGeometry`（93 条断言，见 `build-review/probe_selection_geometry.cpp`）、`HistoryStore` 的环形/上限/失效清理、拼接的 `dy` 搜索 —— 这类不碰焦点与渲染，可以写真正的断言；
- **重构等价性比对**：把重构前的实现抄进探针，与新实现用相同输入渲染后逐像素比对。见 `build-review/probe_magnifier_render.cpp` —— 这比「读一遍觉得等价」可靠得多，且能留下可视化产物；
- **「有没有被重采样」可以量化**：给渲染探针喂一张 1px 宽的红蓝条纹图，然后**数一行里出现了几种颜色** —— 恰好 2 种就是 1:1，出现中间色就是被插值了。这比目视「看着挺清楚」可靠得多，而且能测出半像素偏移这类肉眼看不出的问题（M3 就是靠它抓到 DPR 1.5 下整幅图被平移半像素）。见 `build-review/render_pin.cpp`，三档 DPR 各一组断言；
- **端到端接线要驱动真实事件链**：`sendEvent()` 可以直接把鼠标/键盘事件投进控件，绕过平台层，因此不需要显示窗口（不会闪屏）。M3 用它验证「拖出选区 → Ctrl+T → 发出逐像素正确的裁剪」，背景用**坐标编码图**（每个像素的颜色编码自己的坐标），任一像素错位都能被指出来。见 `build-review/probe_pin_wiring.cpp`；
- **原生窗口样式**：焦点行为测不了，但**承载焦点的机制**能测 —— `GetWindowLongPtrW(hwnd, GWL_EXSTYLE)` 读扩展样式，比 `windowFlags()` 可靠（后者只是请求）。`WS_EX_NOACTIVATE` 就等价于「点它 / `show()` 都不会被激活」。见 `build-review/probe_noactivate.cpp`：对进程内**所有**顶层窗口施加一条通用不变量，而不是手写清单。注意 `winId()` 就足以逼出 HWND，**不必 `show()`**（`SnapOverlay` 全屏，show 会闪屏）；
- **合成屏几何**：注入非零屏幕原点（如 `QRect(1920,0,1920,1080)`）暴露坐标空间错误，不需要真有第二块屏。见 `build-review/probe_multiscreen_coords.cpp`。**探针里必须调用产品真正用的那个换算（如 `mapToGlobal`），不能自己重算一遍**，否则探针和产品会各自漂移；
- **「两次渲染只应在指定区域不同」要穷举比对，不要抽查**：把同一控件的两种状态各渲染一次，逐像素比对，并**精确划定允许不同的区域**。`render_pin_hover.cpp` 用它在 27903 个像素上断言「悬停样式除了四周 14px 窄带与按钮区之外什么都没碰」。抽查会漏掉「色带循环多跑一圈」这类只影响一行像素的错误。
  - ⚠️ **比对必须在设备像素空间做，而 `QImage::pixel()` 索引的就是设备像素**。控件几何是逻辑坐标，两者在 DPR ≠ 1 时不是一回事。这个坑真实发生过：第一版把逻辑坐标直接喂给 `pixel()`，于是采样范围**恰好落进了阴影窄带本身**，报出 2059 个「差异像素」——全是阴影在正常工作。任何跨 DPR 的像素比对都要显式乘上 dpr，且允许区域也要按 dpr 取整（阴影深度 14 逻辑像素在 DPR 1.5 下要按 `qCeil(14×1.5)=21` 设备像素留边）。
  - 断言「允许不同的区域」时要留**至少 1–2 设备像素的余量**：抗锯齿的覆盖可能溢出逻辑矩形一点点，否则会误报成「样式挪动了画面」。
- **半透明叠加的量值可以反解出来**：源图用已知纯色，`alpha = 255 × (1 − 观测值/源值)` 就能把阴影/蒙版的 alpha 逐层读回来，于是「衰减曲线对不对」「四角有没有被叠两次」都变成可断言的数字，而不是「看着挺自然」。见 `render_pin_hover.cpp` 第 3 节；
- QSettings 往返、注册表写入后独立复核（用 Python `winreg`，`reg.exe` 被拉黑）。

- **`QWidget::render(target, targetOffset, sourceRegion)` 画的是「源区域的内容 → targetOffset」，不是「控件原点 → targetOffset、区域只作裁剪」。** 实测（30 行最小程序）：把 (100,50) 处的区域用零 offset 渲出去，内容落在目标的左上角，而控件的 (0,0) 没动。所以「原地重绘某个区域」必须传 `region.boundingRect().topLeft()`。这条只在**局部重绘的验证**里会踩到，但它伪装得极像「脏区算错了」：每一步都报 10 万+ 差异像素，而差异框的尺寸恰好等于脏区尺寸、位置在原点。**差异框的尺寸/位置和脏区对不上，就是坐标问题，不是覆盖率问题。** 见 `build-review/probe_partial_repaint.cpp`（它把这条语义连反例一起断言下来，不再依赖对文档的某种读法）。
- **测量「控件请求了多大的重绘区域」只能从 paint 事件里读，而 paint 事件只在控件可见时才会来。** `QWidget::update()` 对不可见控件是空操作，所以探针必须 `show()` —— 对全屏 overlay 就先把平台强制成 `offscreen`。而 `QRegion()` 空区域对 `render()` 意味着「整个控件」，所以「一个 paint 事件都没收到」会让所有比对**免费通过**：每一步都要断言区域非空。见 `probe_partial_repaint.cpp` 的 `drain()`。
  - 推论：控件自己的定时器会在测量窗口内改状态。overlay 有一个 30ms 的 hover 定时器，它一次 `update()` 会把「当前鼠标底下那个真实窗口」的轮廓画出来，于是 dim 遮罩变了半个屏幕 —— 表现和脏区算错一模一样。**先让定时器响完再测，或者把它挪到最后一步。**

**探针的构建与运行已经脚本化**：`bash build-review/build_probes.sh [--run] [名字...]`（无名字 = 全建全跑）。**23 个目标**的源文件组合与 moc 依赖写在脚本里的注册表中，**moc 按类名 glob 定位、绝不写 hash 目录**（CMake 一重跑，hash 就变，手写的链接行必然指向不存在的路径）。这一条是被两类反复发生的错误逼出来的：漏一个 moc 文件、以及路径指向已消失的 hash 目录。

- ⚠️ **运行探针前必须把 Qt 与 MinGW 的 `bin` 加进 `PATH`**，否则 exe 在 loader 阶段就失败，而 Git Bash 把它报成**没有任何输出的 `exit=127`** —— 看起来完全像「二进制不存在」。`--run` 已经代劳。
- ⚠️ **脚本执行期间不要编辑脚本**：bash 是按字节偏移边读边执行的，改动会让它从错位的位置继续解析，报出与真实内容无关的语法错（曾误判成 `echo "...(s)"` 有问题）。
- 平台层必须**整组链接**：`PlatformFactory.cpp` 点名了全部四个 Windows 实现，而每个实现的 vtable 在自己的 TU 里发出，只链调用到的那一个仍会缺符号。库也是：`-ldwmapi -lgdi32 -luser32` 跟着平台组一起给。

**部署包有两层验证，缺一不可**（见 3.2）：

- **静态闭环**（`tools/deploy.sh` 内置）：`objdump -p` 扫暂存目录里每个 PE，每条 `DLL Name:` 必须落在暂存目录或 `System32` 里。这一步证明「没有东西缺失」。
  - ⚠️ **`api-ms-win-*` / `ext-ms-win-*` 必须跳过**：它们是 API set 虚拟 DLL，由加载器通过 apiset schema 解析，本来就没有文件。按「文件是否存在」判会误报。
  - 单独断言 `platforms/qwindows.dll` 存在 —— 它缺失时报的是「could not find or load the Qt platform plugin windows」，这句话不会告诉你找过哪些目录。
- **运行期**（`tools/smoke_deploy.sh` + `build-review/probe_deploy.cpp`）：把探针**拷进部署目录**再跑。这一步证明「Qt 找得到」。
  - ⚠️ **不能从别处跑同一个二进制**。Qt 的插件与翻译目录由 `QLibraryInfo` 的 prefix 推出，而部署场景下 prefix 由 `Qt6Core.dll` 的位置决定；从临时目录跑，Qt 会去找**已安装的** Qt，探针就在报告 Qt 安装而不是那个文件夹。探针因此会先断言 `Qt6Core.dll` 就在自己旁边。
  - 子进程的 PATH 只留 Windows 系统目录，进一步排除 Qt 安装作为来源 —— 文件夹真自包含则毫无影响，不自包含则在这里响亮地失败，而不是在用户机器上安静地失败。
  - 顺带解决了一个纯静态检查做不到的事：**`QLibraryInfo::path(TranslationsPath)` 在部署布局下到底解析成什么**，这次第一次被实测确认是 `<appdir>/translations`。
- **刻意不做启动测试**：启动应用会注册全局热键、往托盘塞图标、写 HKCU，而部署脚本可能在用户工作中途被跑。启动能抓到的三类失败（缺平台插件、缺运行时 DLL、Debug/Release 混用）都已在静态层覆盖。


**不能确定性验证、必须人工确认的**：
- 任何涉及窗口 active / `QGuiApplication::focusWindow()` 的结论 —— 终端启动的进程 `GetForegroundWindow()` 恒为终端窗口，Windows 据此拒绝 `SetForegroundWindow()`，同一份代码不同轮次结果会**相反**；
- **双屏行为**（本机只有 1 块屏）；
- 光标相关断言（会话空闲时 `GetCursorInfo` 可能返回 `hCursor=NULL`）；
- 像素级差异判断（本机两次连续 `grabWindow` 有 94% 像素不同，最大通道差 106）。

---

## 八、决策记录

### 已决（2026-09-21）

| # | 事项 | 决策 | 影响 |
| --- | --- | --- | --- |
| 1 | OCR | **推迟** | 第四节 2.2 转为评估存档；本轮不引入任何 OCR 依赖 |
| 2 | 滚动长截图 | **推迟** | 第四节 2.1 转为评估存档；本轮不新增 `src/capture/` |
| 3 | 历史记录落盘 | **认可** `AppDataLocation/history/`、默认上限 20 条 | M4 按此实现；上限做成设置项，用户可改 |

### 仍然悬空（不阻塞本轮，但迟早要定）

1. **多屏 overlay 键盘焦点路由** —— **已决（2026-09-21，方案 D）**：按 `QCursor::pos()` 落在哪块屏决定激活哪块 overlay，回退到第一块。原「面板抢焦点」假设已作废 —— `WS_EX_NOACTIVATE` 早已落地并生效，`reclaimKeyboardFocus()` 的调用点也已覆盖全部路径（证据见 0.3 节）。**残留风险**：本机 1 块屏，D 的实际多屏效果未验证。
2. **马赛克是否改「提交时才落层」** —— 当前拖动即写入 `mosaicLayer_`，右键取消会残留并被导出（R3-5）。
3. **「另存为」的两处要不要也跟随「保存时不再询问」** —— 现在**刻意不跟随**（见 3.5）：图钉右键与历史菜单里的两项是「另存为」，含义就是自己挑一个位置，静默它等于让用户再也没法把某一张放到别处。如果实际用起来觉得「我就是要它直接存」，再改。
4. **设置对话框是否改即时生效** —— 现在 OK 时统一应用，因此切语言不重译已打开窗口（换来「取消」是真取消）。
5. **规则冲突**：「不为单一实现创建抽象接口」vs 现存 4 个单实现接口 + `PlatformFactory`。M0 拆分会再引入纯函数模块（非接口，不加剧冲突），但这条总得有个结论。
6. **贴图的悬停样式是否要设置项，以及当前这几个数值是否合适** —— 内阴影深度 14 / 边缘 alpha 150 / 关闭按钮 18px 都是拍的值，且**没有做成设置项**（同 1.1 节偏差 2 的 YAGNI 理由）。请先看 `build-review/pin_chrome_before_after.png` 再定。另一个相关的悬空点：不悬停时边框是 `#818181` 中灰（`QColor(0,0,0,120)` 叠在窗口自身的调色板背景上 —— 贴图是**不透明**窗口，所以这个 alpha 混的是调色板而不是桌面），识别度是否够、要不要改成更醒目的静止态，也需要裁决。
7. **历史记录三个默认值是否合适** —— 条数上限 20、字节预算 200MB、复制后弹托盘提示，都是拍的值。200MB 是按「全屏 PNG 约 3–5MB、20 条 ≈ 100MB」倒推的，没实测过真实使用；「复制后弹提示」在连续截多张时会比较吵，可能需要改成只在首次或静默。
8. **序号 / 高亮的外观数值，以及八个工具图标的形状是否合适** —— 荧光笔默认 `#FDD835` / alpha 110 / 笔宽 18，序号徽标默认 `#E53935` / 直径 28，都是拍的值。请先看 `build-review/toolbar_eight_tools.png`（8 个图标逐工具）、`..._2x.png`（2 倍放大）与 `toolbar_icons_zoom.png`（6 倍最近邻拼版，看形状本身）再定。特别是 alpha 110 —— 调高会盖住底下的文字，调低则在深色背景上几乎看不见。图标**形状**（`src/overlay/ToolbarIcons.cpp`）同样是审美判断：探针能证明八个图标互不混淆、笔画宽一致，证明不了它们好看或符合直觉，这一条必须人看。另：原计划里的「半透明矩形高亮」**没有做**（理由见 1.3 偏差 1），若确实需要，正确做法是给矩形工具加「填充」开关，而不是新开一个工具。
9. **应用图标造型是否合适** —— 取景框（圆角蓝渐变底板 + 四段白色 L 角标）是照「16px 下要能读」这条约束设计的，`build-review/icon_preview.png` 是八个尺寸统一放大到 128px 高的最近邻拼版，请看一眼再定。若换造型，改 `tools/make_icon.cpp` 后跑 `bash tools/make_icon.sh` 重新生成即可，图标本身不进构建步骤。
10. **部署包的裁剪取舍** —— 剪掉了 `Qt6Network` / `tls` / `networkinformation` / `generic`（截图工具不开套接字），**以及 `Qt6Svg` + `iconengines/qsvgicon` + `imageformats/qsvg` + `qgif`**（工具图标改成代码绘制后，预留 SVG 的理由失效；剩下的理由是攻击面 —— 不该带一个 XML/SVG 解析器和一个从不读 `.gif` 的插件）。保留 `imageformats/`（仅 `qico` + `qjpeg`）与 `styles/`。第 `[4]` 节探针已双向断言，若日后真要用 SVG 图标，**先改 `tools/deploy.sh` 的裁剪清单**，探针会明确报出哪一条挡着。
11. **代码签名** —— 没有证书，任何用户都会看到 SmartScreen 警告。这不是脚本能解决的问题，需要决定是否购买证书。
12. **中文向导页** —— Inno Setup 不自带中文 `.isl`，自定义文案已是中文但内置页面（「下一步」等）仍是英文。需要把 `ChineseSimplified.isl` 放进 Inno 的 `Languages\` 目录并取消 `[Languages]` 里的注释。

