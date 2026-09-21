# 设置功能（Settings）

新增于第三轮审查之后。托盘菜单的「设置」入口此前只是 `qDebug()` 占位，现在接上了真实窗口。

## 一、可配置项

| 分区 | 设置项 | 存储键 | 默认值 |
| --- | --- | --- | --- |
| 快捷键 | 截图全局热键 | `hotkey` | `Alt+A` |
| 通用 | 界面语言（简体中文 / English） | `language` | `zh` |
| 通用 | 开机时自动启动 | `autoStart` | `false` |
| 保存 | 默认保存目录 | `save/directory` | `Pictures` |
| 保存 | 图片格式（PNG / JPEG） | `save/format` | `png` |
| 保存 | JPEG 质量（1–100） | `save/jpegQuality` | `92` |
| 截图 | 截图中包含鼠标指针 | `capture/includeCursor` | `false` |
| 标注 | 记住上次使用的颜色和粗细 | `annotations/remember` | `true` |
| 标注 | 每个工具的颜色/线宽/马赛克粒度/字号 | `annotations/<tool>/*` | 见 `ToolSettings` |

工具栏的 4 个动作按钮（撤销 / 复制 / 保存 / 取消）也从文案表取词，英文沿用原有的短词 `Undo / Copy / Save / Cncl`——按钮只有 32px 宽，`Cancel` 放不下。中文两字（各约 11px）居中后左右各余 5px，与相邻按钮的 4px 间距相加仍有 14px 视觉间隔，不会粘连。

存储位置由 `QSettings` 决定：Windows 下为 `HKEY_CURRENT_USER\Software\QShot\QShot`。
因此 `main.cpp` 里 `setOrganizationName()` / `setApplicationName()` 必须在任何 `Settings` 访问**之前**执行。

## 二、文件职责

| 文件 | 职责 |
| --- | --- |
| `src/core/Settings.h/.cpp` | 唯一的状态源。构造时一次性载入并缓存全部值（`text()` 会在绘制路径上调用，不能每次都读注册表），写入时同步更新缓存与存储，并发出信号 |
| `src/core/Strings.h/.cpp` | 全部用户可见文案，中英两列表格 |
| `src/core/IAutoStart.h` | 开机启动的平台接口 |
| `src/platform/windows/WinAutoStart.h/.cpp` | 注册表 Run 键实现 |
| `src/ui/SettingsDialog.h/.cpp` | 设置窗口。只读写 `Settings`，不碰热键注册与开机启动的实现 |
| `src/app/ShotApplication.cpp` | 把设置的**副作用**接起来：重注册热键、重刷托盘文案、写注册表 |

副作用与界面分离是有意为之：对话框因此完全不需要知道热键怎么注册、开机启动怎么实现，只要改设置即可。

## 三、关键决策

### 1. i18n 用文案表，不用 Qt Linguist

全 UI 44 条文案、2 种语言。`.ts/.qm` 会引入 `lrelease` + CMake 构建规则，而且**运行时切换语言仍然要自己写**每个控件的 `changeEvent()`/`retranslateUi()`；文案表没有构建工具依赖，且读起来就是一张对照表，评审时缺条目一眼可见。

`Str` 枚举末尾有 `Count` 哨兵，`Strings.cpp` 里 `static_assert(std::size(kTable) == Count)` 保证两者不会失配。若文案量涨到几百条，迁到 `.ts` 是机械工作。

**排查硬编码文案的正确方式**：不要按中文字符范围去 grep——工具栏那 4 个动作按钮的文案是**英文**字面量（`"Undo"/"Copy"/"Save"/"Cncl"`）藏在 `kButtons` 表里，按 CJK 范围扫会全部漏掉，结果就是语言切了但工具栏没变。应该按**设置点**扫：`drawText(` / `setText(` / `setToolTip(` / `setWindowTitle(` / `setPlaceholderText(` / `showMessage(` / `QMessageBox::` / `setTitle(` / `addItem(`，然后逐个确认参数是不是查表来的。

Qt **自身**的文案（`QMessageBox` 按钮、`QLineEdit` 右键菜单）不在表里，由 `installQtTranslations()` 加载 Qt 自带的 `qtbase_zh_CN.qm` 处理；英文是 Qt 的源语言，不加载任何目录。效果：中文模式下标准按钮显示「确定/取消」，英文模式下显示 OK/Cancel。

### 2. `IGlobalHotkey` 改为接收 `QKeySequence`

原签名是 `registerHotkey(const QString& key, Qt::KeyboardModifiers)`，实现里再用 `QKeySequence(key)` 解析回来。设置页产出的本来就是 `QKeySequence`，这个字符串往返会经过 Qt 的按键名文法，对命名键不可靠。改为直接传 `QKeySequence`，表示形式只有一种。

