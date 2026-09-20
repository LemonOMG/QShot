# QShot 代码审查报告

- 审查时间：2026-09-19
- 审查范围：`src/`（22 个文件）、`CMakeLists.txt`、`docs/`、仓库根目录残留
- 验证方式：
  - 实际编译：`mingw32-make -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug` → `[100%] Built target qshot`（Qt 6.11.2 MinGW 64bit，通过）
  - 静态检查：`g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow`（8 个 .cpp，无错误）
  - 逐文件人工走查（状态机、坐标/DPR 换算、信号槽连接完整性）
- 结论：**可编译、可运行，主流程（框选 → 标注 → 复制）骨架完整，但存在 7 个会被用户直接感知的功能缺陷，以及平台抽象泄漏、性能、仓库卫生三类结构性问题。**

问题分级：P0 = 功能错误/UB，必须修；P1 = 架构与健壮性；P2 = 性能；P3 = 工程规范。

---

## P0 必须修复

### P0-1 复制/保存丢失全部标注（数据丢失级）

- 位置：`SnapOverlay.cpp:662-677`（`copyToClipboard`）、`SnapOverlay.cpp:49-62`（信号连接）
- 现象：画完箭头/马赛克/文字后按 Enter、双击或点「复制」，剪贴板里只有原始背景，标注全部消失。
- 根因：`copyToClipboard()` 直接从 `backgroundPixmap_` 裁剪，完全没走 `annotationLayer_`；`AnnotationLayer` 也没有提供「渲染到 QImage」的接口。另外 `ToolbarWidget::saveRequested` 在 `SnapOverlay` 里**根本没有连接**，点「Save」无任何反应。
- 建议：给 `AnnotationLayer` 增加
  ```cpp
  QImage renderToImage(const QImage& basePhysical) const; // 按 dpr 合成马赛克+矢量标注
  ```
  复制与保存都走同一条渲染路径；保存用 `QFileDialog::getSaveFileName` + PNG。这样也天然修掉「马赛克不进剪贴板」的问题。

### P0-2 `Annotation` 存在未初始化成员（未定义行为）

- 位置：`Annotation.h:12-18`，使用点 `SnapOverlay.cpp:68-73`，消费点 `AnnotationLayer.cpp:50`
- 现象：文本标注路径构造 `Annotation a;` 时只设置了 `type/color/fontSize/points/text`，`lineWidth` 从未赋值；而 `paintAnnotation()` 第一行就是 `QPen pen(a.color, a.lineWidth, ...)`——即使 Text 用不到笔宽，也会读取未初始化内存（值可能是负数或极大值，笔宽非法时渲染结果不确定）。
- 建议：结构体一律给默认成员初始化器，消灭这类隐患：
  ```cpp
  struct Annotation {
      AnnotationType type = AnnotationType::None;
      QColor color = Qt::red;
      int lineWidth = 4;
      int mosaicSize = 16;
      int fontSize = 18;
      QVector<QPoint> points;
      QString text;
  };
  ```

### P0-3 右下角缩放手柄命中区域偏移，与其余 7 个手柄不一致

- 位置：`SnapOverlay.cpp:361-374`
- 现象：右下角手柄的命中区整体偏了 `(m, m)`，落在选区**外部**（`QRect(r.bottomRight(), QSize(2m,2m))` 从角点向右下扩展），而其它 7 个手柄都是 `角点 - QPoint(m,m)` 居中命中。用户会发现右下角拖动很难命中，且手柄外一点点的位置反而能拖。
- 建议：
  ```cpp
  if (QRect(r.bottomRight() - QPoint(m, m), QSize(2*m, 2*m)).contains(pos)) return Handle::BottomRight;
  ```

### P0-4 隐藏工具栏时子面板（颜色/粗细面板）残留

