# 模块与数据边界

## 渲染依赖

| 界面 | 主体材质 | 菜单与提示 |
| --- | --- | --- |
| 开始菜单 | 自有 `NextKdeGlassView`，打开时背景快照 | `shared/liquid-popup` |
| Fences 分区、系统监视 | 自有 `FenceGlassRenderer`，由 DesktopCanvas 提供壁纸 | `shared/liquid-popup` |
| 智能空间 | `SmartSpaceWidget` 壁纸柔化缓存及自绘 | `shared/liquid-popup` |
| 时钟、活动、音乐、日历 | `LiquidDesklet` + `shared/liquid-glass` | `shared/liquid-popup` |
| 系统任务栏 | `WallpaperBackdrop` + `shared/liquid-glass` | `shared/liquid-popup` + PanelStyle |

智能空间、系统监视及四个新小组件都在 **ukui-fences 一个进程中**；无需各自启动后台 UI 程序。公共库静态链接：源文件只维护一份，但更新运行效果仍需逐应用重编和重启。

## 数据与外部集成

- Fences：布局、主题、启动开关写入用户目录；智能空间的索引与 SQLite 知识库在用户缓存/数据目录，原文档只读。Provider 后端代码仍支持用户主动配置的 HTTP 服务。
- 系统监视：可选 DeepSeek 诊断需要用户自行提供 API 配置，会向所选服务发送诊断请求；没有内置密钥。
- 音乐：通过 Strawberry 的 MPRIS D-Bus 接口读取及控制播放；HTTP(S) 封面按元数据下载并限时限大小。仓库不含曲库或歌曲。
- 日历：只读系统 `Schedule` 表，不写入待办、不判断任务完成、不额外发提醒。农历用系统 ICU，2026 调休数据随源码提供；其他年份没有内置官方调休表。
- 活动统计：仅累计前台应用名称与停留时间，不读取窗口标题、文档名、输入内容；统计保存在用户目录。
- 智能空间的 Agent Skill 导出是可选外部集成：需要用户另行安装 `ukui-fences-index-query` 到界面支持的路径，本仓库不打包本机 Agent 环境。索引器与知识库脚本已完整收录，可直接通过 CLI 使用。

面板配置仅控制面板进程；没有全桌面的统一参数服务。FTG340 电源策略独立于主题和小组件，不作为构建/安装主应用的前置条件。
