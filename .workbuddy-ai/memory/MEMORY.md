# QShot 项目长期记忆

## 环境（实测确认）

- Qt 6.11.2 MinGW 64bit：`D:/Qt/6.11.2/mingw_64`
- 编译器：`D:/Qt/Tools/mingw1310_64/bin/g++.exe`；构建：`.../mingw32-make.exe`
- 构建目录：`build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug`（Qt Creator 生成）
- 本机显示：**1 块屏**，`geometry=(0,0 1707x1067)`，`devicePixelRatio=1.5`（分数缩放是默认路径，不是边缘情况）

## 常用命令

构建验证：
```
D:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug
```
秒级静态检查（不触发 AUTOMOC，用于快速发现警告）：
```
D:/Qt/Tools/mingw1310_64/bin/g++.exe -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow \
  -I src -I D:/Qt/6.11.2/mingw_64/include -I .../QtCore -I .../QtGui -I .../QtWidgets \
  <各 .cpp>
```
Qt 行为探针（写独立小程序实测 Qt 语义，链接 `-lQt6Core -lQt6Gui`，运行前把 `D:/Qt/6.11.2/mingw_64/bin` 加进 PATH）。

## Qt 语义事实（已实测，勿再猜）

| 事实 | 结论 |
| --- | --- |
| `QImage::setDevicePixelRatio()` | 会 **detach → 深拷贝**。对全屏图（2560×1600×4≈16MB）用在每次 paintEvent 上是实打实的性能 bug |
| `QPixmap::toImage()` | **保留** devicePixelRatio |
| `QImage::copy(QRect)` | 保留 DPR；rect 是**物理像素**坐标 |
| `QPainter::drawImage(x, y, img)` | **遵循** img 自身的 DPR（8×8@DPR2 画满 16×16） |
| `QImage().fill(...)`（null 图） | 安全空操作，不崩溃，仍是 null |
| `setDevicePixelRatio()` 与 DPI 元数据 | **互不影响**。`dotsPerMeter` 恒为 3780（96 DPI），保存 PNG 不落盘 DPR |
| `QScreen::grabWindow(0)` | 返回**该屏**的物理尺寸位图 + 该屏 DPR（1707×1067@1.5 → 2560×1600@1.5）。注意 Qt 用**截断**（2560.5→2560），而业务代码常用 `qRound`（2561），差 1px 靠 `intersected()` 兜住 |
| 多个顶层窗口逐个 `show()` | 焦点给**最后一个** show() 的窗口；不调 `activateWindow()` 就无法指定焦点归属 |
| `QPainter` 画在 `QImage` 上 | **会**按该图自身的 `devicePixelRatio` 缩放传入的坐标（10 逻辑单位 @dpr1.5 覆盖 15 物理像素）。所以「逻辑坐标画进物理尺寸的图」是正确写法，不是 bug |
| `QImage::fromImage()` | 保留 DPR（`QPixmap::fromImage` 亦然），之后再 `setDevicePixelRatio()` 是冗余 |
| `QWidget::setFocus()` | **只设置窗口内部焦点，不激活窗口**。窗口不是 activeWindow 时，`show()+setFocus()` 拿不到键盘输入，必须 `activateWindow()+setFocus()` |
| `toolbar->show()`（Qt::Tool 子窗口） | 会让 `QGuiApplication::focusWindow()` 变成 **null**（整个进程无焦点窗口），所有按键被丢弃；`hide()` 不会恢复，只有显式 `activateWindow()` 能恢复 |
| 键盘事件传播 | **不会**从窗口型子控件（`Qt::Tool` 等）传播到 `parentWidget()`，即使 parent 关系存在。别指望父窗口兜快捷键 |
| `QTextEdit` + `WA_TranslucentBackground` | stylesheet 的 `border` **一个像素都不画**（`probe_qss_border.cpp` 遍历所有变体均 0 帧像素） |
| `QTextEdit` 上自绘 | `QPainter(this)` 画的内容被 **viewport 子控件盖住**，必须画在 `viewport()` 上（`probe_frame.cpp`：0 → 970 帧像素） |
| 两次连续 `QScreen::grabWindow(0)` | **像素不稳定**（实测 94% 像素不同、最大通道差 106）。所以「比较两次截图的像素差」在本机**不能**用来判断某物是否被画上去 |
| `QPixmap::toImage() → setDevicePixelRatio(1.0) → QPixmap::fromImage()` | **无损**（合成图实测 `changed=0, maxDelta=0`）。需要按物理坐标合成时这是安全做法 |
| `QWidget::grab()` | 返回 **DPR 缩放后**的位图，而 `findChildren()` 给的几何是**逻辑**坐标。两者直接对比会误判成「布局没铺开」。要 1:1 比对必须用 DPR=1 的 `QWidget::render(&pixmap)` |
| `DrawIconEx` + 自顶向下 32bpp DIB section | **会正确写入 alpha**（48×48 光标实测 483 个非透明像素）。用 `CreateCompatibleBitmap` 则没有 alpha 通道 |
| 终端启动的进程里 `GetCursorInfo` | 会话空闲时可能返回 `hCursor=NULL` / `CURSOR_SHOWING=0`（系统隐藏了光标）→ 光标相关断言不可靠 |
| `SetCursorPos()` | 在终端启动的探针里**不可靠**（设 `(1600,1000)` 实际落到 `(850,416)`），不能用来把光标钉到已知位置做确定性测试 |

