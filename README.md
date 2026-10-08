# UKUI Liquid Desktop

面向银河麒麟 / UKUI X11 的 Qt 5 液态桌面组件集合：开始菜单、桌面分区与六类小组件、系统任务栏主题。各应用可以分别构建和启用，构建源码不会自动改变当前桌面。

**当前版本：v0.6.0 · 公共液态弹窗与桌面资源优化（2026-10-03）**。Fences 及其关于页版本为 0.6.0；开始菜单和面板保留各自的组件版本号。

[下载与版本说明](https://github.com/xikario/ukui-liquid-desktop/releases/tag/v0.6.0) · [更新日志](CHANGELOG.md) · [v0.6.0 详细说明](docs/versions/v0.6.0.md)

## 选择要使用的产品

| 产品 | 可以做什么 | 使用说明 |
| --- | --- | --- |
| **Fences 桌面** | 桌面文件分组、分区折叠、图标整理、文件操作与撤回、六类桌面小组件、统一设置 | [Fences 首页](apps/ukui-fences/README.md) · [桌面右键与设置](apps/ukui-fences/docs/DESKTOP_MENU.md) |
| **开始菜单 V2** | 开始按钮和 Win 键打开菜单、固定应用、搜索、最近文件、主题及安装来源查询 | [开始菜单首页](apps/ukui-kaishicaidan-v2/README.md) |
| **任务栏液态主题** | 为现有系统任务栏添加液态底板、菜单与提示，调整清晰度、强度和壁纸跟随 | [任务栏首页](apps/ukui-panel-liquid/README.md) |

Fences 的小组件都运行在同一个 Fences 进程中；无需另装六个独立应用。桌面空白处右键 → **桌面小组件** 控制显示，**Fences 设置 → 桌面小组件** 分别设置当前显示和随 Fences 启动。

| 小组件 | 主要功能 | 操作指南 |
| --- | --- | --- |
| 智能空间 | 本地文件索引、正文检索、文件夹浏览、按需预览与本地知识库 | [智能空间](apps/ukui-fences/docs/SMART_SPACE.md) · [知识库与 CLI](apps/ukui-fences/docs/SMART_SPACE_KNOWLEDGE.md) |
| 系统监视 | CPU、内存、磁盘、进程趋势；可选 API 诊断；结构化详情、复制和导出 | [系统监视与诊断](apps/ukui-fences/docs/SYSTEM_MONITOR.md) |
| 时钟与倒计时 | 时间、日期、倒计时预设、暂停继续与到点通知 | [时钟指南](apps/ukui-fences/docs/DESKTOP_WIDGETS.md#时钟与倒计时) |
| 活动统计 | 开机时长与应用前台停留时间，可暂停记录 | [活动统计指南](apps/ukui-fences/docs/DESKTOP_WIDGETS.md#活动统计) |
| 音乐播放器 | 已配置的 MPRIS 客户端自动接入、封面、播放控制、进度与音量 | [音乐配置与使用](apps/ukui-fences/docs/MUSIC_PLAYER.md) |
| 日历与系统待办 | 月历、农历、调休、只读展示系统日历事项 | [日历指南](apps/ukui-fences/docs/DESKTOP_WIDGETS.md#日历与系统待办) |

音乐支持手动打开一次客户端后自动识别启动信息；多个已启用客户端同时运行时优先接入正在播放的，其次暂停的；同状态选择最后启动的。播放状态变化或客户端退出后自动重新选择。系统监视诊断提供只读建议与命令复制，不执行模型返回的命令。

## 构建、安装与恢复

完整克隆仓库后，在**仓库根目录**执行。需要 CMake 3.16+、C++17、Qt 5、X11/OpenGL 开发环境及 Python 3；各产品的附加依赖见 [构建说明](docs/BUILD.md)。

```sh
cmake -S apps/ukui-fences -B apps/ukui-fences/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-fences/build -j2
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2 -DCMAKE_BUILD_TYPE=Release -DBUILD_GLASS_TESTS=ON
cmake --build apps/ukui-kaishicaidan-v2/build-v2 -j2
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-panel-liquid/build -j2
```

只想试用时，可运行构建出的 Fences，或用开始菜单的开发启动脚本；任务栏插件需要安装并由系统面板加载。具体入口、退出方式、登录启动和回退步骤见 [安装与恢复](docs/INSTALL_AND_RESTORE.md)。不要把“构建完成”当成“运行中的旧实例已经升级”。

已验证的主要环境为 **麒麟 V10 / ARM64 / Qt 5.12 / X11**。其他发行版、架构、OEM 面板版本需另行验证，当前没有完整 Wayland 支持。验证结果与限制见 [验证记录](docs/VALIDATION.md)。

## 可选集成与公共模块

| 模块 | 用途与边界 |
| --- | --- |
| [系统应用模糊兼容](integration/peony/README.md) | 独立用户服务，处理指定 UKUI 应用普通窗口的矩形模糊残留；不修改 KWin 或 Peony 二进制 |
| [FTG340 电源策略](extras/ukui-desktop-performance/README.md) | **管理员权限的独立扩展**，仅针对匹配驱动的接电最低频率；不是通用显卡优化，也不随主应用安装 |
| [公共液态菜单](shared/liquid-popup/README.md) | 菜单、提示气泡、矢量符号、锚点定位与预览程序 |
| [公共光学材质](shared/liquid-glass/README.md) | 缓存背景上的 GPU Snell 折射、色散、高光及 CPU 降级 |
| [后台任务生命周期](shared/async-work/README.md) | 控件销毁后停止投递结果，应用退出时等待后台任务结束 |

桌面主应用安装到用户目录，不替换系统桌面或面板二进制。用户主动卸载 DEB 应用时会请求管理员授权；可选 FTG340 扩展会写入系统服务、规则和驱动参数，其权限边界与主应用不同。

## 数据与视觉效果边界

- 智能空间界面使用本地索引和知识库，不提供远程模型重排或多空间管理；显式配置的 Provider 可调用本机程序或远程服务。外部 Agent Skill 需另行安装。
- 系统监视的本地采样不需 API；主动发起智能诊断时，采样数据会发送到所配置的服务。密钥由用户提供，优先保存在系统 Secret Service 密钥环。
- 音乐通过会话 MPRIS 接口控制用户自己的播放器；网络封面按元数据下载。日历只读系统待办，活动统计不记录窗口标题或输入内容。
- 开始菜单打开时取背景快照；任务栏和桌面小组件以缓存壁纸绘制。可选窗口透视由合成器显示下层，不对其他窗口做实时折射。
- 公共弹出菜单使用 CPU 缓存材质。GPU 不可用时主表面降级；各应用的外观参数尚未跨进程实时同步。
- 任务栏插件不自动改造独立进程弹窗和部分 QML 表面。魔壶动画来自系统 KWin，本仓库不实现该动画。

更多模块关系与数据路径见 [架构说明](docs/ARCHITECTURE.md)；源码收录范围见 [发布范围](docs/SOURCE_SCOPE.md)。带日期的设计、评审和部署文档是历史记录，当前使用方式以各产品首页及操作指南为准。

## 上游代码与致谢

本项目沿用、适配了 [SuceV587/NextKde](https://github.com/SuceV587/NextKde) 相关液态玻璃代码，并参考其桌面小组件设计。感谢原项目作者及贡献者的开源工作。

- **液态玻璃代码**：`glass.glsl`、`snells-glass.glsl` 的直接引入来源为 [xikario/NextKde-kylinos](https://github.com/xikario/NextKde-kylinos/tree/0dcd60cbd8723a6da95349a82fbc63385081c22a)。本项目保留这些 shader，并在 Qt 5 / UKUI X11 下适配折射、色散和高光渲染，供公共材质模块及相关应用使用。
- **桌面小组件设计**：时钟与倒计时、活动统计、日历参考 NextKde 的 DeskCenter 等组件，在本项目中以 Qt 5 Widgets 重新实现，并接入本地桌面和系统待办。
- **来源与许可**：相关第三方文件保留原始署名及 GPL v3 许可证；具体来源提交、复用范围和许可证位置见 [第三方来源与许可](THIRD_PARTY_NOTICES.md)。

项目按 **GPL-3.0-or-later** 发布；第三方文件保留各自原始许可，详见 [LICENSE](LICENSE) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。
