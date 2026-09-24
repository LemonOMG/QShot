# QShot 项目长期记忆

> 只放**跨会话必需**的结论。细节分流到别处，这里不重复：
> **实测过程** → `.workbuddy-ai/memory/YYYY-MM-DD.md`；**Qt GUI 验证方法论** → skill `qt-gui-verification`；
> **Windows 部署与安装包** → skill `qt-windows-deploy`；**计划与决策理由** → `docs/ROADMAP.md`；**设置项语义** → `docs/SETTINGS.md`。

## 环境

- Qt 6.11.2 MinGW 64bit `D:/Qt/6.11.2/mingw_64`；工具链 `D:/Qt/Tools/mingw1310_64/bin/`
- Debug 构建：`mingw32-make.exe -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug`（**链接前先确认没有 qshot.exe 在跑**，否则 `ld` 报 `Permission denied`）
- 发布：`bash tools/deploy.sh --build` → `dist/QShot/`（自包含 34MB）；`bash tools/smoke_deploy.sh [--app]` 在部署目录里实跑
- 静态检查（秒级，不跑 AUTOMOC）：`g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow -Wunused -Isrc -I<Qt>/include{,/QtCore,/QtGui,/QtWidgets} $(find src -name '*.cpp')`
- 探针：**已并入 CMake / CTest**（2026-09-24）。旧脚本 `build-review/build_probes.sh` **已删除** —— 两套构建同一批源文件就是「同一个东西两处维护」
  - `ctest --test-dir build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug [--output-on-failure]` → **19 个测试 / 627 条断言**，全量约 1 分钟
  - ⚠️ 判据是每个测试必须打印 `N checks, 0 failures`（`PASS_REGULAR_EXPRESSION`）。汇总行是返回前最后一行，匹配到它就等于「跑到末尾且零失败」—— **比退出码更强**。只看退出码会让「一条输出都没有」的探针静默通过：`probe_quiet_save` 的 29 条断言曾长期一条都不打印却 `exit=0`，旧脚本因此报 **598 而非 627** 且显示全绿
  - ⚠️ 该正则**绝不能加 `^...$` 锚点** —— CTest 对**整段**输出做一次匹配、不是逐行，带锚点会误杀每一个会打印检查行的探针（实测 19 个测试全红）
  - ⚠️ `ctest` 自己把 Qt 与 MinGW 的 `bin` 注入测试环境（`ENVIRONMENT_MODIFICATION`），**不需要先 export PATH**；「无输出的 `exit=127`」那个坑从根上消失
  - 源码清单收进静态库 `qshot_core`（只写一次），moc 交给 AUTOMOC —— 消灭了「漏 moc 文件」和「指向已消失的 hash 目录」两类反复错误
  - `probe_deploy` 不入 CTest（必须在部署目录内跑，走 `tools/smoke_deploy.sh`）；`probe_settings_autostart` 带 `side-effects` 标签（读写真实 HKCU Run 键，先存后恢复）
  - `cmake --build <dir> --target render_evidence` 重生成目视证据 PNG；`--target archaeology` 构建 20 个历史一次性探针（默认不构建）
  - ⚠️ `set_tests_properties` 是 `属性 值` 成对的：列表型属性要**先放进变量再整体传**，否则最后一个值成了没属性名的孤儿，报无用的 `incorrect number of arguments`
  - ⚠️ 探针结尾**必须显式 `fflush(stdout)`**（stdout 重定向到管道时全缓冲，进程退出不一定冲刷它）
- ⚠️ **`build-review/` 是源码目录**：`.gitignore` 的 `build-*/`（本意是 CMake 构建树）曾把它整个吞掉 —— 45 个源文件 + 脚本 + 全部证据 PNG 在整个项目历史里都是未跟踪状态。靠紧随其后的 `!build-review/` 救回；`build-review/*.png` 仍忽略（渲染产出，可再生）
- 本机 **1 块屏** `1707x1067`、**dpr=1.5**（分数缩放是默认路径，不是边缘情况）

## 项目约定

