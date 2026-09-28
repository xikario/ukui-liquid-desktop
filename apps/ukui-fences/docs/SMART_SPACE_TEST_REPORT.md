# 智能空间当前测试报告

## 覆盖范围

- 本地关键词、正则和中文短语检索。
- 文件夹下钻、文件预览、打开和右键操作。
- 快速全量、增量、OCR 补全、暂停和断点续跑。
- 默认贴边隐藏、展开恢复、多显示器位置和设置持久化。
- 固定“全部文件”范围，不再创建、读取或写入保存空间。
- 本地知识库 build/search/stats 以及 Agent Skill 的 status/search/fetch。
- Skill 路径复制和目录导出。

## 验证命令

```bash
cmake --build build -j4
(cd build && ctest --output-on-failure)
python3 -m py_compile scripts/smart_space_knowledge.py
python3 /path/to/ukui-fences-index-query/scripts/unified_search_api.py --status --json
```

## 验收要求

- 运行期没有远程请求、模型配置或正文上传入口。
- 默认隐藏设置在重启和重新启用后生效。
- 本地知识库只包含原始文件元数据、正文片段和本地检索字段。
- Skill 导出目录包含 `SKILL.md` 和全部查询脚本，可被其他 Agent 直接复用。