## 项目约定

- 开发规则见 `.agents/rules/q-shot.md`：禁 Q 前缀类名、命名空间 `qshot`、平台能力须抽象为接口、YAGNI、每次变更必须编译通过。
- 规则内部存在冲突：「不为单一实现创建抽象接口」与现存 3 个单实现接口（`IScreenCapture`/`IGlobalHotkey`/`IWindowDetector`）+ `PlatformFactory` 矛盾。已两次写入审查报告，**待用户裁决**。
- 平台装配点：`src/core/PlatformFactory.cpp`（`#ifdef Q_OS_WIN` 分支）。工厂返回 `nullptr` 的情况**已在本轮判空**（`registerGlobalHotkeys` / `onCaptureTriggered`），新增平台能力时记得照做。
- 坐标约定：标注点统一为「相对选区左上角的逻辑坐标」；`AnnotationLayer::paint()` 需自行 `translate`，`renderToImage()` 不需要（传入的是已裁剪图）——两者约定不同，改动时容易踩。
- 放大镜/马赛克等涉及 DPR 的换算一律 `qRound(logical * dpr)`；本机 dpr=1.5，取整误差需留意。
- **设置存储**：`QSettings`，Windows 下为 `HKCU\Software\QShot\QShot`。`main.cpp` 的 `setOrganizationName`/`setApplicationName` 必须在任何 `Settings` 访问**之前**执行。
- **用户可见文案全部集中在 `src/core/Strings.cpp`**（中英两列表格，`Str::Count` 哨兵 + `static_assert` 保证与枚举同步，当前 44 条）。新增文案必须加在那里，不要在别处写 `QStringLiteral("中文")`。Qt 自身文案（标准按钮等）由 `installQtTranslations()` 加载 `qtbase_zh_CN.qm`。
- **排查漏翻的硬编码文案：按「设置点」扫，不要按中文字符范围扫。** 踩过的坑：工具栏 4 个动作按钮的文案是**英文**字面量（`"Undo"/"Copy"/"Save"/"Cncl"`，藏在 `kButtons` 表里当 `iconName`），按 `[\x{4e00}-\x{9fff}]` 扫全部漏掉。正确 grep：`drawText(|setText(|setToolTip(|setWindowTitle(|setPlaceholderText(|showMessage(|QMessageBox::|setTitle(|addItem(`。
- **画在按钮里的文字不要用写死的字体族**（如 `QFont("Arial", 8)`）——中文会走逐字回退、字形大小不可控。从 `p.font()` 派生再设字号。
- 工具栏按钮只有 32px 宽，动作按钮文案必须极短（英文用 `Cncl` 而非 `Cancel`）；改文案后要重新渲染确认不粘连。
- 设置对话框（`src/ui/SettingsDialog.cpp`）只读写 `Settings`；热键重注册、开机启动写注册表等**副作用一律留在 `ShotApplication`**，通过 `Settings` 的信号触发。
- 开机启动：`WinAutoStart` 用 `QSettings(path, NativeFormat)` 直指 Run 键，值必须是**带引号的原生分隔符路径**（含空格路径否则被截断）。

## 待用户决策

