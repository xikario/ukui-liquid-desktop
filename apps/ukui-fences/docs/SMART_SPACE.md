# 智能空间小组件

智能空间是本地文件索引、正文检索和预览小组件。它不调用远程模型，不包含联网搜索、候选重排、追问或多空间管理功能；需要深度分析时，由外部 Agent Skill 调用本地索引完成。

## 使用

- 桌面右键选择“智能空间小组件”，或运行 `ukui-fences-launcher --smart-space`。
- 组件默认贴边隐藏，可在设置 → 更新策略中关闭“启动或启用组件后默认贴边隐藏”。
- 搜索框点击搜索或按 Enter 执行。普通搜索按空格拆分多个条件；`re:` 前缀启用不区分大小写的 Unicode 正则表达式。
- 文件夹可以继续向下展开；文件卡片支持打开、预览和右键操作。
- 顶部范围固定为“全部文件”；搜索条件只在当前会话生效，不保存为独立空间。
- “更多”菜单提供索引刷新、快速建库、知识库状态和任务暂停。

## 可索引格式

支持 PDF、DOCX/WPS、PPTX/WPS、XLSX/WPS、文本、图片 OCR 等格式。索引名称、路径、类型、大小、修改时间和可提取正文；原始文件始终只读输入。

## 索引和知识库

- UI 快照位于 Qt 用户缓存目录的 `smart-space/index.json`。
- 本地知识库位于 `~/.local/share/ukui-fences/knowledge/`，使用 SQLite/FTS5 保存正文片段和本地检索元数据。
- 建库、搜索和状态查询都在独立后台进程中完成，不调用网络。
- 新快照和数据库更新采用原子事务；索引目录变化可按手动、文件变化或定时策略更新。

## Agent Skill

设置 → Skill 提供两个功能：

- 复制 Skill 路径：复制 `ukui-fences-index-query/SKILL.md` 所在目录。
- 导出 Skill：把完整 Skill 目录复制到用户选择的位置，供 Codex、Antigravity 等 Agent 安装或复用。

Skill 的统一查询入口支持 `status`、`search` 和 `fetch`。简单任务只查询索引；总结、比较和研究任务由 Agent 分轮查询并读取原文，输出必须带本地文件路径。

## 配置

- 组件位置、尺寸、主题、索引目录、排除目录、文件格式、更新策略、OCR、默认隐藏和 Provider 路径：`QSettings` 的 `smartSpace` 分组。
- Provider 配置：`~/.config/kyfences/smart-space-providers.json`。

## 验证

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j2
(cd build && ctest --output-on-failure)
```
