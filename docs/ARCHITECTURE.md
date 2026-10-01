# 模块、进程与数据边界

[项目主页](../README.md) · [构建](BUILD.md) · [安装与恢复](INSTALL_AND_RESTORE.md)

## 应用与共享代码

| 模块 | 运行方式 | 主要依赖 |
| --- | --- | --- |
| [Fences](../apps/ukui-fences/README.md) | 桌面、智能空间、系统监视及四类 LiquidDesklet 卡片在同一个进程中 | Qt 5、X11、后台索引/日历/API 请求进程 |
| [开始菜单 V2](../apps/ukui-kaishicaidan-v2/README.md) | 独立驻留进程，透明开始按钮覆盖层与 Win 键监听 | Qt 5、X11/XRecord、会话 D-Bus |
| [任务栏主题](../apps/ukui-panel-liquid/README.md) | Qt 样式插件加载到系统 ukui-panel 进程 | 匹配的系统 Qt、OEM 面板、用户启动包装器 |
| [模糊兼容服务](../integration/peony/README.md) | 可选用户服务，事件驱动处理指定 X11 窗口属性 | Python Xlib、用户 systemd 会话 |
| [FTG340 策略](../extras/ukui-desktop-performance/README.md) | 可选管理员安装的系统 oneshot 服务与电源事件规则 | 特定驱动、sysfs、systemd、udev |

桌面主应用以普通用户运行、写入用户目录，不替换系统二进制。开始菜单主动执行 DEB 卸载时请求管理员授权；FTG340 扩展会写系统服务和驱动参数，不能归入“全部用户态安装”。

## 渲染关系

| 表面 | 材质实现 | 菜单/提示 |
| --- | --- | --- |
| 开始菜单生态液态 | 自有 NextKdeGlassView 适配层调用 shared/liquid-glass；打开时背景快照 | shared/liquid-popup |
| Fences 分区与系统监视卡片 | 自有 FenceGlassRenderer，使用 DesktopCanvas 壁纸 | shared/liquid-popup |
| 智能空间 | 自有壁纸柔化缓存与绘制 | shared/liquid-popup |
| 时钟、活动、音乐、日历 | LiquidDesklet + shared/liquid-glass | shared/liquid-popup |
| Fences 设置 / 诊断详情 | shared/liquid-glass 与缓存背景 | 共享菜单及各自控件 |
| 系统任务栏 | WallpaperBackdrop + shared/liquid-glass | shared/liquid-popup + PanelStyle |

共享模块静态链接，源码维护一份；修改后仍需重编、安装并重启各宿主。各应用参数没有跨进程统一同步服务。弹出菜单采用 CPU 缓存材质，不是所有表面都使用 GPU shader；主表面 GPU 失败会降级。

背景快照/壁纸缓存不包含实时后窗折射。任务栏可选透视只是合成器下层自然透出；系统魔壶由 KWin 提供。

## 数据与外部调用

| 功能 | 本地数据 | 外部调用边界 |
| --- | --- | --- |
| Fences 文件/布局 | 布局与配置写用户目录，文件操作直接作用于用户目标 | GIO、文件管理器、归档工具等 |
| 智能空间 | 文件只读提取，快照及 SQLite 正文片段写用户缓存/数据目录 | 默认本地；显式 Provider 可运行程序、HTTP 或 D-Bus；外部 Skill 另行安装 |
| 系统监视 | `/proc` / sysfs / statvfs 采样，配置与可选用户导出 | 主动诊断时采样发送到用户选定 HTTPS 服务；密钥环保存凭据，无内置密钥 |
| 音乐 | 已启用客户端配置、临时封面缓存，不含曲库 | 会话 MPRIS 播放控制、按元数据下载 HTTP(S) 封面、用户要求时启动/前置播放器 |
| 日历 | 只读系统 Schedule，用户节假日缓存 | 系统日历管理原事项；手动同步已发布 holiday-cn 数据，不上传待办 |
| 活动统计 | 应用名称及前台停留时长，写用户数据目录 | 不读取标题、文档名或输入；不等同于有效工作时间 |
| 开始菜单 | 应用入口、固定/最近记录；剪贴板历史仅当前进程 | 应用启动、系统设置、电源操作；卸载经来源确认和权限流程 |

智能空间文件检索、知识库搜索、系统监视 API 诊断是三个不同入口。知识库不执行远程问答；外部 Agent 是否上传选定片段由其自身环境和用户配置决定。

具体路径、字段和使用方式以各产品操作指南为准；历史报告中的文件数量、个人部署路径或性能样本不作为默认配置。
