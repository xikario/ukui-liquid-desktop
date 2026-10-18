# UKUI Liquid Desktop 当前部署与源码交接说明

> 本文保留 X86 交付机器的源码与部署记录，不代表合入后其他机器的当前状态。压缩包不含 Git 历史，文中提交号为交付方声明；主线审查及补充修复见[最终合入审查](X86_FINAL_ACCEPTANCE_REVIEW_20261019.md)。

更新时间：2026-10-18。本文用于交给其他检查者复核本机液态桌面的源码、部署、运行进程和验证边界。

## 先看结论

- 当前源码真源：/home/kylinos/src/ukui-liquid-desktop-integration
- 分支：codex/x86-desktop-fixes
- HEAD：ad6d480 fix(desktop): launch desktop entries through GIO compatibility API
- 工作区已提交且干净；运行中的开始菜单、Fences、任务栏样式均来自这棵源码对应的用户级部署。
- 本机为麒麟 V10 SP1、x86_64、X11、Qt 5.12.12-kylin、GLib/GIO 2.64.6。
- 任务栏居中已经是正式 PanelLayoutCompat 模块，不是 experiments/ 原型。本机当前为 panel1、居中开启、68%。
- OEM ukui-panel 缺少原生长度/对齐方法，因此本机按运行时能力选择 QWidget 兼容后端；不能按 CPU 架构判断支持情况。
- 桌面图标打不开的根因已定位：本机没有 gio launch 子命令，旧 Fences 仍调用该命令且误把“进程创建成功”当成“应用启动成功”。现在开始菜单和 Fences 共用 GIO DesktopAppInfo API。

## 1. 本机环境

| 项目 | 当前值 |
| --- | --- |
| 系统 | 银河麒麟桌面操作系统 V10 SP1 |
| 内核 | 5.4.18-155-generic |
| 架构 | x86_64 |
| 会话 | X11，DISPLAY=:0 |
| Qt | 5.12.12-kylin |
| GLib/GIO | 2.64.6 |
| 系统面板 | ukui-panel 3.26.0.0-0k5.63 |
| 当前屏幕 | 单屏 1920x1200，任务栏高 48 像素 |

## 2. 源码树和分支

远程仓库：https://github.com/xikario/ukui-liquid-desktop.git

当前仓库的主要提交链：

| 提交 | 内容 |
| --- | --- |
| ad6d480 | 共享桌面快捷方式启动兼容模块，修复 Fences/开始菜单应用启动 |
| 3a923fd | 屏幕变化后恢复任务栏 strut 横向范围 |
| 34e9596 | 防止原生 realign() 导致居中任务栏跳回 |
| 1576627 | 新增按接口能力选择的 PanelLayoutCompat |
| e48d405 | 壁纸源文件消失后保留 Fences 自定义壁纸 |
| 85bfc62 | 开始菜单打开工作和图标绘制缓存 |
| f94ee00 | 开始菜单冷启动和原生菜单路由处理 |
| afe2b58 | 启动辅助进程输出管道隔离，避免应用被 SIGPIPE 杀死 |
| 7d67f98 | 合入已评审 OEM/X86 修复 |

本机还有两个容易混淆的旧目录：

- /home/kylinos/src/ukui-liquid-desktop：旧 Git worktree，当前为 detached 3e1fbe5，不是正在运行的源码。
- /home/kylinos/apps/ukui-liquid-desktop：历史复制目录，包含旧构建、备份和测试产物，没有 .git，不是当前源码真源。

确认当前源码：

~~~
cd /home/kylinos/src/ukui-liquid-desktop-integration
git status --short --branch
git log --oneline --decorate -12
~~~

## 3. 三个主组件的部署

### 3.1 任务栏液态样式

源码和构建：apps/ukui-panel-liquid/、apps/ukui-panel-liquid/build/。

系统 /usr/bin/ukui-panel 没有被替换，实际通过 Qt style plugin 加载液态样式：

