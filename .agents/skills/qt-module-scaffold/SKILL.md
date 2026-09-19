# Skill: qt-module-scaffold

## 描述
按 QShot 架构规范创建新的 Qt 模块。

## 输入
- 模块名称（如 capture、overlay、annotation）
- 模块职责描述

## 步骤
1. 在 src/ 下创建模块目录
2. 创建 .h 和 .cpp 文件，类名禁止以 Q 开头
3. 在 CMakeLists.txt 中添加源文件
4. 确保父对象管理和信号槽连接正确
5. 编译验证