- 位置：`SnapOverlay.cpp:88-90`（`hideToolbar`）、`ToolbarWidget.cpp:289-293`（`hideSubPanel`）
- 现象：`hideToolbar()` 只调 `toolbar_->hide()`，没有级联隐藏 `subPanel_`。而 `subPanel_` 是独立的顶层窗口（`SubPanelWidget` 构造时 parent 传的是 `nullptr`，见 `ToolbarWidget.cpp:50`）。于是开始拖动/缩放选区时，工具栏消失但颜色面板还浮在屏幕上。
- 建议：`hideToolbar()` 里同时 `toolbar_->hideSubPanel()`（把该方法提为 public，或让 `ToolbarWidget::hideEvent` 统一处理）。

### P0-5 Undo 按钮与 Ctrl+Z 行为不一致

- 位置：`SnapOverlay.cpp:50-53`（工具栏路径）vs `SnapOverlay.cpp:97-101`（`handleUndo`）
- 现象：工具栏 Undo 按钮的 lambda 只做 `undo()` + `update()`，不刷新 `undoEnabled`；撤销到空之后按钮仍是可点状态，点了没反应。而 Ctrl+Z 走 `handleUndo()`，会正确 `setUndoEnabled(!isEmpty())`。同一功能两条路径两套逻辑，`handleUndo()` 实际上被写出来却没被工具栏复用。
- 建议：两处都收敛到 `handleUndo()`：
  ```cpp
  connect(toolbar_, &ToolbarWidget::undoRequested, this, &SnapOverlay::handleUndo);
  ```

### P0-6 混合 DPR 多屏捕获用 maxDpr 统一合成 → 画面模糊 + 内存峰值过高

- 位置：`WinScreenCapture.cpp:14-42`
- 现象：把所有屏幕画进一张 `DPR = maxDpr` 的画布。DPR=1 的显示器抓到的位图被 `drawPixmap` 按逻辑坐标绘制到 2x 画布上，等于被**放大 2 倍再重采样**，最终呈现必然模糊；屏幕 DPR 差异越大越明显。
- 附带：`virtualGeometry.size() * maxDpr` 在 3×4K@200% 下是 11520×4320×4B ≈ **190MB**，`SnapOverlay` 又用 `background.toImage()` 复制一份（`SnapOverlay.cpp:19`），峰值接近 400MB。
- 建议：按屏分别保留「物理位图 + 逻辑矩形」，合成时按各自 DPR 绘制；或直接改为「每个 QScreen 一张 overlay 窗口 + 一张 pixmap」，只在复制时按需裁剪（这也顺带解决 P2-3 的双份内存问题）。

### P0-7 选中椭圆/箭头/马赛克/文字后子面板不弹出，字号与马赛克粒度无法调整

- 位置：`ToolbarWidget.cpp:431-435`（只对 Rectangle/Pen 调 `showSubPanel()`）vs `ToolbarWidget.cpp:269-287`（`showSubPanel` 已支持 Mosaic/Text 的尺寸）
- 现象：`SubPanelWidget::currentSizes()` 专门为 Mosaic 返回 `kMosaicSizes`、为 Text 返回 `kFontSizes`，说明设计上是支持的；但 `handleToolClick` 的白名单只放了矩形和画笔，导致马赛克块大小、字号永远无法在首次选中时设置。`updatePosition()` 里「只要 currentTool_ != None 就 showSubPanel」的逻辑又和它矛盾。
- 建议：删掉白名单，统一 `if (currentTool_ != AnnotationType::None) showSubPanel(); else hideSubPanel();`。

---

## P1 架构与健壮性

### P1-1 平台抽象被绕过，`core/` 接口形同虚设

- 位置：`ShotApplication.cpp:8-10`、`ShotApplication.cpp:94`、`ShotApplication.cpp:136`；`SnapOverlay.cpp:10-12`、`SnapOverlay.cpp:31-33`
- 现象：`IScreenCapture` 没有任何地方以接口类型引用（`WinScreenCapture capture;` 直接用具体类）；`ShotApplication` 直接 `#include "platform/windows/WinGlobalHotkey.h"`；UI 层 `SnapOverlay` 直接 `#include` 并 `new WinWindowDetector()`——平台代码渗透到 UI 层。`ARCHITECTURE.md` 里画的 `ShotApplication --> IScreenCapture` 关系在代码中并不存在。
- 另外 `CMakeLists.txt:19-24` 无条件编译 `platform/windows/*`，`CMakeLists.txt:38` 无条件链接 `dwmapi`，但 `ROADMAP` 目标是「Windows 优先，兼容 macOS/Linux」——当前配置在 macOS/Linux 上**直接构建失败**。
- 建议：加一个薄工厂（`PlatformFactory::createScreenCapture()/createHotkey()/createWindowDetector()`），`main.cpp` 组装后注入 `ShotApplication`；CMake 用 `if(WIN32) ... elseif(APPLE) ...` 分支，`dwmapi` 放进 `if(WIN32)`。注意 `.agents/rules/q-shot.md` 同时要求「不要为单一实现创建抽象接口」，这两条规则需要先对齐——要么补上第二实现，要么承认接口是为跨平台预留、在文档里写清楚。