| 作用 | 当前路径 |
| --- | --- |
| 用户样式插件 | ~/.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so |
| 面板包装器 | ~/.local/bin/ukui-panel-liquid |
| 登录切换入口 | ~/.local/bin/ukui-panel-liquid-session |
| 恢复入口 | ~/.local/bin/ukui-panel-liquid-restore |
| 面板自启动 | ~/.config/autostart/ukui-panel.desktop |
| 登录恢复自启动 | ~/.config/autostart/ukui-panel-liquid-session.desktop |
| 兼容布局配置 | ~/.config/ukui/liquid-panel.ini |

当前进程：

~~~
/usr/bin/ukui-panel -style ukuiliquidpanel
~~~

当前布局配置：

~~~
[layout/panel1]
centered=true
centeredLength=68
~~~

关键源码：src/PanelLayoutCompat.*、src/PanelController.cpp、src/PanelStyle.cpp、src/WallpaperBackdrop.*。

PanelLayoutCompat 的行为：原生接口存在时调用 setAlignment/setLength；缺少时仅在 X11 单屏底部任务栏启用 QWidget 兼容后端。它按面板名保存配置，合并布局事件，避免常驻轮询，停用时恢复原生全宽布局，并在屏幕变化后修复 _NET_WM_STRUT_PARTIAL 横向范围。

本机已实测：30/68/95%、关闭恢复全宽、重启恢复、自动隐藏、原生 realign()、应用增减、图标溢出翻页、托盘变化、连续点击稳定性和空闲无持续布局修正。尚未声明支持多屏、负坐标、主屏切换、拔插、顶部/左右停靠、Wayland 或其他 OEM 任务区布局。当前保留整屏底部预留区域；缩短窗口不等于窗口管理器提供非矩形最大化区域。

### 3.2 开始菜单 V2

源码和构建：apps/ukui-kaishicaidan-v2/、apps/ukui-kaishicaidan-v2/build-v2/。

| 作用 | 当前路径/名称 |
| --- | --- |
| 运行程序 | ~/.local/bin/ukui-kaishicaidan-v2 |
| 启动器 | ~/.local/bin/ukui-kaishicaidan-launcher |
| 自启动 | ~/.config/autostart/ukui-kaishicaidan-autostart.desktop |
| D-Bus 主服务 | org.ukui.kaishicaidan.v2 |
| 兼容别名 | org.ukui.menu（能注册时才占用） |

当前进程是 ~/.local/bin/ukui-kaishicaidan-v2。应用快捷方式辅助入口为 --launch-desktop <desktop-file>；开始按钮和 Win 键分别经过 StartButton、KeyInterceptor、UkuiMenuCompat 路由到同一菜单实例。

### 3.3 Fences 液态桌面

源码和构建：apps/ukui-fences/、apps/ukui-fences/build/。

| 作用 | 当前路径 |
| --- | --- |
| 运行程序 | ~/.local/bin/ukui-fences |
| 启动器 | ~/.local/bin/ukui-fences-launcher |
| 自启动 | ~/.config/autostart/ukui-fences-autostart.desktop |
| 主配置 | ~/.config/kylin/ukui-fences.ini |
| 布局 | ~/.config/kyfences/layout.json |
| 壁纸副本 | ~/.local/share/ukui-fences/wallpapers/ |

当前进程是 ~/.local/bin/ukui-fences --autostart。桌面分区、图标、智能空间、系统监视、时钟/倒计时、活动统计、音乐和日历都在这个进程中。

## 4. 近期关键修复

### 桌面图标和开始菜单应用启动

本机 gio help 没有 launch，所以旧代码的 gio launch 不能用于本机。新增共享模块：

~~~
shared/desktop-launch/DesktopLaunchCompat.h
shared/desktop-launch/DesktopLaunchCompat.cpp
~~~

它运行时解析 libgio-2.0.so.0 的 GDesktopAppInfo API，支持 .desktop 文件和传入文件 URI。接入位置：

