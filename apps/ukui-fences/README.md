# ukui-fences 0.5.1 · 桌面分区与小组件

Fences 在 UKUI X11 桌面上提供文件分区、图标整理及六类小组件，统一使用桌面布局和设置中心。原 Peony 桌面保留作为底层，不需要替换系统文件管理器。项目使用 Qt 5 Widgets / C++17，按 GPL-3.0-or-later 发布。

[项目主页](../../README.md) · [构建](../../docs/BUILD.md) · [安装与恢复](../../docs/INSTALL_AND_RESTORE.md) · [v0.5.1](../../docs/versions/v0.5.1.md)

## 首次使用

以下命令从**仓库根目录**执行：

```sh
cmake -S apps/ukui-fences -B apps/ukui-fences/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-fences/build -j2
./apps/ukui-fences/build/ukui-fences
```

试用后若要安装到用户目录并登记登录启动，执行 `sh apps/ukui-fences/packaging/install-user.sh`。升级前自行备份已有二进制、配置及布局；安装脚本不会自动退出旧实例或保存历史二进制。详细步骤见 [安装与恢复](../../docs/INSTALL_AND_RESTORE.md)。

1. 桌面空白处右键 → **新建**，创建普通分区或从壁纸取色创建分区。
2. 把桌面图标拖入分区进行归组；点击标题栏箭头折叠或展开。
3. 右键 → **编辑分区布局**，移动和缩放分区及小组件；完成后选择 **退出布局编辑**。
4. 右键 → **桌面小组件**，勾选要显示的组件。
5. 右键 → **Fences 设置…**，设置外观、字体、壁纸、网格、文件同步、组件显示和启动选项。

分区归组属于桌面布局；把文件拖入真正的文件夹图标、粘贴或删除则会操作磁盘文件。布局导入导出也不备份文件内容。

## 桌面功能

| 功能 | 使用方式 |
| --- | --- |
| 分区 | 自定义名称、标题图标、背景颜色、不透明度和字体；折叠、锁定、分区内排序 |
| 布局编辑 | 移动、缩放、屏幕与相邻组件磁吸；可启用壁纸磁性画线调整分区轮廓 |
| 桌面图标 | 手动或按名称、类型、修改时间排列；网格大小在设置中调整；避开可见分区及小组件 |
| 文件操作 | 图标右键提供打开、打开方式、复制、剪切、粘贴、重命名、回收站、属性等；单图标永久删除需要确认且不可撤回 |
| 文件同步 | 自动观察桌面目录并定时对账；“文件同步”可设置新文件收纳到指定分区 |
| 刷新 | 右键 **刷新桌面** 或 F5：同步文件和元信息、恢复监听；原图标短闪约 180 ms，保留有效位置与选择 |
| 壁纸与配色 | 跟随系统或使用 Fences 自定义壁纸，支持缩放模式、壁纸取色和外部配色文件 |
| 液态图标 | **Fences 设置 → 图标与文字** 选择系统原始图标底座或液态底座；分区内部可单独启用 |
| 布局备份 | **排列与布局** 或 **Fences 设置 → 分区与布局** 导入、导出或重置布局 |

刷新不会通过销毁全部图标模拟重载；未变的壁纸继续复用材质缓存，同路径替换图片仍可更新。启动先准备壁纸和分区材质，再显示画布；智能空间现有索引在后台加载，打开组件不自动扫描文件。

## 六类桌面小组件