### P1-2 `IWindowDetector::windowRectAt()` 的参数被忽略

- 位置：`WinWindowDetector.cpp:51-53`
- 现象：接口注释写的是「返回 globalPos 下顶层窗口的逻辑矩形」，实现却完全不看入参，改用 `GetCursorPos()`。调用方 `SnapOverlay.cpp:40` 传的是 `lastHoverPos_`（30ms 前的鼠标位置），实际取的是"当前"鼠标位置——参数与行为不一致，接口契约失效。
- 建议：删掉 `GetCursorPos`，直接用入参换算；如果确实依赖光标，就把接口签名改成无参。

### P1-3 老式字符串信号槽连接 + 多余的 `dynamic_cast`

- 位置：`ShotApplication.cpp:96-100`
- 现象：`IGlobalHotkey` 本身就继承 `QObject`，`dynamic_cast<QObject*>` 是多余的；`SIGNAL()/SLOT()` 宏是运行时字符串匹配，拼错不会编译报错。
- 建议：
  ```cpp
  connect(globalHotkey_, &IGlobalHotkey::hotkeyPressed, this, &ShotApplication::onCaptureTriggered);
  ```

### P1-4 无单实例保护 + 热键失败无限重试

- 位置：`ShotApplication.cpp:103-118`
- 现象：用户双击两次图标，第二个实例的 `RegisterHotKey` 必然失败，于是每 5 秒重试一次，**永不停止**（无退避、无次数上限），同时不断刷新托盘 tooltip 文案。第二个实例还会常驻托盘。
- 建议：`main.cpp` 里用 `QSharedMemory`/命名互斥量做单实例；重试加指数退避 + 上限（例如 5 次后停止并只保留「重新注册热键」菜单项）；进阶是允许用户在设置里改快捷键。

### P1-5 焦点管理：文本输入结束后快捷键可能失效，且文本缺少显式提交键

- 位置：`SnapOverlay.cpp:430-431`、`TextInputWidget.cpp:63-71`
- 现象：`textInput_` 是独立顶层窗口（`Qt::Tool`），`startInput()` 里 `setFocus()` 会抢走焦点。`finish()` 后只是 `hide()`，没有把焦点交还给 overlay（也没有 `activateWindow()`），此时 Esc / Enter / Ctrl+Z 可能不再生效。另外 `keyPressEvent` 只处理了 Escape，Enter 是插入换行——**提交只能靠失焦**，如果焦点没拿到，文本就永远提交不了。
- 建议：`finish()`/`cancel()` 之后让 overlay `activateWindow(); setFocus();`；补 `Ctrl+Enter` 提交、`Escape` 取消的显式语义。

### P1-6 热键实现只支持 A-Z/0-9，且用了魔数

- 位置：`WinGlobalHotkey.cpp:33-46`
- 现象：`UINT vk = qtKey;` 依赖「Qt::Key 值与 VK 码相同」，这只对 A-Z/0-9 成立（`Qt::Key_A == 0x41 == VK_A`）。换成 F1、方向键、`~` 等会静默得到错误 VK，`RegisterHotKey` 失败后返回 false，调用方只看到「热键被占用」，无法区分真正原因。另外 `fsModifiers |= 0x4000; // MOD_NOREPEAT` 手写魔数（`MOD_NOREPEAT` 在 `winuser.h` 中已有定义），`hotkeyId_ = 1001` 也是硬编码。
- 建议：写一个 `qtKeyToVirtualKey()` 显式映射表，未覆盖的键返回失败并给出明确日志；改用 `MOD_NOREPEAT` 宏；`hotkeyId_` 用常量或 `GlobalAddAtom` 方案。