- apps/ukui-kaishicaidan-v2/src/DesktopAppLaunch.cpp
- apps/ukui-fences/src/DesktopItem.h
- apps/ukui-fences/src/DesktopIcon.cpp 的“打开方式”

实机已验证：开始菜单“软件商店”启动 /usr/bin/kylin-software-center，开始菜单“终端”启动 mate-terminal，双击桌面 Qoder CN.desktop 启动 /opt/Qoder CN/qoder-cn，双击桌面“计算机”打开 Peony。

### 刷新桌面壁纸丢失

Fences 成功加载自定义图片后，将副本保存到 ~/.local/share/ukui-fences/wallpapers/。原图消失、暂时不可读或损坏时，刷新和重启优先使用副本；原图恢复后再更新副本。详见 docs/WALLPAPER_REFRESH_FIX_20261016.md。

### 任务栏居中偶发跳动

兼容层合并 Move/Resize/LayoutRequest，在 OEM realign() 后重新套用目标矩形，避免反复移动、全宽闪帧和常驻轮询。详见 docs/PANEL_CENTERED_COMPAT_RESEARCH_20261015.md、docs/PANEL_LAYOUT_COMPAT_20261016.md。

### 本轮评审修复（50ed00f 及后续复核）

- Fences 普通文件打开现在等待 `gio open`/`xdg-open` 的退出状态；两个打开器都失败时，桌面、Fences 和智能空间入口会显示失败原因，不再把 helper 创建成功当作应用打开成功。`.desktop` 启动失败也不再退回把快捷方式文件本身交给 `xdg-open`。
- PanelLayoutCompat 在当前受支持的 X11 单屏底部后端中，直接按当前屏幕几何维护 `_NET_WM_STRUT_PARTIAL` 的横向范围，不从屏幕切换前的 OEM 属性捕获旧边界。
- UkuiMenuCompat 为原生 D-Bus 回退切换记录消费窗口；延迟到达的 XRecord 短按只消费一次，避免 81–250ms 事件延迟造成开始菜单双 toggle。
- 复核后，Fences 交互入口改为异步执行普通文件打开器，慢挂载不会阻塞桌面事件循环，失败仍通过回调提示。测试使用延迟失败的 `gio` 验证 UI 事件循环继续响应。
- Win 键去重覆盖 XRecord→D-Bus 延迟和 D-Bus→XRecord 延迟，并区分原生回退切换与 XRecord 切换；新的 XRecord 手势可清除上一条普通记录，避免快速第二次按键被吞。
- 输入时序边界：原生回退先切换后，500ms 内到达的 XRecord 与“用户立刻再按一次”无法仅凭两路事件区分；当前选择吞掉迟到事件以避免双切换，因此这个极窄窗口仍可能吞掉第二次真实按键。超过 500ms 的迟到通知也不保证去重。
- StartButton 的任务栏位置调试日志改为 `UKUI_KAISHICAIDAN_DEBUG` 环境变量开启；右键菜单改由按钮持有，按钮销毁时一并清理。

## 5. 构建、安装和恢复

从仓库根目录构建：

~~~
cmake -S apps/ukui-fences -B apps/ukui-fences/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-fences/build -j2
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2 -DCMAKE_BUILD_TYPE=Release -DBUILD_GLASS_TESTS=ON
cmake --build apps/ukui-kaishicaidan-v2/build-v2 -j2
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-panel-liquid/build -j2
~~~

任务栏插件安装：

~~~
python3 apps/ukui-panel-liquid/scripts/install.py
~/.local/bin/ukui-panel-liquid-session
~~~

Fences/开始菜单安装：

~~~
sh apps/ukui-fences/packaging/install-user.sh
cmake --install apps/ukui-kaishicaidan-v2/build-v2 --prefix "$HOME/.local"
~~~

更新已运行组件后必须退出旧实例再启动，不能只替换磁盘文件。恢复入口：

~~~
~/.local/bin/ukui-kaishicaidan-launcher --quit
~/.local/bin/ukui-fences-launcher --quit
~/.local/bin/ukui-panel-liquid-restore --restart
~~~