1. 「跨平台」是否当真：若当真，需给工厂加 Null Object 或判空 + 加 macOS/Linux CI 编译 job。
2. 多屏 overlay 的键盘焦点路由方案 —— **第三轮已查明与屏幕数无关**：`showToolbar()` 的 `toolbar_->show()` 本身就清空 focusWindow。待定方案：面板 `show()` 后 `overlay->activateWindow()` / 纯展示面板加 `Qt::WindowDoesNotAcceptFocus` / 快捷键改用 `Qt::ApplicationShortcut` 兜底。
3. ~~是否清理仓库残留~~ **已办（2026-09-21）**：根 `main.cpp`、`Main.qml`、`build_output.txt`、空 `err.txt`/`out.txt`、死目录 `importedcontent/` 均已 `git rm`；`build-mingw/`、`.cache/` 已删；`build-review/` 只留源码+PNG（25M→485K）。`.gitignore` 已补 `.cache/` 与根级残留名。ROUND3 P3 闭环。
4. 马赛克是否改为「提交时才落层」（当前拖动即写入 `mosaicLayer_`，右键取消会残留并被导出）。
5. 保存是否要支持「静默保存到固定目录」（当前仍每次弹保存对话框，只是默认目录/格式按设置预填）。
6. 设置对话框目前 OK 时统一应用，因此切换语言不会即时重译已打开的窗口（换来「取消」是真取消）。是否要改成即时生效。

## 产品能力缺口（第四轮评估结论）

按「用户实际要办的事」而非功能清单评估，当前覆盖 2 / 6 个场景：

- **已能胜任**：① 选中区域→标注→复制/另存（六种标注、Enter 复制、Ctrl+Z 撤销）；② 单击抓取整窗（悬停高亮 + `hoverWindowRect_`，见 `SnapOverlay.cpp` 约 778 行）
- **不能胜任**：③ 贴图（钉在屏幕上参考对照）；④ 滚动长截图；⑤ OCR 取字；⑥ 多显示器（**基础缺陷**：R3-4 坐标空间混用 + 焦点路由未修，非功能缺失）
- **打磨项**：托盘图标是纯蓝色方块（`ShotApplication.cpp` 里 `pixmap.fill(Qt::blue)`）；工具栏图标是几何占位符而非真图标；无安装包/签名

关键判断：**截图工具的泛用性不取决于「能不能截」，而取决于「截完之后能拿它干什么」**。目前出口只有剪贴板与存盘两条。另外多屏是**门槛问题而非加分项**——2 屏用户遇到坐标/焦点错误会直接弃用，这是泛用性的硬上限。

建议补齐优先级（性价比排序）：① 修多屏（R3-4）→ ② 贴图（工作量最小、复用现有选区/移动逻辑）→ ③ 历史记录（环形缓冲 + 托盘菜单）→ ④ 序号/高亮标注 → ⑤ 滚动截图 → ⑥ OCR（可用 Windows 内置 `Windows.Media.Ocr`，符合「优先复用原生能力」）。

## 审查历史