- 规则 `.agents/rules/q-shot.md`：禁 Q 前缀类名、命名空间 `qshot`、C++17、平台能力抽象为接口、YAGNI、**每次变更必须编译通过**。
- **`SnapOverlay::renderSelectionImage()` 是唯一的合成入口**（`physicalSelectionRect()` → `backgroundImage_.copy()` → `annotationLayer_.renderToImage()`）。复制 / 另存 / 贴图都走它。
- **overlay 与 pin 之间只走信号**：`SnapOverlay::pinRequested` → `ShotApplication::onPinRequested()` 建窗口。overlay 不知道 `PinWindow` 存在，贴图才能比 overlay 活得久。
- 平台装配点 `src/core/PlatformFactory.cpp`（`#ifdef Q_OS_WIN`），返回 `nullptr` 已判空。**链接时必须把 5 个 Windows 实现的 `.cpp` 全带上**（vtable 在自己的 TU 里发出，只链调用到的那个仍缺符号），并补 `-ldwmapi -lgdi32 -luser32`。
- 坐标约定：标注点 =「相对选区左上角的**逻辑**坐标」。`AnnotationLayer::paint()` 需自行 `translate`，`renderToImage()` **不**需要 —— 两者约定不同，易踩。DPR 换算一律 `qRound(logical * dpr)`。
- **设置** `QSettings`（Windows = `HKCU\Software\QShot\QShot`）。`main.cpp` 的 `setOrganizationName`/`setApplicationName` 必须在任何 `Settings` 访问**之前**。
- **用户可见文案全在 `src/core/Strings.cpp`**（中英两列 + `Str::Count` 哨兵 + `static_assert`，当前 **70** 条）。**查漏翻按「设置点」扫，不要按中文字符范围扫**（工具栏动作按钮是英文字面量 `Undo/Copy/Save/Cncl`，按 `[\x{4e00}-\x{9fff}]` 全漏）。grep：`drawText(|setText(|setToolTip(|setWindowTitle(|setPlaceholderText(|showMessage(|QMessageBox::|setTitle(|addItem(`。
- 按钮内文字**不要写死字体族**（中文会逐字回退），从 `p.font()` 派生再设字号；工具栏按钮只有 32px 宽，动作文案必须极短，**改文案后必须重跑 `render_pin` 的中英双语断言**。
- `SettingsDialog` 只读写 `Settings`；热键重注册、写注册表等**副作用一律留在 `ShotApplication`**，经 `Settings` 信号触发。`WinAutoStart` 用 `QSettings(path, NativeFormat)` 直指 Run 键，值必须是**带引号的原生分隔符路径**。
- **列表类 UI 用 `aboutToShow` 重建**（如 `HistoryMenu`），别维护平行副本。**动作一律按条目 `id` 闭包捕获，不按行号**（行号会静默错位）。
- **脏区（M7 定的）**：① 一律「宁大勿小」——少一个像素留残影，多几个像素只是多画几笔；② 同一个矩形**只能有一处计算**，绘制与脏区共用（`dimensionBadgeRect()` / `textHintRect()` / `annotationInkMargin()` 都是这么切的）；③ 笔画类工具（`isStrokeTool()`）的脏区**只取最新一段**，取整条路径会让横跨屏幕的笔画涨回整屏；④ 判定「脏区对不对」的断言必须是**「只重绘脏区后的整帧 == 全量重绘」**，不是「等于我手算的矩形」（后者只是把公式抄进测试）。
- **注释要写「为什么」**：非显然代码都带解释性注释，含实测数字与被否掉的替代方案。
- **审查问题状态只有一个地方：`docs/REVIEW_STATUS.md`（M7 后定的）。** 三份审查报告是**快照、不是待办清单**，不要更新它们的状态。规矩：① 修完一项 → 改索引一行；② 新编号 → **先登记再写报告**；③ **每轮开工前先跑 `python tools/verify_review_status.py`**。校验器强制编号双向一致、状态词闭集、已修行落点文件真实存在、每份报告有横幅与指针、汇总表与行数一致；`--selftest` 证明 9 条检查都能变红。**起因**：同类失误连续发生三次（P1-3/6/8、P2-2/2-3、R3-9），状态曾散落在三份报告的**八个小节**里，每节在写下的那天都是对的、合起来就是错的。
- **构建自带警告**：`CMakeLists.txt` 有 `target_compile_options(qshot PRIVATE -Wall -Wextra -Wshadow)`（用 `CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"` 包住）。秒级静态检查仍然保留（不跑 AUTOMOC）。

## 实测过的语义（本项目反复踩的）

