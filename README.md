# QShot (快截)

基于 Qt 的截图工具。平台相关能力（屏幕采集 / 窗口探测 / 全局热键 / 开机启动）都走接口抽象，
**当前只实现了 Windows**。

## 构建

需要 Qt 6.11 MinGW 64bit 与配套的 MinGW 工具链。构建目录名与 Qt Creator 的约定一致，
方便直接用 IDE 打开同一个目录。

```bash
cmake -S . -B build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug \
      -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_PREFIX_PATH=D:/Qt/6.11.2/mingw_64
mingw32-make.exe -C build/Desktop_Qt_6_11_2_MinGW_64_bit_Debug
```

Release 与打包交给脚本，它会配置自己的构建目录（`build/release`，用 Ninja）：

```bash
bash tools/deploy.sh --build      # 配置 + 构建 Release + 打包 + 校验
bash tools/smoke_deploy.sh        # 在打包目录里实际启动一次，验证插件与中文目录
```

产物在 `dist/QShot/`，自包含，可以直接拷到没有装 Qt 的机器上运行。安装包脚本见
`tools/installer/qshot.iss` —— **它从未编译过**，本机没有 Inno Setup，详见 `docs/ROADMAP.md` 第 3.3 节。

## 开发辅助

| 命令 | 用途 |
| --- | --- |
| `bash build-review/build_probes.sh [--run] [名字...]` | 构建（并运行）全部验证探针与离屏渲染脚手架，无名字 = 全建全跑；结束时汇总断言总数 |
| `bash tools/smoke_single_instance.sh [exe]` | 启动两份 qshot.exe，验证第二份拒绝启动、第一份退出后名字被交还；不带参数测 Debug 版，传 `dist/QShot/qshot.exe` 测发布版 |
| `bash tools/make_icon.sh` | 重新生成 `resources/qshot.ico`（8 个尺寸）与放大拼版预览 |
| `python tools/verify_icon.py` | 不依赖 Qt，解析并校验 ICO 容器结构 |
| `python tools/verify_iss.py` | 安装包脚本的静态检查（BOM、宏、GUID、任务引用、路径是否存在） |

探针的二进制放在临时目录，`build-review/` 只留源码与证据（PNG）。全量构建约 4 分钟，
**必须后台跑**，前台会被超时信号打断。完整输出（含被 `tail` 截掉的前半段）写在临时目录的
`probe_log.txt` 里。

`probe_deploy` 不在这条命令里跑：它必须**在打包目录内**执行才能测到真实的插件解析，
由 `bash tools/smoke_deploy.sh` 负责（它会把探针拷进去、跑完删掉）。

`probe_partial_repaint` 是唯一一个自己把平台设成 `offscreen` 的探针，与「离屏渲染不要用 offscreen 平台」
（本机该平台字体库退化，一个字都不渲染）**不矛盾**：它 `show()` 是为了让 `update()` 真的触发 paint 事件
（不可见控件的 `update()` 是空操作），断言的是「重绘区域」而不是画面上的文字，所以字体渲不出来无所谓。

单实例那条是 shell 脚本而不是探针，因为第二份**本来就该弹一个模态框**——模态框会阻塞，
所以它的退出码说明不了任何事，而探针没办法把它关掉。可读的外部信号只有 stderr：没有守护的
第二份会因为抢不到第一份持有的全局热键而在零点几秒内 `qWarning()`，有守护的那份根本走不到
`ShotApplication`。所以「stderr 是空的」就是判据。它会启动 4 份 qshot.exe 并全部杀掉，
**运行期间屏幕会闪出几个窗口和对话框**。

## 文档

- `docs/ROADMAP.md` —— 路线、里程碑、偏差记录、决策记录、验收手段
- `docs/SETTINGS.md` —— 全部设置项与默认值
- `docs/CODE_REVIEW.md`、`docs/CODE_REVIEW_ROUND2.md`、`docs/CODE_REVIEW_ROUND3.md` —— 历次代码审查