| 组件 | 功能与操作指南 |
| --- | --- |
| 智能空间 | [本地文件检索、索引更新、预览与配置](docs/SMART_SPACE.md)；[SQLite 知识库与脚本](docs/SMART_SPACE_KNOWLEDGE.md) |
| 系统监视 | [CPU/内存/磁盘/进程采样，API 配置、诊断详情与导出](docs/SYSTEM_MONITOR.md) |
| 时钟与倒计时 | [时间、倒计时预设、暂停继续和到点通知](docs/DESKTOP_WIDGETS.md#时钟与倒计时) |
| 活动统计 | [开机时长、前台应用停留排行、暂停记录](docs/DESKTOP_WIDGETS.md#活动统计) |
| 音乐播放器 | [MPRIS 多客户端配置、运行进程识别、自动切换及播放控制](docs/MUSIC_PLAYER.md) |
| 日历与系统待办 | [月历、农历与调休、只读系统待办](docs/DESKTOP_WIDGETS.md#日历与系统待办) |

六类组件与桌面共用一个进程。**当前显示**与**随 Fences 启动**是两个独立选项；在设置中心统一管理。隐藏音乐卡片不会停止播放器，隐藏活动卡片仍可记录；请用“暂停记录”停止统计。倒计时组件未启动时不会发出通知。

## 统一设置中心

入口为桌面右键 **Fences 设置…**，或安装后的 `~/.local/bin/ukui-fences-launcher --settings`。

- **液态外观 / 壁纸与配色 / 图标与文字**：分区材质、壁纸、主题、图标底座和字体。
- **分区与布局**：网格、锁定、字体、编辑状态、布局备份。
- **桌面小组件**及专项页面：显示、自启动、智能空间、系统监视、音乐等配置。
- **文件同步 / 帮助与维护**：新文件收纳、使用说明、关于及维护入口。

外观开关即时生效；有“应用”按钮的表单需点击后保存。切换页面保留草稿，关闭时提示放弃未应用的修改。浏览隐藏组件的专项页不会自动启动采样或索引，需要明确启用组件。

## 文件撤回的范围

桌面和分区共用最近 **50 条、本次运行内**的撤回记录。聚焦桌面或分区后按 Ctrl+Z，或使用右键 **撤回 / 撤销上一步**。支持创建、重命名、粘贴、移至回收站，以及拖入文件夹图标的移动或复制。

回收站撤回核验本次删除的元信息及文件身份，不会把外部后来删除的同路径文件当作本次记录。记录已清理、内容已修改或身份无法确认时会明确失败，需在回收站手动恢复。目录拖放撤回遇到同名冲突不覆盖，解决冲突后可再试；只保留未成功的项。永久删除和清空回收站不在撤回范围内，跨文件系统回收站恢复也不能保证支持所有 OEM GIO 实现。

## 启动器与配置路径

安装后的启动器优先调用已有实例，没有实例时启动二进制：

| 命令 | 用途 |
| --- | --- |
| `~/.local/bin/ukui-fences-launcher` | 显示 Fences 桌面 |
| `~/.local/bin/ukui-fences-launcher --settings` | 打开统一设置 |
| `~/.local/bin/ukui-fences-launcher --edit` | 切换已有实例的布局编辑；首次启动进入编辑 |
| `~/.local/bin/ukui-fences-launcher --system-monitor` | 显示系统监视 |
| `~/.local/bin/ukui-fences-launcher --smart-space` | 显示智能空间 |
| `~/.local/bin/ukui-fences-launcher --hide` | 隐藏 Fences 桌面，保留进程 |
| `~/.local/bin/ukui-fences-launcher --autostart` | 按已保存的组件启动选项恢复 |
| `~/.local/bin/ukui-fences-launcher --quit` | 正常退出；没有实例时不创建桌面 |

启动器按第一个参数分派，建议一次使用一个操作。会话 D-Bus 服务为 `org.ukui.fences`，对象为 `/ukuiFences`；可脚本调用的槽位以 [DesktopCanvas.h](src/DesktopCanvas.h) 为准。

| 数据 | 默认位置 |
| --- | --- |
| 组件与外观配置 | `~/.config/kylin/ukui-fences.ini` |
| 分区与散落图标布局 | `~/.config/kyfences/layout.json` |
| 智能空间快照 | Qt 用户缓存目录下 `smart-space/index.json` 及配套流式快照 |
| 本地知识库 | `~/.local/share/ukui-fences/knowledge/`，可在设置中改位置 |
| 活动统计 | `~/.local/share/ukui-fences/widgets/activity.json` |

XDG 环境变量可改变默认用户目录。API 密钥优先使用系统 Secret Service 密钥环；布局导出不等于密钥、索引或文件备份。

## 依赖与限制

构建依赖 Qt 5 Core/Gui/Widgets/DBus/Network、X11、OpenGL、Zlib 和 C++17；完整说明见 [构建文档](../../docs/BUILD.md)。文件操作依赖 GIO，启动器使用 `gdbus`；智能空间和日历脚本需要 Python 3。PDF/OCR 按需使用 Poppler 与 Tesseract；API 诊断使用 curl 和密钥环；音乐桌面入口使用 gtk-launch，客户端需提供 MPRIS；农历使用系统 ICU。

这是 X11 实现。主表面基于壁纸缓存而非实时后窗折射，GPU 不可用时降级。相关设计参考、shader 来源和许可证见 [第三方声明](../../THIRD_PARTY_NOTICES.md)。测试入口及历史限制见 [构建与验证](../../docs/BUILD.md) 和 [验证记录](../../docs/VALIDATION.md)。