| 事实 | 结论 |
| --- | --- |
| `qRound` 正负不对称 | 正 .5 向 +∞、负 .5 远离零 → 对**绝对坐标**取整会让同一手势在负原点屏上差 1px；要平移不变就对**位移**取整 |
| 描边 `drawRect` 边框 | 笔**骑在路径上**各铺半笔宽 → DPR 2 时最后一列染成 `#FF0000A5`。要精确对齐必须用 **`fillRect` 色带** |
| 逻辑像素边框宽 vs DPR | 内容起点 = `kBorderWidth × dpr` 设备像素，**必须整数**，否则整幅图平移半像素并被重采样 |
| **带 dpr 的 `QImage` 上 `QPainter::translate`** | 平移量是**逻辑**坐标（被 dpr 缩放）；而离屏图的**原点可能是裁剪过的物理坐标**。两者只在没裁剪时重合 —— 混淆会让整幅掩码错位（N-7 就是这么来的：`translate(-logicalBounding.topLeft())` 应为 `translate(-physicalBounding.x()/dpr, ...)`） |
| 展示型面板抢焦点 | 已解决：`Qt::WindowDoesNotAcceptFocus` → `WS_EX_NOACTIVATE`（实测 `0x08080088`） |

## 危险操作

**⚠️ `git rm` 曾在 `docs/` 上连带清空整个工作区目录**（含未跟踪文件，成因未定位）。规矩：① 删「已跟踪+未跟踪混装」目录前先把未跟踪文件复制到仓库外；② `git rm` 后**必须 `ls`**，不能只信 `git status`；③ 更稳的是 `rm <file>` + `git add -A <dir>`。
救回未跟踪文件的唯一来源：`~/.workbuddy-ai/file-history/<sessionId>/<hash>@vN`（先在 `~/.workbuddy-ai/changes-index/<sessionId>.json` 确认动过）。死路：`git fsck --lost-found`、VSCode `User/History`、回收站、stash。

## 当前状态

- **路线**：走「全能」路线，计划见 `docs/ROADMAP.md`。本轮 = Phase 0 地基 + Phase 1 出口能力 + Phase 3 打磨；**OCR 与滚动长截图已决策推迟**（评估结论保留在 ROADMAP 第四节）。
- **M0~M6 全部完成**：拆模块 → 多屏坐标空间（真双屏待人工确认）→ 焦点路由（本机 1 屏无法验证）→ 贴图 + 悬停样式 → 历史记录 → 序号/高亮 → 真图标集 + 工具栏图标集 + 部署 + 设置补项 + 清理。
- **M7 完成（性能项收尾）**：P2 全节 + R3-6。复核发现 P2-2/P2-3 **早已修好、只是文档没跟上** —— 审查文档的表格会过期，**先复核再动手**。R3-6 = `paintEvent` 按 `event->rect()` 裁剪 + 暗色遮罩从 `subtracted()` 改 4 个互不重叠的 `fillRect` + 四个状态各算脏区（Annotating 下笔画类工具**只取最新一段**）。实测脏区 1%~27%，Dragging 单帧 0.92ms → 0.34ms。详见 `docs/ROADMAP.md` §3.7。
- **M6 清理项已收尾**：N-7 马赛克增量 ✅（探针抓到一个**既有 bug**：掩码按被裁剪的物理原点平移）；P1-4 单实例保护 ✅；P1-3/6/8 复核后确认早已修掉，只剩 `hotkeyId_` 魔数（已改常量）。
- **三轮审查清单已全部收尾**：41 个编号里 **38 已修、3 部分已修**。逐条状态只看 `docs/REVIEW_STATUS.md`。
  「部分已修」的三条**就是当前全部未完成的审查项**：**P1-2**（`windowRectAt()` 在 `found==false` 时回落 `GetCursorPos()`，该路径下入参被忽略，低危防御路径）、**P3**（只剩「无测试无 CI」；`-Wall -Wextra -Wshadow` 已于 2026-09-23 开进 CMake）、**N-9**（`EnumData` 未整体清零 + 仍用 `GetClassNameA`/`GetWindowLong`；`IScreenCapture.h` 仍 include `<QScreen>`）。
  另有 N-1 与 R3-4 代码已完成，但**需要真双屏人工确认**（本机 1 块屏）。
  → **反复出现的教训：审查文档的表格会过期，先复核再动手。** 已在 M7 后用「唯一索引 + 校验器 + 规则」把这条结构性地解决掉。
