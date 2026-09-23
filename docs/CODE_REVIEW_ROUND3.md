# QShot 代码审查报告（第三轮）

> **这是快照，不是待办清单。** 本文记录 2026-09-20 当时的发现与判断，**此后不再更新**。
> R3-1~R3-9 与 U-1~U-3 现在什么状态，只看 [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。
>
> 本文是这套契约的**起因**：正文与「建议修复顺序」里带着当时的状态判定，第四节那张表
> 曾经写着 N-2 ❌ 未修、P1-5 ⚠️ 待处理 —— 而这些在写完之后都修掉了，表却没跟上，
> 于是第三轮之后又发生过一次「照着一份过期清单干活」。现在状态只在索引里，
> 由 `python tools/verify_review_status.py` 强制一致。

- 审查时间：2026-09-20
- 基线提交：`32b1eed`（工作区干净，无未提交改动）
- 审查范围：`src/` 全部 24 个文件、`CMakeLists.txt`、`docs/`、仓库卫生
- 审查方式：
  1. `mingw32-make -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug` → `[100%] Built target qshot`
  2. `g++ -fsyntax-only -Wall -Wextra -Wshadow -Wunused` 静态检查（2 条警告，见 R3-8）
  3. **写 Qt 探针程序实测语义**（`build-review/probe_*.cpp`），用证据而非推理下结论 —— 本轮头号问题是靠探针挖出来的，另有一整类疑似 bug 被探针证伪

---

## 一、头号发现：焦点链路断裂（P0 × 2，均已实测复现）

这三个问题同源，建议一次改完。根因是：**`SnapOverlay` 是普通 `QWidget`，从未在任何时机把键盘焦点/窗口激活状态拿回来**，而工具栏、子面板、文本框都是 `Qt::Tool` 顶层窗口，`show()` 会打断焦点链。

### R3-1（P0）文本标注框根本无法输入文字

`TextInputWidget::startInput()` 结尾只做了 `show(); setFocus();`（`TextInputWidget.cpp:42-43`）。

**实测**（`probe_focus5.cpp`，overlay 处于激活状态）：

```
[1] overlay active (no toolbar yet)              focusWindow=OVERLAY   focusWidget=null
[2] te->show()+setFocus() while overlay active   focusWindow=OVERLAY   focusWidget=null
     type 'h' -> toPlainText()=""
```

原因：`setFocus()` 只设置**窗口内部**的焦点控件，不会激活该窗口。overlay 的窗口仍然是 focusWindow，字符被送到 `SnapOverlay::keyPressEvent`，落到 `QWidget::keyPressEvent` 后被丢弃。**用户敲不进任何字**，文本标注功能实质不可用。

**修复已实测可行**（`probe_focus4.cpp` 第 4 阶段）：

```
[4] + textInput->activateWindow()   focusWindow=TEXTINPUT  focusWidget=QTextEdit
     type 'h' -> toPlainText()="h"      ← 打字成功
```

```cpp
// TextInputWidget::startInput() 结尾
show();
activateWindow();   // ← 必须：setFocus() 不会激活窗口
setFocus();
```

### R3-2（P0）选完区之后，Esc / Enter / Ctrl+Z 全部静默失效

`showToolbar()` 里 `toolbar_->show()`（`SnapOverlay.cpp:105-109`）会让**整个进程没有焦点窗口**。

**实测**（`probe_focus2.cpp` / `probe_focus4.cpp`）：

```
[1] overlay shown + activateWindow      focusWindow=OVERLAY   Esc → overlay.got = 1   ← 正常
[2] toolbar->show()  (showToolbar)      focusWindow=none      Esc → 被丢弃
[3] toolbar->hide()                     focusWindow=none      Esc → 被丢弃（不会自动恢复）
[4] overlay->activateWindow()           focusWindow=OVERLAY   Esc → overlay.got = 1   ← 只有显式激活能救
```

后果：用户拖出选区后，键盘就彻底死了 —— Esc 关不掉、Enter 复制不了、Ctrl+Z 撤销不了，只能靠鼠标点工具栏按钮或双击。**这与屏幕数量无关**，单屏必现。

> 这条同时解释了第二轮 N-1 描述的「Enter 静默失效、Esc 误伤另一屏」——现象是真的，但根因不是多屏，而是 `showToolbar()` 本身。

### R3-3（P1）`Qt::Tool` 子窗口的按键**不会**向父窗口传播

「父窗口兜住快捷键」这条路走不通，已实测：

```
tool->parentWidget() == overlay ? YES
[Esc]     accepted by tool = 0   overlay.got = 0
[Ctrl+Z]  accepted = 0           overlay.got = 0
[Enter]   accepted = 0           overlay.got = 0
```

即使 `parentWidget()` 确实指向 overlay，键盘事件在窗口边界处就停了（窗口型子控件不参与父链传播）。

**结论**：修 R3-1/R3-2 不能靠"补一个 parent 关系"，必须
1. 面板 `show()` 之后显式 `overlay->activateWindow()`；
2. 纯展示型面板（`ToolbarWidget`、`SubPanelWidget`，二者默认 `focusPolicy` 就是 `Qt::NoFocus`）加 `Qt::WindowDoesNotAcceptFocus`，从源头不参与激活竞争；
3. 需要键盘输入的面板（文本框）自己 `activateWindow() + setFocus()`，并在 `finish()/cancel()` 之后把激活状态还给 overlay；
4. 兜底：把 Esc / Enter / Ctrl+Z 改成 overlay 上的 `QShortcut`（`Qt::ApplicationShortcut` 上下文），彻底不依赖焦点归属。

---

## 二、其他新发现

### R3-4（P1）多屏下「局部坐标 / 全局坐标」混用

单屏原点恰好是 `(0,0)`，所以下面每一处都"碰巧正确"，一旦换到副屏全部失效：

| 位置 | 问题 |
| --- | --- |
| `SnapOverlay.cpp:156,229-233,676` | `hoverWindowRect_` 来自 `IWindowDetector::windowRectAt()`，`WinWindowDetector.cpp:124-128` 返回的是**全局**逻辑坐标（显式加了 `screen->geometry().x()`）；overlay 却把它当**窗口局部**坐标用于绘制和 `selectionRect_ = hoverWindowRect_`。副屏上悬停框画到 overlay 外，点选后选区落在屏外，`setBaseImage()` 与背景求交后为空 |
| `ToolbarWidget.cpp:262` | `updatePosition(selectionRect_.normalized(), rect())` 传的是局部坐标，但 `move()` 对顶层窗口是**桌面全局**坐标 → 副屏上工具栏错位 |
| `ToolbarWidget.cpp:277` | `if (targetY < 0)` 用 0 当屏幕上边界，副屏原点非 0 时判断失效 |
| `TextInputWidget.cpp:40` | `move(pos)`，`pos` 是 overlay 局部坐标 → 副屏上文本框跑到别处 |
| `SnapOverlay.cpp:95` | `textInput_->pos()` 是**全局**窗口坐标，减去 `selectionRect_.topLeft()`（局部）→ 只有单屏原点为 (0,0) 才成立 |

**建议**：定一个约定写进 `AnnotationLayer` 那种头文件注释里 —— 要么 overlay 内部一律用局部坐标、所有跨窗口调用先 `mapToGlobal()`；要么 detector 返回值统一 `- screenGeometry.topLeft()` 归一化后再用。

### R3-5（P1）右键取消马赛克笔迹后，马赛克**仍然会出现在复制/保存的图里**

`updateMosaic()` 在鼠标拖动过程中就把马赛克块直接写进了 `mosaicLayer_` / `mosaicMask_`（`AnnotationLayer.cpp:191-309`）。而右键取消只清 `activeAnnotation_.points`（`SnapOverlay.cpp:523-531`），**不调 `rebuildMosaicCache()`**，该 annotation 也从未进入 `annotations_`。

于是：
- 屏幕上：那一笔马赛克留在画面上（视觉上"取消失败"）
- 导出时：`renderToImage()` 无条件 blit `mosaicLayer_`（`AnnotationLayer.cpp:143-145`）→ **用户明确取消掉的马赛克被打进最终成品**，且没有任何提示

（Esc 全清、Undo 走的是 `rebuildMosaicCache()`，所以只有右键这条路径中招。）

```cpp
// SnapOverlay.cpp 右键取消分支
state_ = OverlayState::Selected;
activeAnnotation_.points.clear();
annotationLayer_.rebuildMosaicCache();   // ← 补这一行
update();
```

更彻底的做法：马赛克也改成「提交时才落层」，`updateMosaic()` 只更新一个临时的预览层。

### R3-6（P2）除 Idle 外所有状态都是整屏重绘

> **已修（2026-09-23，性能项收尾轮）。** 状态以 [`REVIEW_STATUS.md`](REVIEW_STATUS.md) 为准。
> 下面保留发现时的描述。落地内容与实测：
>
> - `paintEvent` 改为按 `event->rect()` 裁剪，背景 `drawImage` 与遮罩都只画脏区；
>   暗色遮罩从 `fullPath.subtracted(holePath)` 换成 4 个 `fillRect`（上/下/左/右，互不重叠 —— 遮罩色是半透明的，
>   重叠会叠深），既快又天然可按脏区裁剪。
> - 四个状态各自给出脏区：Annotating = 笔迹新旧包围盒；Moving / Resizing = 选区新旧 chrome（边框 + 8 个手柄 + 尺寸徽标）；
>   Dragging = 选区新旧 chrome ∪ hover 轮廓 ∪ 放大镜面板。Idle / Selected 原有的局部重绘保留，并改用同一套辅助函数。
> - Annotating 下**笔画类工具（画笔/高亮/马赛克）只取最新一段**，否则整条路径的包围盒会随拖动线性增长，
>   等于把 R3-6 在这三个工具上原样还回去。判据 `isStrokeTool()`。
> - 顺带修掉一个反向开销：马赛克图层被清空后仍会被每帧 blit（见 `CODE_REVIEW.md` P2-4 现状）。
> - **探针抓到一个真缺陷**：马赛克的脏区边距原先取一个块宽，不够。`updateMosaic()` 先外扩一个块宽、
>   再把块网格对齐到图像原点，于是首块可以再往前一格 —— 实测差异出现在笔画起点上方 24px，而边距只允许 17px。
>   改为 `2 * mosaicSize`。这条只可能被实测抓到（详见 `ROADMAP.md` §3.7.5）。
>
> 实测（`probe_partial_repaint`，119 checks / 0 failures；屏 900×700 的探针窗口）：
>
> | 状态 | 改前 | 改后脏区 |
> | --- | --- | --- |
> | Idle（放大镜跟随） | 100% | 264×204 = 8.55% |
> | Selected（点击输入提示） | 100% | 188×82 = 2.45% |
> | Annotating（拖矩形） | 100% | 117×57 = 1.06% |
> | Annotating（画笔，逐段） | 100% | 149×109 = 2.58%（每段恒定） |
> | Annotating（马赛克，逐段） | 100% | 207×167 = 5.49%（每段恒定） |
> | Dragging（拉选区） | 100% | 476×353 = 26.67% |
> | Moving（整体平移） | 100% | 249×176 = 6.96% |
> | Resizing（拖手柄） | 100% | 289×216 = 9.91% |
>
> 探针的核心断言不是「脏区等于我手算的矩形」（那只是把公式抄进测试），而是
> **「只重绘脏区后的整帧，与整帧全量重绘逐像素相同」** —— 脏区少一个像素就会失败，
> 多几个像素则通过；再单独断言「屏幕远端角落没被重绘」来保证它确实变小了。
>
> 尚未做的：脏区仍是**包围盒**。Dragging 下选区与放大镜面板离得远，包围盒是两者的并集（26.67%），
> 而真实变化只有「选区边框 + 新旧选区之间的环带 + 面板」。要再压下去得改用 `QRegion` 并让 `paintEvent` 按
> `event->region()` 裁剪，收益约 2 倍、只作用于该状态，本轮按 YAGNI 不做。

`mouseMoveEvent` 的 Annotating / Moving / Resizing / Dragging 四个分支都是无参 `update()`（`:572, :578, :596, :603`）→ 每次鼠标移动重绘整屏：整屏 `drawImage` 背景 + `QPainterPath::subtracted()` 全屏布尔运算 + 全部标注重绘。本机物理分辨率 2560×1600，4K 下拖矩形/画笔会明显掉帧。第二轮的 N-8 只优化了 Idle 的放大镜面板，这四个状态没覆盖。

顺带：`paintEvent` 里用 `fullPath.subtracted(holePath)` 做暗色遮罩（`:161-170`），其实画 4 个矩形（上/下/左/右）更快，也天然适合做局部重绘。

### R3-7（P2）工具栏没有 `leaveEvent`，悬停高亮会残留

`ToolbarWidget.h:47-51` 只声明了 press/release/move。鼠标移出工具栏后 `hoverPos_` 停在最后一个按钮上，该按钮保持高亮，直到鼠标再次移入。补 `leaveEvent` 置 `hoverPos_ = QPoint(-1,-1)` 并 `update()`。

### R3-8（P2）死代码 / 遮蔽 / 未用参数（静态检查可复现）

```
src/overlay/SnapOverlay.cpp:242:19: warning: declaration of 'currentSelection' shadows a previous local [-Wshadow]
      shadowed declaration is here: SnapOverlay.cpp:152:11
src/overlay/ToolbarWidget.cpp:339:94: warning: unused parameter 'isAction' [-Wunused-parameter]
```

- `SnapOverlay.cpp:642-653` `mouseReleaseEvent` 的 Moving 分支 if/else 两个分支体**完全相同**（注释写着「Clicked inside existing selection without moving」，行为却没区别）→ 死分支，删掉一半
- `ToolbarWidget.cpp:339` `drawButton()` 的 `isAction` 参数从未使用 → 从签名里删掉
- `SnapOverlay.cpp:747` `selectedPixmap.setDevicePixelRatio(dpr_)` 冗余（`QImage::fromImage` 已继承 DPR）—— 第二轮 N-9 提过，仍在

### R3-9（P3）

> **已全部解决（2026-09-23 逐条复核确认）。** 状态以 [`REVIEW_STATUS.md`](REVIEW_STATUS.md) 为准。
> 下面保留发现时的描述，逐条对照如下：
>
> | 发现时的问题 | 现状 | 落在哪 |
> | --- | --- | --- |
> | `SnapOverlay.cpp` 每次鼠标按下 `qDebug()` 打印完整状态 | ✅ 已删（`SnapOverlay.cpp` 连 `<QDebug>` 都不再包含）。其余文件的 `<QDebug>` 保留 —— 它们喂的是 `qWarning()`，属于错误路径该有的输出 | M6 清理 |
> | `setBaseImage()` 在 `physicalRect` 为空时**不更新** `baseImage_`，保留上一张选区图 | ✅ 已修：显式 `baseImage_ = QImage()`，注释写明「留着上一张裁剪会让后续 `updateMosaic()` 采错像素」 | M6 清理 |
> | `magnifierLayout()` 每次都构造 `QFontDatabase::systemFont()` + `QFontMetrics`（每次 mouseMove 调 2 次 + paintEvent 1 次） | ✅ 已修：`panel::fixedFontMetrics()` / `uiFontMetrics()` 是函数内静态 | M4（见 `ROADMAP.md` §1.3） |
> | `resources/` 空目录、无 `qt_add_resources` → 托盘图标是 32×32 蓝方块；`Alt+A` 硬编码 | ✅ 已修：`resources/` 有 `qshot.ico`（8 个尺寸）+ `resources.qrc`，托盘取 `:/icons/qshot.ico`；`Alt+A` 变成 `Settings.cpp` 的 `kDefaultHotKey`（是**默认值**，不再是散落的字面量） | M6 图标 + 部署 |
> | `ShotApplication.cpp` 两处重复 `setGeometry(screen->geometry())` | ✅ 已删：全仓只剩 `SnapOverlay` 构造函数里那一处 | M6 清理 |
>
> **这一节的教训与 P1-3/P1-6 完全一样**：审查文档的表格会过期，**先复核再动手**。六条里有四条是在别的轮次里顺手修掉的，
> 文档却一直挂着「仍未处理」—— 而「以为还有活要干」和「以为活已经干完」这两种误读的代价是一样的。

- `SnapOverlay.cpp:464` 每次鼠标按下都 `qDebug()` 打印完整状态；`ShotApplication` 里也有多处 —— 生产噪音
- `AnnotationLayer.cpp:170-173` `setBaseImage()` 在 `physicalRect` 为空时**不更新** `baseImage_`，保留上一张选区图 → 后续 `updateMosaic()` 拿错底图（配合 R3-4 可达）
- `SnapOverlay.cpp:363-365` `magnifierLayout()` 每次都构造 `QFontDatabase::systemFont()` + `QFontMetrics`，而它每次 mouseMove 被调 2 次、paintEvent 再调 1 次 → 缓存成成员变量
- `resources/` 是**空目录**，CMake 里也没有 `qt_add_resources` → 托盘图标仍是 32×32 蓝色方块（`ShotApplication.cpp:46-48`），热键文案里的 `Alt+A` 也是硬编码字符串
- `ShotApplication.cpp:144` 与 `:157` 重复 `setGeometry(screen->geometry())`（构造函数已设过）

---

## 三、已实测**排除**的误报（重要，下一轮别重复排查）

这两条我一开始都怀疑是 bug，写探针实测后证伪，记录在此：

### 1. `QPainter` 在 `QImage` 上**会**按该图的 `devicePixelRatio` 缩放

```
[T1] fillRect(0,0,10,10) on a dpr=1.5 target
  pixel(14,14) = #ff0000     ← 10 逻辑单位 × 1.5 = 15 物理像素，缩放生效
[T4] 逻辑坐标 (10,10)-(20,20)、pen 16 画进 60px@1.5 的 mask
     white bbox = (0,0)-(45,45)   ← 与「逻辑 0..30 × 1.5」吻合
```

所以 `AnnotationLayer::renderToImage()`（把逻辑坐标画到物理图上）和 `updateMosaic()`（把逻辑笔画画进 `strokeMask`，再按物理像素索引）**坐标系都是正确的**，不是 bug。

### 2. `mosaicLayer_` 的 blit 与底图严格 1:1 对齐

```
[T3] src 30px@1.5 → dst 60px@1.5：只覆盖 30px
```

源图按自身 DPR 折算成 20 逻辑单位，目标再 ×1.5 → 正好回到 30 物理像素。同尺寸同 DPR 的两层叠加**不会错位**，马赛克没有"缩小到 2/3"的问题。

（第二轮已确认的 `QImage::setDevicePixelRatio()` 触发 detach、`QImage().fill()` 对 null 图安全这两条仍然有效。）

---

## 四、上一轮遗留项状态

> **这张表是 2026-09-20 的判定。** 保留它是因为它记录了「当时复核到了什么」，
> 但其中至少 N-2、P1-5 两行已经过期（写完之后就修掉了）。现状见
> [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

| 编号 | 项 | 状态 |
| --- | --- | --- |
| N-1 | 多屏 overlay 键盘焦点错位 | ⚠️ **本轮找到真根因**（R3-2：`toolbar_->show()` 让 focusWindow 变 null，与屏幕数无关），升级为 P0 |
| N-2 | `PlatformFactory` 返回 `nullptr` 未判空 | ❌ 未修（当时；**后由 M6 清理修掉**，现状见索引） |
| N-3 | 保存功能三缺陷 | ✅ 已修 |
| N-4 | `setDevicePixelRatio` 触发深拷贝 | ✅ 已修 |
| N-5 | 手柄命中区偏移 | ✅ 已修（`hits()` lambda 统一 8 个手柄） |
| N-6 | 草稿注释 + 硬编码刷新区域 | ✅ 已修（`MagnifierLayout` + `kMag*` 常量） |
| N-7 | 马赛克增量包围盒（累积笔迹退化全图） | ✅ 已修（M6 清理，见 `ROADMAP.md` 3.6.1）—— **探针顺带抓到一个既有 bug**：掩码按未裁剪的逻辑原点平移，触到选区边缘的笔画整幅马赛克错位 |
| N-8 | `hoverTimer` 无参 `update()` | ✅ 已修（Idle 局部重绘） |
| N-9 其余 | 工厂/焦点/坐标等细节 | ⚠️ 部分：`setGeometry` 重复调用、`currentOverlays_` 未清理失效 `QPointer`、`AnnotationLayer` 两处绘制逻辑未去重、`WinWindowDetector` 的 `EnumData` 未清零 + 仍是 `GetClassNameA`/`GetWindowLong`、`IScreenCapture.h` 仍 include `<QScreen>` |
| P1-3 | 老式 `SIGNAL/SLOT` + `dynamic_cast<QObject*>` | ✅ **早已修掉，是这张表没跟上**：现用 `&IGlobalHotkey::hotkeyPressed`，全文无 `dynamic_cast<QObject*>`（M6 清理时复核） |
| P1-4 | 单实例保护 + 热键无限重试 | ✅ 已修（M6 清理）—— 重试早已有上限（`kMaxHotkeyRetries`），本轮补上单实例保护：`core/ISingleInstance.h` + `WinSingleInstance` + `PlatformFactory::createSingleInstance()` |
| P1-5 | 文本输入焦点未归还 | ⚠️ **本轮定位并升级为 R3-1（P0）** —— R3-1 随后已修 |
| P1-6 | `default: vk = qtKey;` 静默产出错误 VK | ✅ 主体早已修掉（`virtualKeyFor()` 显式映射表，未覆盖返回 0；`MOD_NOREPEAT` 已用宏）；本轮只剩 `hotkeyId_ = 1001` 魔数，已改 `constexpr int kHotkeyId` |
| P1-8 | `trayMenu_` 泄漏（`new QMenu()` 无 parent） | ✅ 已修（M6 清理）—— `trayMenu_` 已是 `unique_ptr`；顺带把 `globalHotkey_` 的三份所有权表达收敛成 `unique_ptr` + 无父对象 |
| P3 | 仓库残留（根 `main.cpp`、`Main.qml`、`build_output.txt`、空 `err.txt`/`out.txt`） | ✅ 已清理 |

---

## 五、建议修复顺序

> 当时排的顺序，只作历史记录 —— R3-1~R3-9 现在的状态见 [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

1. **R3-1 + R3-2 + R3-3**（同一根因，一次改完，收益最大）—— 面板 `show()` 后 `overlay->activateWindow()`；文本框 `activateWindow() + setFocus()`；纯展示面板加 `Qt::WindowDoesNotAcceptFocus`；Esc/Enter/Ctrl+Z 改用 `Qt::ApplicationShortcut` 兜底
2. **R3-5** 右键取消马赛克 —— 一行 `rebuildMosaicCache()`，但关系到"成品图正确性"
3. **R3-4** 坐标空间统一（决定多屏能不能用）
4. **R3-6** 局部重绘（决定 4K 下流不流畅）—— **已在 2026-09-23 的性能项收尾轮完成**，见本节 R3-6 的状态块与 `ROADMAP.md` §3.7
5. **R3-7 / R3-8 / R3-9** 清理与收尾
6. 回头处理 N-2、N-7、P1-4、P1-8、P3 —— **已在 M6 清理时全部做完**（N-2 工厂判空、N-7 马赛克增量、P1-4 单实例、P1-8 所有权收敛、P3 仓库残留）

---

## 六、探针程序（可复现）

留在 `build-review/`（该目录已被 `.gitignore` 覆盖）：

| 文件 | 验证内容 |
| --- | --- |
| `probe_dpr_paint.cpp` | `QPainter` 在 `QImage` 上的 DPR 语义（T1~T4） |
| `probe_focus_propagation.cpp` | `Qt::Tool` 子窗口按键是否上传父窗口 |
| `probe_focus2.cpp` | `toolbar->show()/hide()/activateWindow()` 对 focusWindow 的影响 |
| `probe_focus3.cpp` / `probe_focus4.cpp` | 文本框 `setFocus()` 失效与 `activateWindow()` 修复 |
| `probe_focus5.cpp` | overlay 激活状态下打开文本框仍无法输入 |
| `probe_fix.cpp` | 修复方案预演（含「点击仍能送达 `WindowDoesNotAcceptFocus` 面板」） |
| `probe_zorder.cpp` | 激活 overlay 后工具栏/子面板的 Win32 z 序 |
| `probe_fromimage.cpp` | `QPixmap::fromImage()` 是否保留 DPR |
| `probe_activate.cpp` / `probe_verify*.cpp` | 前台归属表征（结论见下） |

编译方式（链接 `-lQt6Core -lQt6Gui -lQt6Widgets`，运行前把 `D:/Qt/6.11.2/mingw_64/bin` 加入 PATH）。

---

## 七、本轮已落地的修复

> 本节记录第三轮**当时做了什么**，属于历史；各项现在什么状态见
> [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

按第五节顺序落地了第 1、2 步及第 5 步的清理项（共 7 个文件，+117 / −34 行）。

### 已修复

| 编号 | 改动 | 文件 |
| --- | --- | --- |
| **R3-1** | `startInput()` 在 `setFocus()` 之前加 `activateWindow()` | `TextInputWidget.cpp` |
| **R3-2** | 新增 `SnapOverlay::reclaimKeyboardFocus()`（= `activateWindow()`），在 `showToolbar()` / `hideToolbar()` / `handleToolSelection()` / 文本提交或取消后 / `colorChanged` / `sizeSettingChanged` 之后调用 | `SnapOverlay.h/.cpp` |
| **R3-3** | 同上：不依赖父窗口兜快捷键，改为主动取回激活 | `SnapOverlay.cpp` |
| **R3-2 附带** | `ToolbarWidget` 与 `SubPanelWidget` 加 `Qt::WindowDoesNotAcceptFocus`（纯展示面板不再参与激活竞争） | `ToolbarWidget.cpp` |
| **R3-2 附带** | `onCaptureTriggered()` 里 `overlay->show()` 后补 `activateWindow()`；顺手删掉与构造函数重复的 `setGeometry()` | `ShotApplication.cpp` |
| **R3-5** | 右键取消马赛克笔迹时调 `annotationLayer_.rebuildMosaicCache()` | `SnapOverlay.cpp` |
| **R3-5 附带** | 新增 `TextInputWidget::cancelInput()`，右键重置选区时丢弃半成品文本（否则编辑框会停在空画布上继续抢键） | `TextInputWidget.h/.cpp`、`SnapOverlay.cpp` |
| **R3-7** | `ToolbarWidget::leaveEvent()` 重置 `hoverPos_` | `ToolbarWidget.h/.cpp` |
| **R3-8** | 消除 `currentSelection` 变量遮蔽；删掉 `drawButton()` 未使用的 `isAction` 参数；合并 `mouseReleaseEvent` 的重复死分支；删掉 `copyToClipboard()` 里冗余的 `setDevicePixelRatio()` | `SnapOverlay.cpp`、`ToolbarWidget.h/.cpp` |
| **R3-9 部分** | 删除 `mousePressEvent` 的 `qDebug()` 及不再需要的 `<QDebug>`；`setBaseImage()` 在裁剪矩形为空时清空 `baseImage_` | `SnapOverlay.cpp`、`AnnotationLayer.cpp` |

### 验证结果

| 项 | 结果 |
| --- | --- |
| 构建 | `mingw32-make` → `[100%] Built target qshot` |
| 静态检查 | `-Wall -Wextra -Wshadow -Wunused` **零警告**（改前有 2 条） |
| 启动冒烟 | `timeout 8 ./qshot.exe` → `exit_code=124`（存活 8 秒未崩，托盘/工厂/热键路径正常） |
| 文本输入（确定性，3 次复现） | 改前 `show()+setFocus()`：`toPlainText()` 恒为空；改后 `activateWindow()+setFocus()`：`"hi"` 正确落入编辑框 |
| 焦点被清空（确定性） | `toolbar->show()` 后 `QGuiApplication::focusWindow() == nullptr`，Esc 被丢弃 |
| z 序（无回归） | `overlay->activateWindow()` 后工具栏/子面板仍在 overlay **之上**（Windows owned-window 行为），不会藏到遮罩后面 |
| 点击送达（无回归） | `Qt::WindowDoesNotAcceptFocus` 的面板仍能收到鼠标按下事件 |

### 未能确定性验证的部分（需人工确认）

`probe_verify*.cpp` 想断言"重新激活后 Esc 一定到达 overlay"，但**从终端启动的探针永远拿不到系统前台**（`GetForegroundWindow()` 始终是终端），Windows 会拒绝 `SetForegroundWindow()`，因此 `focusWindow` 相关断言在探针里时好时坏，同一份代码不同轮次结果不一致。真实运行中用户是通过热键/鼠标触发的，进程持有前台权限，不存在这个限制。

**建议人工花 10 秒确认**：按 Alt+A → 拖出选区 → 按 Esc（应立即关闭）；再选一次 → 点文字工具 → 在选区内点击 → 打字（应能输入）→ 点别处提交 → 按 Enter（应复制并关闭）。

### 仍未处理

R3-4（多屏坐标空间统一，需先定约定 + 双屏实测）、R3-6（四个状态的局部重绘）、R3-9 剩余项，以及上一轮遗留的 N-2、N-7、P1-3/4/6/8、P3 仓库残留。

> **这份清单当时是对的，现在全部处理完了** —— 逐条状态见 [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。
> 其中 R3-9 的六条在 2026-09-23 复核时确认**全部早已解决**，只是报告没跟上。

## 八、UI 可用性修复（用户反馈驱动）

用户反馈三条：① 马赛克不该有颜色配置；② 文字没有尺寸和颜色可选；③ 提示用户"可以输入文字"的线索太淡，根本不知道点一下就能打字。

第 ② ③ 条其实不是"功能缺失"，而是**功能存在但用户看不见**——这正是可用性缺陷，比崩溃更难发现，所以按缺陷处理。

### 8.1 马赛克去掉颜色行（U-1）

`SubPanelWidget` 里颜色行原本对所有工具无条件绘制。改为 `hasColorRow()` 返回 `currentTool_ != AnnotationType::Mosaic`，马赛克只留尺寸档。面板宽度随之从 398px 自动收缩到 174px。

`ToolSettings` 结构体仍保留 `color` 字段、马赛克记录层也仍忽略它——没有连带删字段，因为删除会波及序列化/默认值，属于纯 YAGNI 之外的改动，收益为零。

### 8.2 文字尺寸/颜色变得可读（U-2）

根因：文字工具的尺寸档原本用三个裸 `"T"` 字形表示，字号差异在 12px 的面板里几乎看不出，用户完全无法判断这是"字号选择"。改为 `paintSizeGlyph()` 直接渲染 **14 / 18 / 24** 数字（颜色档不变）。马赛克/直线则渲染递增的方块/圆点。

顺带修掉一个真 bug：`showSubPanel()` 先定位后 `updateSelection()`，导致**切换工具后面板尺寸不更新**（内容变了但面板没重算）。改为先 `updateSelection()` 再定位。同时把布局计算抽成单一 `computeLayout()`（返回 `Layout{sizeChips, colorDots, separator, hasColors, contentWidth}`），消除绘制与命中测试两处各算一遍的重复——原实现里这两处不一致就是这类错位的温床。

### 8.3 输入提示从"看不见"到"显眼"（U-3）

调查过程值得记一笔，因为最初的猜测是错的：

1. 原实现靠 `QTextEdit` 的 stylesheet `border: 1px solid #666` 来暗示输入框。**实测发现它一个像素都不画**——`probe_qss_border.cpp` 遍历所有变体（含加 `WA_TranslucentBackground`、改颜色、改宽度）均为 0 帧像素。
2. 换 `QPainter(this)` 在 `paintEvent` 里自绘边框，**仍然看不见**——因为 `QTextEdit` 的 **viewport 子控件盖在上面**，画在 widget 上的内容被完全遮住。
3. 最终方案：画在 **`viewport()`** 上。`probe_frame.cpp` 验证从 0 帧像素变为 970 帧像素。

落地三处可见性增强，都经离屏渲染目视确认（`render_text_editor.cpp`）：

| 增强 | 实现 |
| --- | --- |
| 绿色虚线边框 | `paintEvent` 在 `viewport()` 上 `drawRect`，2px `DashLine` `#1AAD19` |
| 占位符 | `QPalette::PlaceholderText` 设为标注色 alpha 150，`setPlaceholderText("输入文字…")`；`minimumEditorWidth()` 预留 12px 余量防止占位符被裁 |
| 光标形状 | 文字工具悬停在选区内时 overlay 切 `Qt::IBeamCursor` |
| 区域徽标 | `state_ == Selected && 选区包含鼠标 && textToolArmed()` 时在光标下方画绿色圆角徽标「点击输入文字」；随鼠标移动只重绘新旧徽标的并集 |

徽标触发条件抽成 `textToolArmed()` / `shouldShowTextHint()` / `textHintRect()` 三个小查询，避免把这些判断散进 `paintEvent`。

### 8.4 验证结果（本轮增量）

| 项 | 结果 |
| --- | --- |
| 构建 | `[100%] Built target qshot` |
| 静态检查 | `-Wall -Wextra -Wshadow -Wunused` **零警告** |
| 启动冒烟 | `timeout 8 ./qshot.exe` → `124` |
| 空输入框渲染 | 绿色虚线框 + 浅色「输入文字…」占位符清晰可辨（`text_editor_empty.png`，167×53） |
| 有内容渲染 | 虚线框 + 实色文字（`text_editor_typed.png`，138×53） |
| 文字子面板 | 显示 `14 / 18 / 24` 三档数字 + 颜色点（`panel_text_sub.png`） |
| 马赛克子面板 | 仅尺寸方块，无颜色行，宽度 174px（`panel_mosaic_sub.png`） |
| 矩形子面板 | 无回归（`panel_rect_sub.png`） |

### 8.5 仍未处理

R3-4、R3-6、R3-9 剩余项，以及上一轮遗留的 N-2、N-7、P1-3/4/6/8、P3 仓库残留，均未变动。

> **「均未变动」是 2026-09-20 的说法。** 这些项后来全部收尾（R3-4 需真双屏人工确认，
> 不是代码问题）。逐条状态见 [`REVIEW_STATUS.md`](REVIEW_STATUS.md)。