### P1-7 单击即复制导致双击分支不可达

- 位置：`SnapOverlay.cpp:578-583` vs `SnapOverlay.cpp:629-637`
- 现象：未锁定状态（无标注、未选工具）下，选区内单击 release 就 `copyToClipboard(); close();`，窗口随即销毁，`mouseDoubleClickEvent` 永远等不到第二次点击 → 死代码。而且「有标注时单击无反应、双击才复制」与「无标注时单击就复制」两套语义不一致，用户很容易误触退出。
- 建议：二选一。推荐统一为「单击选中/进入编辑，双击或 Enter 复制」，或者去掉双击分支、把单击复制明确写进 UI 提示。

### P1-8 资源管理小问题

- `ShotApplication.cpp:52`：`trayMenu_ = new QMenu();` 无父对象，`QSystemTrayIcon::setContextMenu` 不接管所有权 → 退出时泄漏（轻微）。给 `this` 作父对象即可。
- `ShotApplication.cpp:33-41`：`globalHotkey_` 已 `new WinGlobalHotkey(this)`（父对象是 `this`），析构里又手动 `delete`——不会双重释放（QObject 析构会把自己从父对象子列表摘除），但属于冗余的所有权表达，建议只保留一种。

---

## P2 性能

### P2-1 马赛克每次鼠标移动都全图扫描（最严重的性能问题）

- 位置：`AnnotationLayer.cpp:172-252`，调用点 `SnapOverlay.cpp:442-444`、`SnapOverlay.cpp:512-517`
- 现象：`updateMosaic()` 在每次 `mouseMoveEvent` 里被调用，而它每次都：
  1. 新建一张与选区等大的 `Grayscale8` 掩膜（4K 选区 ≈ 8MB 分配）；
  2. 用 `QPainter` 把整条笔迹重画一遍；
  3. **遍历全图所有块**（`for y ... for x ...`），每块再扫 `bh*bw` 像素，最坏三轮 → 4K 下每次移动约 2500 万次像素操作。
  以 60Hz 鼠标事件计算，必然明显掉帧。
- 建议：只处理本次笔迹的**增量包围盒**（上一位置到当前位置的矩形外扩 `mosaicSize`），掩膜与 mosaicLayer_ 复用同一缓冲不再重建；再叠一个 ~16ms 的合并节流。这样单次代价从 O(W·H) 降到 O(笔迹面积)。

### P2-2 Idle 状态每次鼠标移动都全屏重绘

- 位置：`SnapOverlay.cpp:556-562`
- 现象：`Idle` 分支无条件 `update()`，触发整张虚拟屏背景位图重绘 + 放大镜重建（含 `QImage` 分配、`copy`、`scaled`）。多屏 4K 下每次移动都是几十 MB 的像素搬运。
- 建议：改为局部更新——`update(oldMagnifierRect.united(newMagnifierRect))`，背景部分用 `WA_OpaquePaintEvent` + 只在需要时重绘；放大镜的 `srcImage` 也可以复用成员缓冲而非每次 new。

### P2-3 同一份全屏数据存了两份

- 位置：`SnapOverlay.h:54-55`（`backgroundPixmap_` + `backgroundImage_`）
- 建议：只保留 `QImage`（`QPainter` 可直接画 `QImage`），或按需 `toImage()` 缓存，省掉一半常驻内存。

### P2-4 每次拖动/缩放结束都重新裁切三张大图

- 位置：`AnnotationLayer.cpp:134-160`（`setBaseImage`，每次 release 都调用）
- 现象：每次 `setBaseImage` 都重新分配 `baseImage_` / `mosaicLayer_` / `mosaicMask_` 三张选区大小的图。连续拖拽选区会持续抖动内存分配。当前逻辑上是安全的（有标注时会锁定选区、禁止移动缩放，不会出现标注错位），但可以按需分配——只在真正用到马赛克时才建 `mosaicLayer_/mosaicMask_`。