- 规模：`src/` **7210 行** / 51 个 `.h`+`.cpp`；最大 `SnapOverlay.cpp` **965 行**（M7 里 +210，几乎全是「为什么这个脏区是对的」的注释）；文案 70 条。
- **安装包**：2026-09-24 **首次编译成功**（Inno Setup 6.7.3，装在 `D:\Program Files (x86)\Inno Setup 6\`），产物 `dist/QShot-0.1.0-setup.exe`（12.0 MB）；静默装到临时目录后**在该目录内**跑 `smoke_deploy.sh` 得 **10 checks / 0 failures**。⚠️ **从 Git Bash 调它必须加 `MSYS_NO_PATHCONV=1`** —— 否则 `/VERYSILENT` 被改写成 `C:/.../PortableGit/versions/1.2.0/VERYSILENT`，安装器弹「无效命令行参数」框而 `/SUPPRESSMSGBOXES` 同样被改写、压不住 → 无限挂起（无日志、无 consent.exe），看着像「本会话跑不起 GUI 安装器」。**仍未做**：代码签名（无证书）。
- 报告：`docs/CODE_REVIEW.md` / `_ROUND2.md` / `_ROUND3.md`。
- **坐标空间契约（M1 定的，别再搞反）**：`WinWindowDetector` 返回**全局桌面**坐标；`SnapOverlay` 在自己边界处**归一化一次**（`translated(-globalOrigin())`），内部一律 widget 局部。顶层窗口（`ToolbarWidget` / `TextInputWidget` / `PinWindow`）的 `move()`/`pos()` 读的是**全局**坐标。`globalOrigin()` = `mapToGlobal(QPoint(0,0))`，**不要**改成 `screenGeometry_.topLeft()`。
- **工具栏宽度不是约束**（M5 实测推翻了原计划里的「阻塞项」）：它是**顶层窗口**，钳制在**屏幕**矩形内，与选区无关。8 工具 = 484px。**别再为此做两排布局或溢出菜单。**
- **部署两条硬事实**：① `windeployqt --translations <lang>` **不部署** `qtbase_<lang>.qm`（只给 99 字节的元目录），必须 `--no-translations` + 显式复制；② 图标**已编进 exe**（`resources.qrc` + AUTORCC），exe 旁边不需要任何 `.ico`。
- 两个保存开关（都默认关闭）：`save/quiet` **只作用于截图工具栏的保存按钮**；`save/onCopy` **只作用于正在截的这一张**。静默保存 `screenshot_<时间戳>.<ext>`，**同名后缀递增绝不覆盖**，失败**必须出声**。两条保存路径共用 `writeImage()`/`resolvedSaveDirectory()` —— 格式来源不同，**编码不能不同**。

## 待用户裁决（索引；逐条理由见 `docs/ROADMAP.md` 第八节）

1. 「跨平台」是否当真 —— 若当真需 Null Object + macOS/Linux CI 编译 job。
2. 马赛克是否改「提交时才落层」（现在拖动即写入，右键取消会残留并被导出）。
3. 「另存为」要不要也跟随 `save/quiet`（现在刻意不跟随）。
4. 设置对话框 OK 时统一应用 → 切语言不即时重译已打开窗口；是否改即时生效。
5. 规则冲突：「不为单一实现创建抽象接口」vs 现存 5 个单实现接口 + `PlatformFactory`。
6. 贴图设置项（不透明度/细边框/是否置顶）与悬停内阴影深度、静止态 `#818181` 边框可读性 —— 审美判断。
7. 历史记录默认值：条数 20 / 字节 200MB / 复制后托盘提示。
8. 序号与高亮外观值（荧光笔 `#FDD835` a110 w18；徽标 `#E53935` d28）；**八工具图标形状**看 `build-review/toolbar_icons_zoom.png`。
9. 「半透明矩形高亮」没做（只做了自由涂抹）；要补应给矩形工具加「填充」开关。
10. **应用图标造型**看 `build-review/icon_preview.png`（改 `tools/make_icon.cpp` 后跑 `bash tools/make_icon.sh`）。
11. 部署裁剪取舍：已剪 `Qt6Network`/`tls`/`networkinformation`/`generic`/`Qt6Svg`/`iconengines`/`qsvg`/`qgif`；日后要用 SVG 图标**先改 `tools/deploy.sh` 清单**，`probe_deploy` 第 `[4]` 节会报出哪条挡着。
12. **代码签名**（无证书，SmartScreen 警告脚本解决不了）；**中文向导页**（需 `ChineseSimplified.isl`）。
13. **单实例第二份只弹框退出** —— 更好的是「让第一份立刻截图」，但需消息专用窗口 + IPC（互斥量带不了载荷）。

