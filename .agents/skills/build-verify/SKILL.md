# Skill: build-verify

## 描述
编译 QShot 项目、运行验证套件，并在需要时新增一个探针。

## 环境（Windows）
工具链需要以下 PATH 前缀，否则 `cmake` / `g++` 找不到：
```
D:/Qt/6.11.2/mingw_64/bin
D:/Qt/Tools/mingw1310_64/bin
D:/Qt/Tools/CMake_64/bin
```
`CMAKE_PREFIX_PATH=D:/Qt/6.11.2/mingw_64`。

## 构建
用 Qt Creator 约定的构建目录，好让 IDE 打开同一个目录：
```bash
cmake -S . -B build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug \
      -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_PREFIX_PATH=D:/Qt/6.11.2/mingw_64
cmake --build build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug -j 8
```
**不要**把构建目录建在 `build-review/` 里面 —— 那是探针的**源码**目录。`.gitignore` 里的
`build-*/` 原本就把它整个吞掉了（45 个源文件 + 构建脚本 + 全部证据 PNG 一直是未跟踪状态），
现在靠一条 `!build-review/` 取反救回来。

## 告警
`CMakeLists.txt` 里已经开了 `-Wall -Wextra -Wshadow`（GCC/Clang 才加），**默认构建就是 0 告警**，
不需要另外配一套 flags。要全量确认就 `--clean-first` 重建并扫**完整**输出，不要只看 `tail`
（会静默丢掉前半段，剩下的看着权威但是错的）。

## 验证
```bash
ctest --test-dir build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug --output-on-failure
```
**19 个测试 / 627 条断言。** 判据是每个测试都必须打印 `N checks, 0 failures`：
汇总行是探针返回前打印的最后一行，所以匹配到它就等于「跑到了末尾且零失败」—— 比退出码更强。
只看退出码会让「一条输出都没有」的探针静默混过去（真发生过，见下）。

`ctest` 自己把 Qt 与 MinGW 的 `bin` 注入测试环境，**不需要先 export PATH**。

- `probe_deploy` 不在 CTest 里：它必须在**打包目录内**执行才能测到真实的插件解析，
  走 `bash tools/smoke_deploy.sh`。
- `probe_settings_autostart` 带 `side-effects` 标签：它会读写真实的 HKCU Run 键（先存后恢复）。
  不想让测试碰注册表就 `ctest -LE side-effects`。

## 新增一个探针
在 `build-review/` 下写 `probe_xxx.cpp`，然后在 `CMakeLists.txt` 的 `if (BUILD_TESTING)` 块里加一行：

```cmake
qshot_add_probe(probe_xxx)
```

它自动获得：链接 `qshot_core`、AUTOMOC、工作目录 `build-review/`、注入的 PATH，以及
「必须打印 `N checks, 0 failures`」这条判据。**不要**手写 g++ 命令行 —— 那正是旧脚本反复
出错的地方（漏 moc 文件、指向已消失的 hash 目录）。

探针约定：结尾打印 `printf("\n%d checks, %d failures\n", checks, failures);`，
失败数非零则 `return 1`。**返回前显式 `fflush(stdout)`** —— stdout 被重定向到管道或文件时是
全缓冲的，而进程退出时不一定冲刷它。`probe_quiet_save` 曾因此 29 条断言一条都没打印出来，
却以 `exit=0` 结束。

## 已确认的 Qt 行为（不必重复验证）
- **`QPainter` 在 DPR>1 的 `QPixmap`/`QImage` 上会自动以逻辑坐标工作**（`begin()` 已应用 DPR 变换）。
  在带 DPR 的图上绘制/合成时**不能**再手动 `scale(dpr, dpr)`，否则双重缩放。
- `QImage(200,200)` + `setDevicePixelRatio(2)` → `size()=200x200`（物理），`deviceIndependentSize()=100x100`（逻辑）。
- `drawImage` 按源图自身的逻辑尺寸绘制。
- 带 `Qt::Tool` 标志、即使传了 parent 的控件**仍是顶层窗口**，`move()`/`pos()`/`frameGeometry()` 全部使用**屏幕坐标**。
  本项目约定：`SnapOverlay` 内部一律用窗口局部坐标，跨到工具栏/文字框时经 `globalOrigin()`
  （= `mapToGlobal(QPoint(0,0))`）换算 —— **不要**改成 `screenGeometry_.topLeft()`，万一 WM 把窗口摆到了别处，只有 `mapToGlobal` 仍然对。
- **`QWidget::render(target, targetOffset, sourceRegion)` 的 `targetOffset` 是「源区域内容」的落点**，
  不是「控件原点」的落点。原地重绘某个区域必须传 `region.topLeft()`。
- **`QWidget::update()` 对不可见控件是空操作**，要读 paint 事件必须先 `show()`。