---

## P3 工程规范

| 项 | 说明 |
| --- | --- |
| 仓库残留 | 根目录 `main.cpp` + `Main.qml` 是 Qt Quick 模板（`qputenv("QT_IM_MODULE","qtvirtualkeyboard")`、`loadFromModule("QShot","Main")`），未参与构建、与 Widgets 方案冲突，应删除；`build_output.txt`、空的 `err.txt`/`out.txt` 也是遗留产物（`build_output.txt` 还记录了那次失败的构建，容易误导），建议删除并补进 `.gitignore` |
| 编译开关 | `CMakeLists.txt` 未开 `-Wall -Wextra`。实测已能发现 4 类警告（`-Wshadow` 的 `currentSelection` 遮蔽、`drawButton` 未使用参数 `isAction`、`MONITORINFOEXW` 部分初始化、`Annotation` 未初始化成员的隐患），建议 `target_compile_options(qshot PRIVATE -Wall -Wextra -Wshadow)` |
| 测试 | 无任何测试。`AnnotationLayer`（坐标换算、马赛克增量）、`WinWindowDetector`（逻辑/物理换算）都是纯逻辑，适合上 CTest + QtTest 做单元测试 |
| 文档 | `ARCHITECTURE.md` 描述了不存在的 `capture`/`settings` 模块与 `IAutoStart` 接口，类关系图与实现不符；`TASKS.md` 仍停在 Phase 0、`ROADMAP.md` 未反映 Phase 1/2 已部分完成 |
| 规则一致性 | `.agents/rules/q-shot.md` 的 YAGNI 条款（「不要为单一实现创建抽象接口」）与现存 3 个单实现接口（`IScreenCapture`/`IGlobalHotkey`/`IWindowDetector`）冲突，建议明确「跨平台预留」为例外并写进规则 |
| 捕获能力边界 | `WinScreenCapture` 走 `QScreen::grabWindow(0)`（GDI BitBlt），对部分 GPU 加速/分层窗口可能抓到黑图或丢帧，建议在真机上验证 Chrome 硬件加速、游戏、播放器等场景，必要时评估 Windows.Graphics.Capture |
| 托盘交互 | `ShotApplication.cpp:88` 注释写着「We could leave it enabled for manual click」，但代码把「截图」菜单项 `setEnabled(false)`。热键被占用时托盘本可提供手动截图入口，与注释意图相反，二选一即可 |

---

## 值得肯定的部分

- `OverlayState` 状态机（Idle/Dragging/Selected/Moving/Resizing/Annotating）划分清晰，各分支 return 明确，没有互相穿透。
- 放大镜实现相当考究：强制源区域宽高为奇数以保证真中心像素、`setDevicePixelRatio(1.0)` 避免 Qt 二次缩放、按 DPR 反算裁剪矩形——这类细节通常容易出错，这里处理得很扎实。
- 马赛克用「掩膜 + 块均值」增量累积，避免重复块被二次模糊，思路是对的（问题只在触发频率与扫描范围）。
- 标注坐标统一采用「相对选区左上角」的逻辑坐标，与 DPR 解耦，是正确的设计选择。
- 命名规范执行到位：无 `Q` 前缀类名、命名空间统一 `qshot`、平台接口统一 `I` 前缀，与项目规则一致。
- 全项目无第三方依赖，仅 Qt6 + dwmapi，符合 YAGNI 原则。

---

## 建议修复顺序（最小 diff 优先）

1. **P0-2 / P0-3 / P0-4 / P0-5**：4 处都是几行内的小改动，先修，风险最低。
2. **P0-1**：补 `AnnotationLayer::renderToImage()`，复制与保存共用（顺带落地「保存」功能）。
3. **P0-7 / P1-7**：交互语义对齐，改完立刻能感受到体验提升。
4. **P0-6 / P2-1 / P2-2**：性能与画质，建议合并到一次「渲染管线整理」中做。
5. **P1-1 / P1-2 / P1-3**：平台工厂 + 接口收敛，属于结构性重构，放在功能稳定之后。
6. **P3**：清理残留文件、开编译警告、补最小测试集，作为每次提交的守门员。