顺带修掉一个真 bug：原实现映射不到 VK 时 `default: vk = qtKey;`，而 `Qt::Key_Print` 是 `0x01000009`、`VK_SNAPSHOT` 是 `0x2C`，于是要么注册失败要么注册成无关按键。现在映射不到就返回 0 并如实报失败，同时补齐了 Insert/Delete/Home/End/PageUp/PageDown/方向键/Print/Pause 等常用命名键。

### 3. 开机启动用 `QSettings` 直接指向 Run 键

`QSettings(path, NativeFormat)` 的 `NativeFormat` 就是注册表，比手写 `RegOpenKeyEx`/`RegSetValueEx` 少一大截错误处理。值写成带引号的**原生分隔符**路径，否则含空格的路径会在第一个空格处被截断。

用 `HKCU` 而非 `HKLM`：无需提权，且该设置本就是按用户生效的。

`isEnabled()` 只判断「条目是否存在」，不比对路径——安装路径变了对用户来说仍然是「开着」，而且路径会在下次启动时被刷新（见下）。

### 4. 启动时无条件对齐注册表

`ShotApplication::applyAutoStart()` 在启动和设置变更时都会调用：想要就无条件重写（顺带刷新迁移过的 exe 路径），不想要且当前存在才删除。这样注册表始终与设置一致，且不需要额外的「上次是否同步过」状态。

### 5. 设置对话框 OK 时统一应用

没有做「改一项立刻生效」。代价是切换语言不会即时重译当前窗口，需要重新打开才生效——换来的是「取消」是真正的取消。对话框内部的 `retranslate()` 仍然保留，因为「恢复默认」可能把语言一起改掉，那种情况下必须立即重译。

## 四、验证结果

| 项 | 结果 |
| --- | --- |
| 构建 | `[100%] Built target qshot` |
| 静态检查 | `-Wall -Wextra -Wshadow -Wunused` **零警告** |
| 启动冒烟 | 存活 6 秒无崩溃，日志确认 `Global hotkey registered successfully: "Alt+A"` |
| 设置往返 | 23 项断言全过（含默认值回退、`rememberToolSettings=off` 屏蔽、质量值 clamp、`restoreDefaults` 复位） |
| 开机启动注册表 | 写入带引号原生路径 → `isEnabled()` 为真 → 删除 → 独立用 `winreg` 复核，Run 键无残留 |
| 设置页渲染 | 中文/PNG、中文/JPEG、英文/PNG 三种状态离屏渲染目视确认；JPEG 质量行按格式显隐正确 |
| 工具栏语言 | 中英双语离屏渲染确认：中文显示「撤销/复制/保存/取消」，英文显示 `Undo/Copy/Save/Cncl`，两字标签在 32px 按钮内不粘连 |
| 光标合成 | 光标形状/尺寸/alpha 正确（标准箭头，未被 DPR 放大 1.5 倍）；合成路径经合成图往返测试确认**无损**（`changed=0, maxDelta=0`） |

### 验证环境限制（本轮新增踩坑）

- **本机两次连续 `grabWindow` 像素并不稳定**：94% 像素不同、最大通道差 106。因此「比较两次截图的像素差」在本机**不能**用来判断某物是否被画上去。
- **`QWidget::grab()` 返回 DPR 缩放后的位图**，而 `findChildren()` 给的几何是逻辑坐标。两者直接对比会误判成「布局没铺开」。要用 DPR=1 的 `QWidget::render()` 才能 1:1 比对。
- **终端启动的进程里 `GetCursorInfo` 可能返回 `hCursor = NULL` / `CURSOR_SHOWING = 0`**（会话空闲时系统会隐藏光标）。光标相关的断言因此不可靠。这一状态同时也验证了降级路径：无光标时不绘制、不崩溃。
- `SetCursorPos()` 在探针进程里行为不可靠（设 `(1600,1000)` 实际落到 `(850,416)`），不能用来把光标钉到已知位置做确定性测试。
- 直接调用 Win32 GDI 的探针需要额外链接 `-lgdi32 -luser32`；主工程之所以不需要，是 `Qt6::Gui` 传递进来的。

## 五、仍未处理 / 后续可做

- **多屏不同缩放比**下光标定位未处理：`QCursor::pos()` 与 `QScreen::geometry()` 的差值再乘 DPR，仅在单屏或各屏 DPR 一致时严格成立。与既有的 R3-4（多屏坐标空间混用）是同一类问题，应一并解决。
- 保存对话框仍然每次弹出，只是默认目录与格式已按设置预填。若希望「静默保存到固定目录」，需要再加一个开关。
- 语言切换不即时重译已打开的窗口（见决策 5）。
- 设置对话框没有做「应用」按钮与热键冲突的即时提示；热键注册失败时走的是既有的托盘气泡 + 自动重试路径。
- 第三轮遗留的 R3-4、R3-6、R3-9 剩余项、N-7、P1-3/4/6/8、P3 仓库残留均未变动（N-2 工厂返回 nullptr 未判空已在本轮顺手修掉）。
