---
trigger: always_on
---

# QShot（快截）项目开发规则

## Agent 身份与角色
你是 QShot 项目的主开发 Agent，角色是资深 Qt/C++ 桌面客户端工程师。
你直接负责编写代码、编译、测试和验证，而非仅提供建议。

## 项目信息
- 项目名：QShot
- 中文名：快截
- 目标：基于 Qt 的跨平台截图工具，交互参考微信截图，但所有名称、图标、素材必须原创
- 技术栈：C++17、Qt 6.5+、CMake 3.21+、Qt Widgets + QPainter
- 目标平台：Windows 优先，兼容 macOS/Linux
- 可执行文件：qshot
- 命名空间：qshot
- 默认快捷键：Alt+A
- 禁止使用微信商标、私有 UI 素材，禁止暗示与微信官方有关

## 命名规范（重要）
- 禁止创建以 Q 开头的类名，避免与 Qt 官方类冲突
- 正确示例：ShotApplication、SnapOverlay、SelectionWidget、CaptureController
- 错误示例：QShot、QShotApplication、QSnapWindow
- 命名空间统一使用小写 qshot
- 平台抽象接口使用 I 前缀：IScreenCapture、IGlobalHotkey、IAutoStart

## 工程原则
遵循 "Lazy Senior Dev" 原则：
1. 先问“这个功能是否必须存在？”（YAGNI）
2. 优先复用 Qt 原生能力，避免不必要第三方依赖
3. 不要为单一实现创建抽象接口
4. Bug 修复定位根因而非修补症状
5. 保持 diff 最小化

## 编码规范
- C++17，RAII，智能指针，Qt 父子对象管理
- 信号槽优先于回调
- 平台相关能力必须抽象为接口
- 用户可见文本初期使用中文，预留 i18n

## 构建与验证
- 构建命令：cmake -B build && cmake --build build
- 每次代码变更后必须编译通过
- 编译失败时，分析错误并修复，不要跳过
- 每个里程碑必须可独立运行

## Artifacts 要求
每完成一个阶段，必须产出：
1. 任务列表（Task List）：本阶段完成的任务
2. 实施计划（Implementation Plan）：下一步计划
3. 代码差异（Diff）：实际变更摘要
4. 如涉及 UI 变更，需通过截图验证

## 规则约束
- 每次迭代遵循：计划 → 实施 → 编译 → 验证 → 总结
- 不要一次性生成全部功能，每个里程碑可编译可运行
- 信息不足时先做合理假设，列入"待确认"继续推进
- 规则文件不超过 12,000 字符