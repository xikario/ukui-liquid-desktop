# 智能空间拆分为AI空间 · 2026-10-19

独立仓库：[xikario/ai-space](https://github.com/xikario/ai-space)，初始版本 0.1.0。Fences 本体仍为 0.7.0，本次没有另打版本标签。

## 边界

Fences 不再编译 SmartSpaceWidget，不再安装/注册索引器、知识库脚本及其测试；相关图标资源随应用移入新仓库。系统监视、音乐、日历、时钟、活动统计仍在 Fences 内。

桌面右键增加独立的「打开AI空间…」入口，不再提供智能空间小组件勾选项；统一设置的 AI空间页面只提供打开应用/打开设置。独立版使用原有 FenceGlassRenderer 与公共材质快照，无需 Fences 在运行。

AiSpaceClient 只负责异步 D-Bus、状态缓存和按需启动。Fences 启动、浏览配置、旧 smartSpace/autoStart=true 均不自动激活独立程序。兼容旧 `showSmartSpaceWidget`、显示/位置/密度等 D-Bus 方法；状态查询是异步缓存，可有短暂延迟。`ukui-fences --smart-space` 直接打开 AI空间，不创建新的桌面进程。客户端销毁不结束独立应用。

## 迁移

首次启动 AI空间复制旧 smartSpace 设置、快照、Provider 及知识库到自己的目录；旧数据不删除、不回写。SQLite 使用一致性 backup，包含源数据库已提交的 WAL 内容。自动启动由 AI空间自己管理，不从旧 Fences 自动继承。

默认配置 `~/.config/xikario/ai-space.ini`，快照 `~/.cache/xikario/ai-space/smart-space/`，知识库 `~/.local/share/ai-space/knowledge/`，Provider `~/.config/ai-space/providers.json`。遵循 XDG 环境变量；详情见独立仓库 MIGRATION.md。

原工作区未提交的结构化文档提取增强、对应测试、olefile/xlrd 依赖和许可证已移入新仓库，并在清理原目录前备份、核对文件内容。独立版已有数据不会被迁移覆盖。旧版本 Fences 仍在运行时，源与新应用使用各自副本，不共用可写数据库。

## 验证

- 原仓库从独立 checkout 构建成功，二进制不含 SmartSpaceWidget 符号。
- `ai_space_bridge` 在私有 HOME / Xvfb / D-Bus 中真实启动新应用，验证按需启动、隐藏、位置设置、运行实例发现、旧 CLI、服务退出清缓存及缺失程序错误。
- 35 项相关回归分批通过，覆盖受影响的桌面材质、菜单、选择/拖拽、刷新、小组件、系统监视、设置、启动壁纸和视频避让回归；不沿用内嵌版智能空间测试冒充独立版通过。
- 旧菜单十项/六组件断言已更新为新增独立入口、五个内置组件。音乐封面和日历文件变化是异步完成，相关断言改为有上限等待实际状态，并先等待首次壁纸准备，保留所有行为与缓存断言。
- AI空间独立构建、三种缩放真实进程测试、SQLite WAL 迁移、结构化提取及安装目录验证见其 `docs/VALIDATION.md`。

测试未改动用户真实配置、索引或当前桌面实例。运行中的旧 Fences 不会因 Git 更新自动切换，新源码需要重新安装并重启才生效。独立 AI空间可直接从新仓库构建目录运行，或按其 README 安装。
