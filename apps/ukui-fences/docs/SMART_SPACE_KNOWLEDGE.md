# 智能空间本地知识库与命令行

知识库从文件索引中切分正文到本地 SQLite/FTS5，用于按名称、路径和正文相关度排序。它不调用远程模型；建库不会修改原文档或源索引。没有提取到正文的文件无法凭空生成全文片段。

[返回智能空间](SMART_SPACE.md) · [返回 Fences](../README.md)

## 在界面使用

先建立或更新文件索引，再在智能空间的设置 **知识库**页指定目录、片段长度与重叠长度，通过组件的更多菜单建库、查看状态或暂停。文件快照与知识库是两份数据，索引更新后需相应更新知识库。

默认目录为 `~/.local/share/ukui-fences/knowledge/`，数据库名 `smart-space-knowledge.sqlite`；XDG 数据目录或用户设置可改变它。建库写入数据库，检索和统计使用只读连接，不做全盘扫描。原始索引越完整、越新，正文检索才越可靠。

## 仓库自带脚本

以下命令均从**仓库根目录**执行。脚本为 `apps/ukui-fences/scripts/smart_space_knowledge.py`，实际子命令是 **build / search / stats**，每个命令都需要显式传入 `--db`。

```sh
python3 apps/ukui-fences/scripts/smart_space_knowledge.py --help
python3 apps/ukui-fences/scripts/smart_space_knowledge.py build --help
python3 apps/ukui-fences/scripts/smart_space_knowledge.py search --help
```

建库示例，把 `/path/to/index.json` 换成智能空间实际快照路径；自定义过知识库目录时，也需同步修改数据库路径：

```sh
knowledge_db="${XDG_DATA_HOME:-$HOME/.local/share}/ukui-fences/knowledge/smart-space-knowledge.sqlite"
python3 apps/ukui-fences/scripts/smart_space_knowledge.py build \
  --index-json /path/to/index.json --db "$knowledge_db" \
  --chunk-size 1200 --overlap 120
python3 apps/ukui-fences/scripts/smart_space_knowledge.py stats --db "$knowledge_db"
python3 apps/ukui-fences/scripts/smart_space_knowledge.py search \
  --db "$knowledge_db" --query "项目名称 方案" --limit 20
```

有对应流式快照时可用 `--index-stream /path/to/index.json.ui.bin.gz` 替代 `--index-json`。命令输出 JSON 行，包括任务、状态或结果；搜索结果含文件路径、命中片段与本地相关度。并非所有 Office/PDF 页都存在可用页码定位，读取原文确认上下文仍有必要。

独立文件索引脚本 `smart_space_indexer.py` 使用 `--roots-json` 和 `--output` 等参数，完整参数用以下只读命令查看：

```sh
python3 apps/ukui-fences/scripts/smart_space_indexer.py --help
```

不要把界面索引参数的预算与脚本默认值混为一谈。手工建库时避免与界面建库任务同时写同一个数据库。

## 外部 Skill 的统一查询入口

`ukui-fences-index-query` 是另行安装的 Agent Skill。它可能提供 `scripts/unified_search_api.py` 及 status/search/fetch 协议；该脚本**不在此仓库**，不能直接在本仓库运行旧文档中的相对命令。以实际 Skill 的 SKILL.md 为准。

外部 Agent 应先核对索引/知识库时间，再检索候选并读取原文，结论带本地来源路径。索引未更新或提取不完整时，不应把无结果解释为事实不存在。

## 隐私与验证

本地建库、检索和统计不上传正文、不保存 API Key。Provider 提取及外部 Agent 的联网边界分别见 [智能空间指南](SMART_SPACE.md#可选-provider-与外部-agent)。SQLite 中的正文片段仍是个人数据，分享数据库前需检查内容。

隔离测试：从仓库根运行 `python3 apps/ukui-fences/tests/test_smart_space_knowledge.py`，或构建目录内运行 `ctest --output-on-failure -R '^smart_space_knowledge$'`。测试使用临时快照和数据库，不读取个人知识库。