本机保留旧二进制回退副本：~/.local/bin/ukui-kaishicaidan-v2.bak-*、~/.local/bin/ukui-fences.bak-*。任务栏安装器的历史备份和 manifest 在 apps/ukui-panel-liquid/releases/。

## 6. 验证记录

当前 HEAD/构建目录实际执行结果：

| 验证 | 结果 |
| --- | --- |
| 开始菜单 launcher-* CTest | 12/12 通过 |
| Fences fence_features | 通过 |
| Fences desktop_refresh_1 | 通过 |
| 任务栏/公共模块全套 CTest | 54/54 通过 |
| 开始菜单软件商店、终端实机启动 | 通过 |
| 桌面 .desktop、计算机实机启动 | 通过 |
| 本轮开始菜单延迟 Win 键手势回归 | 通过（launcher-native-menu-routing） |
| 本轮 PanelLayoutCompat 1/1.25/1.5/2 倍缩放回归 | 4/4 通过 |
| 本轮 Fences 构建后集成回归 | 9/9 通过 |
| 本轮 Fences 慢打开器异步和失败回调 | 通过（fence_features） |
| 本轮面板公共模块全套 CTest | 54/54 通过 |
| PanelLayoutCompat OEM 隔离实测复验（长度、重启、高度、主题开关、自动隐藏、溢出、strut、空闲） | 通过；范围和未覆盖项见 `PANEL_LAYOUT_COMPAT_20261016.md` |

复现命令：

~~~
ctest --test-dir apps/ukui-kaishicaidan-v2/build-v2 -R '^launcher-' --output-on-failure -j2
ctest --test-dir apps/ukui-fences/build -R '^(fence_features|desktop_refresh_1)$' --output-on-failure -j2
ctest --test-dir apps/ukui-panel-liquid/build --output-on-failure -j2
~~~

Fences 构建目录的注册数量不代表全套通过；本文的实际执行范围按上表逐项记录。GPU、OEM 和真实多屏验证需要单独记录。

## 7. 建议的复核顺序

1. 确认仓库、分支和 HEAD 是 codex/x86-desktop-fixes / ad6d480，不要从旧 detached worktree 或 /home/kylinos/apps/ukui-liquid-desktop 取源码。
2. 用 /proc/<pid>/exe、cmdline、文件时间和 SHA-256 确认运行实例来自 ~/.local/bin 的新版本。
3. 检查任务栏实际窗口几何，不要只看 liquid-panel.ini；当前 68% 预期约为 1305x48、x=307、y=1152。
4. 在“液态外观设置”切换居中、30%、68%、95% 和关闭，检查实际宽度、位置和重启恢复。
5. 点击开始菜单软件商店、终端、设置；双击桌面 .desktop、计算机、回收站；右键文件测试“打开方式”。
6. 测试自动隐藏、连续点击任务栏、应用增减、托盘弹层、三级菜单、图标溢出和空闲 CPU。
7. 明确标记未测的多屏、负坐标、顶部/左右停靠、主屏切换、拔插、Wayland，不要把单屏底部结果扩展成全平台结论。

## 8. 关键文件

- apps/ukui-panel-liquid/src/PanelLayoutCompat.cpp
- apps/ukui-panel-liquid/src/PanelController.cpp
- apps/ukui-fences/src/DesktopItem.h
- apps/ukui-fences/src/DesktopIcon.cpp
- apps/ukui-kaishicaidan-v2/src/DesktopAppLaunch.cpp
- shared/desktop-launch/DesktopLaunchCompat.cpp
- docs/PANEL_CENTERED_COMPAT_RESEARCH_20261015.md
- docs/PANEL_LAYOUT_COMPAT_20261016.md
- docs/WALLPAPER_REFRESH_FIX_20261016.md
- docs/X86_INTEGRATION_20261016.md
- docs/BUILD.md
- docs/INSTALL_AND_RESTORE.md
