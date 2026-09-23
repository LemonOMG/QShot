# QShot 代码审查报告（第二轮 · 复审）

> **这是快照，不是待办清单。** 本文记录 2026-09-19 当时的发现与判断，**此后不再更新**。
> 第一轮那些项（P0/P1/P2/P3）现在什么状态，只看 [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。
>
> ⚠️ 下文第一节「上轮问题修复核对」的 ✅/❌ 是**当时的**结论。它在写下那天是对的，
> 但里面已经有多条过期 —— 例如它写着 P1-3/P1-4/P1-8 ❌ 未修，这些后来都修掉了。
> 不要拿那张表当现状。校验器：`python tools/verify_review_status.py`。

- 复审时间：2026-09-19
- 对比基线：`e291fd5`（首轮审查的代码状态）
- 复审范围：15 个改动文件 + 2 个新增文件（`src/core/PlatformFactory.{h,cpp}`）
- 验证方式：
  1. **编译验证**：`mingw32-make -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug` → `[100%] Built target qshot`；`qshot.exe`（16:51）时间戳晚于全部源码（16:46），确认当前代码可构建。
  2. **静态检查**：`g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow`，9 个 .cpp 全部通过，剩余 4 类警告（与首轮相同）。
  3. **Qt 行为探针（本轮关键手段）**：写了 4 个独立小程序实测 Qt 语义，避免靠记忆断言。
- 复审环境实测数据：**显示器 1 块**，`geometry=(0,0 1707x1067)`，`devicePixelRatio=1.50`。

> ⚠️ 环境说明：本机只有一块屏，且 DPR 为 **1.5（分数缩放）**。这意味着本轮 per-screen 重构的两个关键面——多屏行为、分数 DPR 下的取整——**在你的机器上无法完整验证**。下文相关结论已标注「需双屏实测」。

---

## 一、上轮问题修复核对

> **这张表是 2026-09-19 的判定，已经过期**（其中 P1-3/P1-4/P1-5/P1-6/P1-8、P0-3、P2-1、P2-2、P3
> 都在这之后修掉了）。保留它是因为它记录了「当时复核到了什么」；现状见
> [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

| 编号 | 问题 | 状态 | 说明 |
| --- | --- | --- | --- |
| P0-1 | 复制/保存丢失标注 | ✅ 已修 | `renderToImage()` 落地，复制与保存共用渲染路径；`saveRequested` 已接线。新问题见 N-3 |
| P0-2 | `Annotation` 未初始化成员 | ✅ 已修 | 全部给了默认值，`mosaicSize`/`fontSize` 也从 0 改成有意义的 16/18 |
| P0-3 | 右下角手柄命中区偏移 | ⚠️ **半修** | `bottomRight` 已修，但 `topRight` / `bottomLeft` 的同类偏移仍在（N-5） |
| P0-4 | 子面板残留 | ✅ 已修 | `hideToolbar()` 级联 + `hideSubPanel()` 提为 public |
| P0-5 | Undo 行为不一致 | ✅ 已修 | 工具栏信号直接连 `handleUndo`，两条路径收敛 |
| P0-6 | 混合 DPR 合成错误 | ✅ 已修 | 改为 per-screen 捕获 + 每屏一个 overlay，架构方向正确。新问题见 N-1 |
| P0-7 | 马赛克/字号面板白名单 | ✅ 已修 | 白名单删除，统一按 `currentTool_ != None` 判断 |
| P1-1 | 平台抽象泄漏 | ✅ 基本已修 | `PlatformFactory` 落地，UI 层不再 include Windows 头，CMake 条件编译。新问题见 N-2 |
| P1-2 | `windowRectAt` 忽略入参 | ⚠️ 半修 | 正常路径已正确使用入参并做逻辑↔物理换算；`edata.found == false` 时仍回落 `GetCursorPos` |
| P1-3 | 老式 SIGNAL/SLOT 连接 | ❌ 未修 | `ShotApplication.cpp:98` 原样保留，`dynamic_cast<QObject*>` 也在 |
| P1-4 | 单实例保护 + 热键无限重试 | ❌ 未修 | |
| P1-5 | 文本输入焦点未归还 | ❌ 未修 | `TextInputWidget.cpp` 完全未改动；且全项目只有一处 `setFocus()`，无任何 `activateWindow()` |
| P1-6 | 热键 VK 映射不完整 | ⚠️ 半修 | 补了 A-Z/0-9/F1-F24/Esc/Tab/Backspace/Return/Space；但 `default: vk = qtKey;` 让其余键静默产出错误 VK |
| P1-7 | 单击即复制 / 双击死代码 | ✅ 已修 | 单击改为「进入编辑」，`mouseDoubleClickEvent` 现在可达，语义统一 |
| P1-8 | `trayMenu_` 泄漏等 | ❌ 未修 | |
| P2-1 | 马赛克全图扫描 | ⚠️ **半修** | 加了包围盒裁剪，但包围盒是「累积笔迹」的，长笔迹退化回全图；且每次重画整条 path（N-7） |
| P2-2 | Idle 每次移动全屏重绘 | ⚠️ 半修 | 局部 `update(rect)` 生效，但 `hoverTimer` 仍是无参 `update()`；且刷新区域是硬编码的 700×700（N-6/N-8） |
| P2-3 | 双份全屏数据 | ✅ 已修 | 只保留 `backgroundImage_` |
| P2-4 | `setBaseImage` 重复分配三张大图 | ✅ 已修 | 改为懒分配 |
| P3 | 仓库残留 / 编译开关 / 文档 | ❌ 未修 | 见第三节 |

**修复率**：P0 7 项中 5 项完全修复、1 项半修、1 项修复但引入新问题；方向完全正确，改动质量高。

---

## 二、本轮新发现

### N-1（P1）多屏 overlay 的键盘焦点错位 —— 换屏后 Enter 无反应，Esc 会误删另一屏的标注

- 位置：`ShotApplication.cpp:140-159`（循环创建 overlay）、`SnapOverlay.cpp:673-694`（`keyPressEvent`）
- 根因：每块屏创建一个独立顶层窗口，循环里逐个 `show()`。Qt 会把焦点给**最后一个 show() 的窗口**，而代码里没有任何 `activateWindow()`（全项目 grep 只有 `TextInputWidget.cpp:43` 一处 `setFocus()`）。于是焦点固定落在「屏幕列表中最后一块屏」的 overlay 上，与用户实际操作哪块屏无关。
- 后果（双屏场景，键盘事件全部打到错误的那个 overlay）：
  - **Enter 复制**：`keyPressEvent` 要求 `state_ == Selected && !selectionRect_.isEmpty()`，而焦点所在的 overlay 是 Idle、选区为空 → 条件不成立 → **静默无反应**。用户在屏 A 框选后按 Enter，什么都不会发生。
  - **Esc**：焦点 overlay 的 `annotationLayer_` 为空 → 走 `close(); emit closed();` → `closed` 的 lambda 关闭**全部** overlay → **屏 A 上已经画好的标注被一并丢弃**。这是数据丢失。
  - **Ctrl+Z**：撤销的是焦点 overlay 的空图层，屏 A 上的标注纹丝不动 → 用户以为快捷键失灵。
- 说明：本机只有一块屏，**这个 bug 在你机器上复现不出来**。需要双屏实测确认。
- 建议：让 overlay 在获得鼠标交互时主动取焦——
  ```cpp
  // SnapOverlay::mousePressEvent 开头
  activateWindow();
  // 或者更稳妥：enterEvent 里判断鼠标所在屏并 activateWindow()
  ```
  更彻底的做法是把键盘处理上提到 `ShotApplication`（`QApplication::focusWidget()` 不可靠时，用事件过滤器 + 当前鼠标所在屏来路由按键），避免「焦点在哪块屏」这种隐式状态。

### N-2（P1）工厂返回 `nullptr` 未在调用方处理 —— 非 Windows 平台从「编译失败」变成「启动即崩」

- 位置：`PlatformFactory.cpp:11-33`、`ShotApplication.cpp:102`、`ShotApplication.cpp:138-141`
- 现象：非 Windows 下 `createGlobalHotkey()` / `createScreenCapture()` 都返回 `nullptr`，但调用方**不做任何判空**：
  - `ShotApplication.cpp:102`：`globalHotkey_->registerHotkey(...)` —— 而 `registerGlobalHotkeys()` 是在构造函数里调的，所以 **macOS/Linux 上进程一启动就空指针崩溃**。
  - `ShotApplication.cpp:141`：`capture->captureScreen(screen)` 同样未判空。
- 对比：改动前 CMake 无条件编译 `platform/windows/*`，非 Windows 上至少会**构建失败**（错误是可见的）；现在能构建成功、运行时崩溃，问题被推迟到了更糟的时机。
- 建议：二选一——
  - 最小改动：调用方判空并在托盘提示「当前平台不支持」，`registerGlobalHotkeys()` 直接 return。
  - 更干净：工厂返回 Null Object（`NoopScreenCapture` / `NoopGlobalHotkey`），调用方无需判空，也符合接口抽象的初衷。
  顺带：既然 `ROADMAP` 写着「兼容 macOS/Linux」，建议在 CI 里至少加一个 macOS/Linux 的编译 job，否则这类问题会持续潜伏。

### N-3（P1）保存功能的三个缺陷

- 位置：`SnapOverlay.cpp:58-80`
- 1）**保存失败静默**：`finalImage.save(filePath);` 的返回值被丢弃。磁盘无权限、路径不可写、扩展名不被 `QImage` 识别（例如用户手输 `shot.xyz`）时，程序什么都不说，用户以为保存成功了。
  ```cpp
  if (!finalImage.save(filePath)) {
      QMessageBox::warning(this, "保存失败", QString("无法写入 %1").arg(filePath));
  }
  ```
- 2）**对话框 parent 的选择**：原报告建议改成 `nullptr`，**这个建议是错的，已纠正**。overlay 是 `Qt::WindowStaysOnTopHint` 的置顶全屏窗口，若对话框不挂在它下面，原生对话框有可能被这块置顶窗口压在下面，用户会以为程序卡死。保留 `this` 作为 parent 才是正确做法（Qt 的 owned window 始终位于 owner 之上）。因此本条只保留「补扩展名」这一项实际改动。
- 3）**未指定默认扩展名**：`getSaveFileName` 没有传 `selectedFilter` 参数。Windows 原生对话框通常会自动补 `.png`，但非原生对话框（`DontUseNativeDialog`）不会，用户得到一个没有扩展名的文件。建议显式补扩展名。

### N-4（P1）`setDevicePixelRatio()` 触发深拷贝 —— 每次重绘都在拷贝整屏位图【实测证据】

这是本轮最有价值的技术发现，且有实测数据支撑。

- 位置：`SnapOverlay.cpp:140-142`、`AnnotationLayer.cpp:31-35`、`AnnotationLayer.cpp:134-140`
- 实测结论（探针程序输出）：
  ```
  pixmap dpr=1.50 -> toImage dpr=1.50  preserved=1        ← QPixmap::toImage() 保留 DPR
  before: a shares with img = 1                            ← 赋值是浅拷贝（隐式共享）
  after setDevicePixelRatio: a shares with img = 0         ← 调用后发生 detach（深拷贝）
  ```
- 问题代码：
  ```cpp
  // SnapOverlay.cpp:140  —— 每次 paintEvent 都执行
  QImage scaledBg = backgroundImage_;
  scaledBg.setDevicePixelRatio(dpr_);   // ← 触发深拷贝
  painter.drawImage(0, 0, scaledBg);
  ```
  由于 `QPixmap::toImage()` **已经**把 DPR 带过来了（`backgroundImage_.devicePixelRatio() == dpr_`），这两行是**完全冗余的**，代价却是每次重绘都 memcpy 一整屏位图。本机单屏 2560×1600×4B ≈ **16.4 MB/次**；而 Idle 状态下鼠标移动会持续触发 `update()`，等于每移动一次就拷 16MB。
  同样的模式还在 `AnnotationLayer::paint()`：`scaledMosaic.setDevicePixelRatio(dpr_)` —— 一旦画过马赛克，此后每次重绘都深拷贝整张 `mosaicLayer_`（选区物理尺寸，4K 选区约 33MB）。
- 实测还确认了 `QPainter::drawImage(0, 0, img)` **会遵循图片自身的 DPR**（8×8 @DPR2 的源画到 16×16 目标，覆盖了 16 个像素），所以删掉那两行不会改变渲染结果。
- 建议：
  ```cpp
  // SnapOverlay::paintEvent —— 直接删掉两行，只留：
  painter.drawImage(0, 0, backgroundImage_);

  // AnnotationLayer —— 在懒分配处一次性设好 DPR，绘制时直接用：
  mosaicLayer_.setDevicePixelRatio(dpr_);   // 建的时候设一次
  ...
  p.drawImage(0, 0, mosaicLayer_);           // paint() 里不再拷
  ```
  注意 `scanLine()` 索引的是物理像素，不受 DPR 影响，所以这个改动是安全的。
  `renderToImage()` 里同样的 `result.setDevicePixelRatio(dpr_)` 只发生在复制/保存时（一次），可以保留，但也可以顺手改成依赖 `copy()` 已保留的 DPR。

### N-5（P1）`topRight` / `bottomLeft` 手柄命中区仍然偏移（P0-3 只修了一个）

- 位置：`SnapOverlay.cpp:389-392`
- 现状对比：
  | 手柄 | 命中区构造 | 是否居中 |
  | --- | --- | --- |
  | TopLeft | `- QPoint(m,m)` | ✅ 居中 |
  | TopRight | `- QPoint(m,0)` | ❌ 垂直方向偏移 8px（偏向选区内） |
  | BottomLeft | `- QPoint(0,m)` | ❌ 水平方向偏移 8px（偏向选区内） |
  | BottomRight | `- QPoint(m,m)` | ✅ 居中（本次已修） |
  | Top / Bottom / Left / Right | 手写 center 偏移 | ✅ 居中 |
- 后果：右上角、左下角两个手柄的可拖拽区域整体向内偏了 8px，从角外侧起拖会命中失败（表现为「光标已经变成斜向箭头了，但按下去没进入 Resizing」）。
- 建议：统一成 `角点 - QPoint(m, m)`，四个角用同一套写法，避免以后再漏。

### N-6（P1）提交了草稿注释 + 硬编码的刷新区域魔法数

- 位置：`SnapOverlay.cpp:586-595`
- 问题 1 —— 注释里带着思考过程的残留，`Wait` 开头那句明显是写到一半的自我对话，不该进版本库：
  ```cpp
  // Also need to update hover rect if it changes, but hoverTimer handles that via a full update().
  // Wait, hoverTimer does update() which is fine (runs at 30ms).
  ```
- 问题 2 —— 刷新区域硬编码 `350` / `700`：
  ```cpp
  QRect oldMagRect(oldMousePos.x() - 350, oldMousePos.y() - 350, 700, 700);
  ```
  放大镜盒子的实际尺寸是运行时算出来的（`boxWidth = qCeil(magLogicalW)`，`boxHeight = finalMagSize + padding*2 + textHeight`，见 `paintEvent` 里 246-267 行），当前值约 156×~210，700 是「拍脑袋取够大」。一旦以后调大字号、加一行信息（比如显示窗口标题），或者 DPR 变成 1.0/2.0，350 就不够了 → 出现**放大镜残影**。
- 建议：把盒子的尺寸与位置计算抽成一个 `QRect magnifierRectFor(const QPoint&) const`，`paintEvent` 和 `mouseMoveEvent` 共用同一个函数，彻底消除魔法数。

### N-7（P2）马赛克「增量」不彻底：包围盒是累积的，长笔迹退化为全图

- 位置：`AnnotationLayer.cpp:211-218`
- 现状：`path` 由 `a.points` 的**全部**点构成，`path.boundingRect()` 因此覆盖**整条笔迹**的累积范围。而 `updateMosaic()` 在每次 `mouseMoveEvent` 里都被调用，`a.points` 在不断增长 → 包围盒随笔迹变长而变大，一条横跨屏幕的笔迹最终等价于全图扫描，P2-1 的性能问题只是被推迟，没有被解决。
- 另外 `p.drawPath(path)` 每次也重画整条笔迹，仍是 O(笔迹长度)。
- 真正的增量做法：只处理**本次新增线段**的邻域——
  ```cpp
  const QPoint& from = a.points[a.points.size() - 2];
  const QPoint& to   = a.points.last();
  QRect dirty = QRect(from, to).normalized().adjusted(-a.mosaicSize, -a.mosaicSize,
                                                       a.mosaicSize,  a.mosaicSize);
  ```
  包围盒取 `dirty` 而不是 `path.boundingRect()`，掩膜也只画 `from→to` 这一段。这样单次代价与笔迹长度无关，恒定为 O(mosaicSize²)。

### N-8（P2）`hoverTimer` 仍是无参 `update()`，局部刷新的收益被抵消

- 位置：`SnapOverlay.cpp:37-45`
- 现象：`hoverTimer_` 的回调里只要 hover 窗口发生变化就调用无参 `update()`（全屏重绘）。而鼠标在窗口之间移动时 hover 变化相当频繁，于是「局部刷新」在 Idle 状态下几乎退化回全屏重绘。上面 N-6 那句草稿注释其实已经承认了这一点。
- 建议：hover 变化时也做局部刷新——`update(oldHoverRect.united(newHoverRect).adjusted(-2,-2,2,2))`（外扩 2px 覆盖虚线边框）。

### N-9（P3）其余细节

| 位置 | 问题 |
| --- | --- |
| `SnapOverlay.h:34` | 构造参数名仍是 `virtualGeometry`，但现在语义是「单块屏的几何」，命名误导；建议改 `screenGeometry` |
| `ShotApplication.cpp:144,157` | `setGeometry(screen->geometry())` 调了两次（构造函数里已经设过），冗余 |
| `ShotApplication.cpp:129` | `currentOverlays_` 只判 `isEmpty()`，未清理已失效的 `QPointer`。若某个 overlay 未发 `closed` 就被销毁，列表里留下空指针 → 按热键会「什么都不发生」，需要按第二次。建议先 `removeIf([](const QPointer<SnapOverlay>& p){ return p.isNull(); })` 再判断 |
| `AnnotationLayer.cpp:26-45` vs `131-156` | `paint()` 与 `renderToImage()` 的「画马赛克 + 画矢量标注」逻辑完全重复，只有坐标系不同（一个要 translate，一个不要）。建议抽 `void drawContent(QPainter&) const` 供两者调用，避免以后改一处漏一处 |
| `AnnotationLayer.cpp:26` / `134` | 两个函数对坐标系的约定不同（`paint` 传的是未裁剪的选区矩形并自行 translate；`renderToImage` 传的是已裁剪的图，不 translate）。建议在头文件注释里写清楚，这是最容易踩的坑 |
| `WinGlobalHotkey.cpp:56-57` | `#ifndef MOD_NOREPEAT / #define` 写在函数体内部，虽然合法但不规范，建议移到 include 之后、文件作用域 |
| `WinGlobalHotkey.cpp:63` | `default: vk = qtKey;` 让未映射的键（如方向键、`Home`、`PageUp`，Qt 值是 `0x0100xxxx`）静默产出错误 VK，`RegisterHotKey` 失败后调用方只能看到「热键被占用」。建议 `default: qWarning() << ...; return false;` |
| `WinWindowDetector.cpp:55-59` | `struct EnumData { QString name; MONITORINFOEXW mi; bool found; } edata;` —— `edata.mi` 是未初始化的 POD（只设了 `cbSize`）。当前逻辑不会读到未 `found` 的分支，但应写成 `EnumData edata{};` 明确清零 |
| `WinWindowDetector.cpp:27,33` | 仍是 `GetClassNameA` + `fromLocal8Bit`、`GetWindowLong`；建议改 `GetClassNameW` + `fromWCharArray`、`GetWindowLongPtrW`（64 位下更严谨） |
| `IScreenCapture.h:5` | 核心接口头 include 了重量级的 `<QScreen>`，前向声明 `class QScreen;` 即可 |
| `SnapOverlay.cpp:711` | `copyToClipboard` 里 `selectedPixmap.setDevicePixelRatio(dpr)` 是冗余的（`fromImage` 已从 `finalImage` 继承了 DPR） |

---

## 三、仍未处理的 P3 项（首轮已提，本轮未动）

> **标题里的「仍未处理」是 2026-09-19 的说法。** 这一节的五项，现状（逐条见
> [`REVIEW_STATUS.md`](REVIEW_STATUS.md) 的 P3 行）：
>
> - 仓库残留 —— **已清**（`main.cpp`/`Main.qml`/`build_output.txt`/`err.txt`/`out.txt` 均不存在）
> - `CMakeLists.txt` 未开 `-Wall -Wextra` —— **已开**（`target_compile_options`，构建零警告）
> - 无测试、无 CI —— **仍未做**
> - `docs/ARCHITECTURE.md` / `TASKS.md` 描述失配 —— **已消失**（两个文件都已不存在，问题随之作废）
> - `.workbuddy-ai/` 未跟踪 —— **已纳入版本控制**

- 仓库残留：根目录 `main.cpp`（Qt Quick 模板，`loadFromModule("QShot","Main")`）、`Main.qml`、`build_output.txt`（里面还留着一次**失败**的构建记录，容易误导）、空的 `err.txt` / `out.txt`。
- `CMakeLists.txt` 仍未开启 `-Wall -Wextra`。当前实测存在 4 类警告：`-Wshadow`（`SnapOverlay.cpp:236` 的 `currentSelection` 遮蔽）、未使用参数（`ToolbarWidget.cpp:339` 的 `isAction`）、`MONITORINFOEXW` 部分初始化（`WinWindowDetector.cpp:66,102`）。开启后这类问题下次会自己暴露。
- 无测试、无 CI。`AnnotationLayer`（坐标/DPR 换算、马赛克增量）和 `PlatformFactory` 都是纯逻辑，适合 QtTest 覆盖。
- `docs/ARCHITECTURE.md` 仍描述不存在的 `capture`/`settings` 模块与 `IAutoStart` 接口；类关系图与现状（`PlatformFactory` 才是装配点）不符。`TASKS.md` 仍停在 Phase 0。
- `.workbuddy-ai/` 目录目前是未跟踪状态，建议加进 `.gitignore`（或明确提交，别悬着）。

---

## 四、已验证「无问题」的两处（避免误报）

本轮我怀疑过两个点，实测后确认**代码是对的**，记录在此以免后续重复排查：

1. **`rebuildMosaicCache()` 对可能为 null 的 `QImage` 调 `fill()`** —— 懒分配改造后 `mosaicLayer_`/`mosaicMask_` 可能为 null（例如只画了矩形标注就右键重置）。实测 `QImage().fill(...)` 是安全的空操作（保持 `isNull()==1`，不崩溃），且后续 `updateMosaic()` 会补上分配。逻辑自洽。
2. **保存的 PNG 是否带 DPI 元数据导致显示尺寸异常** —— 实测 `setDevicePixelRatio()` **不会**修改 `dotsPerMeter`，保存出的 PNG 恒为 96 DPI、尺寸为物理像素（200×100 存进去就是 200×100），DPR 不落盘。所以保存的是原生分辨率图，行为正确，不存在「被查看器按 144 DPI 缩放」的坑。

另外确认：`QScreen::grabWindow(0)` 返回的确实是**该屏**的物理尺寸位图且带该屏 DPR（实测 1707×1067 @1.5 → 2560×1600 @1.5），per-screen 改造的核心假设成立。
（细节：Qt 用的是截断而非四舍五入，1707×1.5=2560.5 → 2560；而代码里用 `qRound` 得到 2561，靠 `intersected()` 兜住。全屏框选时右下角会有 1px 的理论误差，实际被裁掉，无可见影响。）

---

## 五、建议修复顺序

> 当时排的顺序，只作历史记录 —— N-1~N-9 现在的状态见 [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

1. **N-4**（删两行 + 挪一次 `setDevicePixelRatio`）—— 收益最大、风险最低，每次重绘省一次整屏 memcpy。
2. **N-5 / N-6 / N-9 前三条** —— 纯局部改动，顺手做掉。
3. **N-3**（保存失败提示 + 对话框 parent）—— 用户可感知的健壮性提升。
4. **N-2**（工厂判空或 Null Object）—— 决定「跨平台」这个目标要不要当真；若当真，顺带加 CI 编译 job。
5. **N-1**（焦点路由）—— 需要双屏环境验证，建议排在能实测之后再动。
6. **N-7 / N-8**（真正的增量马赛克 + hover 局部刷新）—— 性能收尾。
7. **P3 清理**：删残留文件、开 `-Wall -Wextra`、补最小测试集。

---

## 六、本轮已落地的修复

> 本节记录第二轮**当时做了什么**，属于历史；各项现在什么状态见
> [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

以下改动已完成，并通过构建 + 启动冒烟测试。

### 已修复

| 编号 | 改动 | 文件 |
| --- | --- | --- |
| **N-4** | 删除 `paintEvent` 里 `QImage scaledBg = backgroundImage_; scaledBg.setDevicePixelRatio(dpr_);` 两行，改为直接 `painter.drawImage(0, 0, backgroundImage_)`（其 DPR 由 `toImage()` 带过来）。`AnnotationLayer::paint()` / `renderToImage()` 同样去掉每帧的 `scaledMosaic` 拷贝，改为在懒分配时给 `mosaicLayer_` 设一次 `dpr_` | `SnapOverlay.cpp`、`AnnotationLayer.cpp` |
| **N-5** | `hitTestHandle()` 重写：抽出 `hits(anchor)` lambda，8 个手柄统一为「锚点 ± m 的 2m×2m 方块」，消除 `topRight`/`bottomLeft` 的单轴偏移 | `SnapOverlay.cpp` |
| **N-6** | 新增 `MagnifierLayout` 结构 + `magnifierLayout(mousePos)` 私有方法，`paintEvent` 与 `mouseMoveEvent` 共用同一份几何计算；`350`/`700` 魔法数消失；删除草稿注释；放大镜调参统一为文件作用域 `kMag*` 常量 | `SnapOverlay.h/.cpp` |
| **N-6 附带** | 放大镜宽度改用**最坏情况文本宽度**（`RGB: (255, 255, 255)` / `POS: -99999, -99999`），面板不再随采样到的颜色值忽宽忽窄——既更稳，也使该矩形可安全用作重绘区域 | `SnapOverlay.cpp` |
| **N-8** | `hoverTimer` 改为局部重绘 `update(oldHover ∪ newHover)`（外扩 2px 覆盖虚线边框），不再整屏重绘 | `SnapOverlay.cpp` |
| **N-3** | 新增 `saveToFile()`：检查 `QImage::save()` 返回值，失败弹 `QMessageBox` 并**保留 overlay**（不销毁用户已做的标注）；补扩展名（按 `selectedFilter` 判定 `.png`/`.jpg`）；`PicturesLocation` 为空时回落 `homePath()`。对话框 parent 保留 `this`（见上文纠正） | `SnapOverlay.h/.cpp` |
| **N-9 部分** | 构造参数 `virtualGeometry` → `screenGeometry`；抽出 `physicalSelectionRect()`，`copyToClipboard()` 与 `saveToFile()` 共用，消除重复的 DPR 换算 | `SnapOverlay.h/.cpp` |

### 验证结果

- **编译**：`mingw32-make -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug` → `[100%] Built target qshot`，改动文件全部重新编译并链接通过。
- **静态检查**：`-Wall -Wextra -Wshadow` 仅剩 1 条既有警告（`SnapOverlay.cpp:242` 的 `currentSelection` 遮蔽，非本次引入）。
- **启动冒烟**：`timeout 6 ./qshot.exe` → `exit_code=124`，即进程完整存活 6 秒未崩溃，托盘、`PlatformFactory`、热键注册路径均正常。
- **渲染等价性**：`drawImage(0,0,image)` 遵循图片自身 DPR 已由探针实测确认（8×8 @DPR2 覆盖 16×16），因此去掉那两行不改变绘制结果。

### 仍未处理（建议下一轮）

> **这是 2026-09-19 的清单，已经全部处理完**（N-1/N-2/N-7/N-9/P1-3/P1-4/P1-5/P1-8/P3 逐条见
> [`REVIEW_STATUS.md`](REVIEW_STATUS.md)）。保留原文以记录当时还欠什么。

- **N-1** 多屏键盘焦点（需双屏环境验证）
- **N-2** 工厂 `nullptr` 判空 / Null Object（决定「跨平台」是否当真）
- **N-7** 马赛克真正的增量包围盒（当前仍是累积笔迹范围）
- **N-9 其余**：`setGeometry` 重复调用、`currentOverlays_` 空指针清理、`AnnotationLayer` 绘制逻辑去重、`WinWindowDetector` 的 `EnumData` 清零与 W/Ptr 系列 API、`IScreenCapture.h` 的 `<QScreen>` 前向声明
- **P1-3/P1-4/P1-5/P1-8**（首轮遗留）：老式 SIGNAL/SLOT、单实例保护、文本输入焦点、`trayMenu_` 泄漏
- **P3**：仓库残留文件、CMake 警告开关、测试、文档同步
