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

## 项目约定

- 开发规则见 `.agents/rules/q-shot.md`：禁 Q 前缀类名、命名空间 `qshot`、平台能力须抽象为接口、YAGNI、每次变更必须编译通过。
- 规则内部存在冲突：「不为单一实现创建抽象接口」与现存 3 个单实现接口（`IScreenCapture`/`IGlobalHotkey`/`IWindowDetector`）+ `PlatformFactory` 矛盾。已两次写入审查报告，**待用户裁决**。
- 平台装配点：`src/core/PlatformFactory.cpp`（`#ifdef Q_OS_WIN` 分支）。注意工厂返回 `nullptr`，调用方目前**未判空**。
- 坐标约定：标注点统一为「相对选区左上角的逻辑坐标」；`AnnotationLayer::paint()` 需自行 `translate`，`renderToImage()` 不需要（传入的是已裁剪图）——两者约定不同，改动时容易踩。
- 放大镜/马赛克等涉及 DPR 的换算一律 `qRound(logical * dpr)`；本机 dpr=1.5，取整误差需留意。

## 待用户决策

1. 「跨平台」是否当真：若当真，需给工厂加 Null Object 或判空 + 加 macOS/Linux CI 编译 job。
2. 多屏 overlay 的键盘焦点路由方案（上提到 ShotApplication 还是 overlay 主动取焦）——需双屏环境才能验证。
3. 是否清理仓库残留（根 `main.cpp`、`Main.qml`、`build_output.txt`、空的 `err.txt`/`out.txt`）。
