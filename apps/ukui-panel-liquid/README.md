# ukui-panel 液态主题

通过 Qt 5 QStylePlugin 为系统 `/usr/bin/ukui-panel` 添加液态底板、进程内菜单和提示样式，保留 OEM 面板的任务、开始和托盘功能。插件委托基础样式处理原有控件，不替换系统面板二进制，也不设置全局 `QT_STYLE_OVERRIDE`。

[项目主页](../../README.md) · [构建](../../docs/BUILD.md) · [安装与恢复](../../docs/INSTALL_AND_RESTORE.md)

本组件属于仓库 [v0.6.0 发布集合](../../docs/versions/v0.6.0.md)。

## 功能与设置入口

“显示任务视图”跟随系统的 `showtaskview` 开关，启动和布局变化后也保持关闭状态；关闭时一并隐藏旁边的分隔线。隐藏的托盘图标由箭头上方的独立液态弹层显示，展开和收起不挤动任务栏应用图标。弹层复用原托盘按钮和程序连接，支持原生点击、右键菜单，Esc 或点击外部收起；图标最多三列，较多时在弹层内滚动；根右键菜单贴住对应按钮上方。弹层接管展开状态，避免原生横向展开与弹层争夺图标；支持拖动排序并保存顺序。详见 [任务视图与向上展开修复](../../docs/PANEL_BEHAVIOR_FIXES_20261007.md)。

面板空白处右键 → **外观与特效**：

- **液态主题（已开启 / 已关闭）**：即时切换；关闭后恢复原面板绘制、菜单、窗口 mask 和模糊属性。
- **刷新背景材质**：重新读取背景并生成一次材质。
- **液态外观设置…**：调整圆角、背景压暗、高光、背景色彩、清晰度、液态强度、壁纸跟随、窗口透视及菜单动画。

设置立即预览；连续滑块变化按 16 ms 合并预览，停顿 180 ms、松开滑块或关闭设置时保存最终值到 `~/.config/ukui/liquid-panel.ini`，仅影响当前面板组件。背景清晰度混入原壁纸细节；液态强度控制边缘光学变化；窗口透视只调整底板透明度，文字和图标不变淡。

**自适应壁纸**开启时合并处理系统/Fences 壁纸配置及文件变化，背景实际变化才重建材质。关闭后保留本次会话缓存，仍可手动刷新；重启会读取启动时壁纸。它不自动选择亮/暗主题，也不会覆盖用户光学参数。

Fences 使用视频壁纸时，任务栏读取同一视频已缓存的首帧生成玻璃材质；切换视频或切回图片后通过文件通知自动更新，连续通知按 250 ms 合并。首帧尚未生成时暂用保存的静态壁纸，缓存生成后自动接入。任务栏不另行解码视频，也不随视频逐帧重建材质。实现与验证见 [视频壁纸同步修复](../../docs/PANEL_VIDEO_WALLPAPER_SYNC_20261006.md)。

设置窗口使用 [公共液态弹窗](../../shared/liquid-popup/README.md)。仅改变透视程度或动画选项时复用现有材质。

## 构建与安装

从**仓库根目录**执行，使用与系统面板相同版本的 Qt：

```sh
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-panel-liquid/build -j2
python3 apps/ukui-panel-liquid/scripts/install.py
```

自备 SDK 时在 CMake 配置中加 `-DCMAKE_PREFIX_PATH=/path/to/qt5/cmake`；缺少 GL 头文件时另设 `UKUI_LIQUID_GL_INCLUDE_DIR`。普通系统开发环境无需这两个路径。

安装写入用户级插件、包装器、恢复工具及两个用户自启动入口，**不会立即重启现有面板**。安装前读取全部输入并备份受管目标，备份目录为本模块 `releases/<时间>-<随机后缀>/`。暂存完成后逐文件替换；可捕获的安装异常逆序恢复旧文件、权限和符号链接，若回滚也失败会列出目标及备份位置。多文件替换不是整体原子事务；进程被强制终止时可能需按 `before.json` 手工恢复。

