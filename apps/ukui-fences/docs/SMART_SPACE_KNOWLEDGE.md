# 智能空间本地知识库

智能空间在文件索引之外维护一份本地 SQLite/FTS5 知识库。原索引始终是只读输入；建库、暂停和重建不会改写原始文件或 OCR 结果。

## 数据流

1. 建库进程读取 `.ui.bin.gz` 流式索引，把有正文的文件按段落切分为片段。
2. SQLite/FTS5 保存文件元数据、正文片段和定位信息。
3. 搜索进程按文件名、路径和正文执行本地相关度排序。
4. Agent Skill 通过 `search` 获取候选，再用 `fetch` 读取原文片段并完成总结或研究。

## 保存与资源

- 默认位置：`~/.local/share/ukui-fences/knowledge/`
- 数据库和 WAL/SHM 文件按当前用户权限保存。
- 搜索和状态查询使用只读连接，不执行全盘扫描。
- 建库进程支持断点、增量复用和独立进程组控制。
- 旧版带派生字段的数据库会在下一次可写打开时迁移为纯本地正文结构，保留文档和片段内容。

## 查询协议

统一入口为 Skill 目录下的 `scripts/unified_search_api.py`：

```bash
python3 scripts/unified_search_api.py --status --json
python3 scripts/unified_search_api.py "项目名称 方案" --limit 20 --json
python3 scripts/unified_search_api.py --fetch "/绝对路径/文件.pdf" --limit 8 --json
```

输出包含文件路径、文件名、命中片段、片段定位和本地相关度。索引过期时，Skill 应提示先更新索引，不应把旧结果当成最新事实。

## 隐私边界

本地建库和检索不调用远程服务、不保存 API Key、不上传正文。外部 Agent 是否将选定片段发送给其自身模型，由外部 Agent 的运行环境和用户配置决定。