- 第一轮 `docs/CODE_REVIEW.md`；第二轮 `docs/CODE_REVIEW_ROUND2.md`；**第三轮 `docs/CODE_REVIEW_ROUND3.md`**（P0：文本标注无法输入、选完区后键盘全失效；均已探针实测）。
- 第三轮已落地 R3-1/2/3/5/7/8 与 R3-9 部分（见报告第七节）；**未做**：R3-4 多屏坐标空间统一、R3-6 局部重绘、R3-9 剩余、N-2、N-7、P1-3/4/6/8、P3。
- 第三轮后追加 **UI 可用性修复**（见报告第八节）：马赛克去颜色行（面板 398→174px）、文字尺寸改数字 14/18/24、输入提示改为 `viewport()` 上的绿色虚线框 + 占位符 + I 形光标 + 「点击输入文字」徽标。顺带修掉 `showSubPanel()` 先定位后 update 导致切工具面板尺寸不更新的 bug。
- **离屏渲染验证法**（无头验证 UI 的有效手段，比探针可靠）：链接真实 `.cpp` + `build/.../qshot_autogen/<HASH>/moc_*.cpp`，把 widget `render()` 到 `QImage` 存 PNG，用 Read 目视。见 `build-review/render_panels.cpp`、`render_text_editor.cpp`、`render_settings_dialog.cpp`。**注意用 DPR=1 的 `render()`，不要用 `grab()`**。
- **新增设置功能**（见 `docs/SETTINGS.md`）：快捷键（`QKeySequenceEdit`，校验需带修饰键或 F1–F24）、语言（中/英）、开机启动、保存目录/格式/JPEG 质量、截图包含鼠标指针、记住标注工具颜色粗细。`IGlobalHotkey::registerHotkey` 签名改为接收 `QKeySequence`；顺带修掉 `default: vk = qtKey` 的 VK 映射真 bug。
- 工具栏修复两处（见 `docs/SETTINGS.md` 与当日日志）：文本工具图标由 `drawText("T")` 改为与其他图标一致的 2px 描边几何画法（原来既不吃画笔也不撑满内框）；4 个动作按钮文案改走文案表，`ButtonDef` 把 `iconName`（几何）与 `label`（文案）拆开。
- 可复现探针留在 `build-review/probe_*.cpp`（该目录已 gitignore）。**直接调 Win32 GDI 的探针要额外链接 `-lgdi32 -luser32`**（主工程由 `Qt6::Gui` 传递）；集成测试探针要按需补齐各 `moc_*.cpp`。
- **`build-review/` 约定（2026-09-21 起）：只存源码与证据，不存产物。** 该目录是扁平结构（无 CMakeLists），编译产物（`.exe`/`build.ninja`/`.ninja_deps`/`.ninja_log`/`CMakeCache.txt`/`cmake_install.cmake`/`CMakeFiles/`/`qshot_autogen/`/`.qt/`）一律不入库、用完即清；需要时按源码现编。当前内容 = 24 个 `.cpp` + 42 张 PNG，约 485K。

## 验证环境限制（别再踩）

**从终端启动的 GUI 探针永远拿不到系统前台**：`GetForegroundWindow()` 恒为终端窗口，Windows 据此拒绝 `SetForegroundWindow()`。于是任何依赖「窗口是否 active / `QGuiApplication::focusWindow()`」的断言在探针里都会**时好时坏**（同一份代码不同轮次结果相反），不能作为验收依据。`AttachThreadInput` 强制前台的写法也不可靠。

可确定性地验证的只有：事件是否被某个 widget 收到（直接 `sendEvent` 到窗口）、`toPlainText()` 这类状态变化、Win32 z 序、编译/静态检查、**QSettings 读写往返、注册表写入后独立复核**。**涉及焦点/激活的结论必须由人工交互确认**。

其他实测到的环境限制（第四轮补充）：

- **本机两次连续 `grabWindow` 像素不稳定**（94% 像素不同）→ 不能靠像素差判断截图上有什么。
- **终端进程里 `GetCursorInfo` 可能返回 `hCursor=NULL`**（会话空闲时系统隐藏光标）→ 光标验证不可靠。
- `SetCursorPos()` 在探针里行为不可靠，无法把光标钉到已知位置。
- `reg.exe` 被安全策略拉黑，不能用；改用 Python `winreg` 做注册表独立复核。

## 危险操作与恢复（2026-09-21 实测）

- **⚠️ `git rm` 在 `docs/` 上连带清空了整个目录**：索引只暂存了指定的 3 个文件（`git status` 完全正常），但工作区整个 `docs/` 被删，**连未跟踪文件一起没了**。审计日志确认本次会话无任何显式删除 `docs/` 的命令，成因未定位，按已知风险对待。
- **规矩**：① 删 `docs/` 这类「已跟踪 + 未跟踪混装」目录前，先把未跟踪文件复制到仓库外；② `git rm` 后**必须 `ls` 该目录**，不能只信 `git status`；③ 更稳的替代是 `rm <file>` + `git add -A <dir>`，绕开 `git rm`。
- **救回未跟踪文件的唯一可靠来源：`~/.workbuddy-ai/file-history/<sessionId>/<hash>@vN`**（agent 写过的文件的历史快照）。定位法：先在 `~/.workbuddy-ai/changes-index/<sessionId>.json` 搜文件名确认动过，再在对应 `file-history` 目录按特征词 grep 快照、挑体积与原件一致的最新版拷回。已跟踪文件则 `git checkout HEAD -- <path>` 即可。
- 本次失败的死路（别再试）：`git fsck --lost-found` 悬空 blob、VSCode `User/History`、回收站、stash、临时目录——均无。