登录时 `~/.local/bin/ukui-panel-liquid-session` 一次性检查当前面板是否已加载插件，必要时正常停止原面板再用包装器启动；已加载则退出。手动切换也使用这个入口：

```sh
~/.local/bin/ukui-panel-liquid-session
```

登录检查带互斥锁、等待上限与失败回退，不常驻轮询。当前桌面切换成功不等于所有 OEM 会话的下次登录都已验证；日志位于 `~/.local/state/ukui-panel-liquid/session-start.log`。

## 关闭效果或恢复原面板

临时关闭可在右键菜单取消液态主题。撤销本项目自启动并立即恢复系统面板：

```sh
~/.local/bin/ukui-panel-liquid-restore --restart
```

不加 `--restart` 仅撤销受管启动入口。恢复脚本拒绝删除没有本项目标记的自启动文件，不自动还原安装前全部自定义入口；需要原定制文件时从安装备份恢复。插件、包装器和历史备份不会因此全部删除。

不要同时运行第二个面板实例；直接调用 `ukui-panel-liquid` 受系统单实例锁限制，日常切换优先使用 session 入口。

## 效果和兼容范围

底板使用缓存壁纸及 [公共光学渲染器](../../shared/liquid-glass/README.md)，按位置、尺寸、缩放与配置更新。鼠标移动只改变局部边缘反光，GPU 不可用时降级为 CPU 模糊材质。窗口透视由桌面合成器显示下层，不对其他窗口做实时折射，也不连续抓屏。

进程内 QMenu 和标准提示复用 [公共菜单模块](../../shared/liquid-popup/README.md)。独立进程的音量、网络、通知弹窗和部分 QML 预览/日历不自动换肤；菜单材质也不能等同于主底板的 GPU 渲染。

液态外观设置使用公共玻璃内标题栏，标题关闭保留最后参数的保存行为；按钮和下拉控件与 Fences、开始菜单共用实现。验证见 [标题栏与控件补齐](../../docs/DIALOG_CHROME_CONTROLS_20261002.md)。

已适配环境为麒麟 V10 / Qt 5.12 的 OEM 面板，历史包版本 `3.26.0.0-0k3.21oemccd3000m0.26.u`。系统升级继续使用新的 `/usr/bin/ukui-panel`，但接口或窗口结构变化后需重新验证，不能保证适配所有发行版面板。

## 隔离验证与源码

```sh
python3 apps/ukui-panel-liquid/tests/install_test.py
python3 apps/ukui-panel-liquid/tests/session_start_test.py
xvfb-run -a -s '-screen 0 2880x1800x24' dbus-run-session -- sh -c 'cd apps/ukui-panel-liquid/build && ctest --output-on-failure'
```

安装与登录脚本测试使用临时目录或替身服务，不改当前用户安装。UI/GPU/OEM 冒烟需要按 [构建文档](../../docs/BUILD.md) 隔离显示与会话，不直接启动到个人桌面。OEM 冒烟另需 bubblewrap 及匹配的系统面板。

主要源码为 `src/PanelStyle.*`、`src/WallpaperBackdrop.*`，部署与恢复工具位于 `scripts/` 和 `packaging/`。历史截图、性能测量和本机备份在忽略的 `artifacts/`、`releases/` 下，不随源码发布。

[材质参数与透视说明](../../docs/MATERIAL_CONTROLS_20260929.md) · [安装事务整改](../../docs/REVIEW_ROUND2_FIXES_20261001.md) · [验证记录](../../docs/VALIDATION.md) · [来源与许可](../../THIRD_PARTY_NOTICES.md)

[桌面光效 CPU 优化与画面对照验证](../../docs/DESKTOP_EFFECT_CPU_20261001.md)：保留原光效参数，合并输入与局部重绘，静止或隐藏时停止光效计时。
