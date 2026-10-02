# 开始菜单 V2 · ukui-kaishicaidan

面向 UKUI X11 的独立开始菜单。透明覆盖层放在系统开始按钮上方，保留原按钮外观；点击按钮或短按 Win 键打开菜单。它不替换系统面板二进制或原 ukui-menu 插件。

本组件属于仓库 v0.6.0 发布集合，程序自身版本为 0.2.0。

[项目主页](../../README.md) · [构建](../../docs/BUILD.md) · [安装与恢复](../../docs/INSTALL_AND_RESTORE.md)

设置、应用移除确认和消息使用 [公共液态弹窗](../../shared/liquid-popup/README.md)。打开设置不会改写配置；相同参数跳过重复应用。

## 构建和试用

从**仓库根目录**执行：

```sh
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2 -DCMAKE_BUILD_TYPE=Release -DBUILD_GLASS_TESTS=ON
cmake --build apps/ukui-kaishicaidan-v2/build-v2 -j2
./apps/ukui-kaishicaidan-v2/run-v2.sh
```

开发脚本以 `--show` 启动菜单，不安装或登记自启动。直接运行二进制且不带参数时，首次启动保持隐藏，等待开始按钮或 Win 键触发。

用户安装：

```sh
cmake --install apps/ukui-kaishicaidan-v2/build-v2 --prefix "$HOME/.local"
```

安装不会自动登记登录启动或替换系统 D-Bus 服务文件。可在桌面环境的启动应用设置中添加 `~/.local/bin/ukui-kaishicaidan-launcher --autostart` 的完整路径。

## 日常操作

| 入口 | 用法 |
| --- | --- |
| 开始按钮 / 裸 Win 键 | 显示或隐藏菜单；Win 组合键继续交给系统 |
| 固定应用 | 点击启动，拖动调整顺序；图标右键固定/取消固定、打开目录、修改图标或按来源移除 |
| 全部应用 | 查看完整应用列表；可按字母、安装时间或最近使用排序，返回固定应用视图 |
| 搜索框 | 输入名称、描述或执行命令相关文字；Enter 启动第一个匹配项 |
| 最近文件 | 读取当前用户的 XDG 最近文件记录，打开文件；“更多”进入按应用分组视图 |
| 左侧文档与头像 | 打开文档目录或账户设置，头像可由 AccountsService 提供 |
| 左侧剪贴板 | 查看当前会话的文本/图片历史，点击重新复制；最多保留 20 项，菜单展示最近 10 项，可清空 |
| 左侧主题 / 设置 | 主题按钮切换皮肤；设置按钮菜单进入系统设置、关于麒麟或开始菜单设置 |
| 电源菜单 | 锁屏、睡眠、休眠、混合睡眠、注销、重启、关机；可用性依赖系统服务与权限 |

单独短按 Super_L / Super_R 才触发菜单；长按或组合键不触发。菜单外点击会收起菜单。开始按钮覆盖层通过 X11 面板事件跟随位置，面板退出时隐藏，重新出现时恢复。

应用目录变化会重新扫描，支持 XDG 应用目录、Flatpak、Snap 及用户便携应用入口。发现应用不代表支持自动卸载所有来源。剪贴板历史仅存在于当前进程，不写入历史文件；最近应用与最近文件则使用本地记录。

## 应用右键与卸载边界

右键时先显示 **正在检查安装来源…**，后台查询结束后给出对应操作。连续换目标只保留最新排队请求；已关闭菜单不接收结果，文字变化不会改变菜单尺寸。

| 识别来源 | 实际操作 |
| --- | --- |
| 可确认的用户便携应用目录 | 确认后使用 `gio trash` 将对应应用目录送入回收站；成功后清理用户入口与固定项 |
| 仅用户快捷方式 | 右键不提供移除快捷方式；启动失败且确认目标程序已丢失或不可执行时，才建议清理失效入口 |
| DEB 包 | 查找所属包并模拟移除影响；受保护的系统/桌面包被阻止；确认后使用 pkexec 授权执行 `apt-get remove`，保留配置文件 |
| Flatpak、Snap、未知来源 | 显示不支持自动卸载或无法确认的说明，需用对应应用管理工具 |

源码不会通过 `purge` 清理包配置，也不把所有用户 `.desktop` 都当作应用本体。操作前查看确认窗口中的目标路径、包名和影响；来源查询本身不会删除内容。

启动通过独立短进程调用 GLib DesktopAppInfo，读取实际桌面文件并保留带空格的 Exec、字段参数与 D-Bus 激活语义；只在接口确认成功后记录启动。失效快捷方式确认默认保留，确认后再次核对文件内容和程序状态；系统入口、D-Bus 激活和无法明确判断的包装命令不提供清理建议。删除/卸载确认前先收起开始菜单，弹窗在所属窗口层级之上显示。

## 皮肤与外观

支持深色、浅色、赛博、玻璃、壁纸色和生态液态（Eco Liquid）六种皮肤；开始菜单设置可调整字体、字号、不透明度并恢复默认。应用图标和头像使用系统/用户资源，控件与电源符号主要由 QPainter 绘制。

设置窗口使用公共液态标题栏，可拖动、最小化、关闭；主题、透明度、字体和字号按钮同步使用矢量符号，保留键盘、滚轮及长按操作。详见 [标题栏与控件验证](../../docs/DIALOG_CHROME_CONTROLS_20261002.md)。

生态液态使用 [公共光学材质](../../shared/liquid-glass/README.md)，打开时取背景快照，按钮和胶囊复用缓存。不会逐帧截图，也不与任务栏参数实时同步。GPU 不可用时使用 CPU 回退；`KAISHICAIDAN_GLASS_NO_GL=1` 禁用该组件的 GPU 材质，`KAISHICAIDAN_GLASS_FAST=1` 选择旧低成本路径。预热会前移首次 shader 编译成本，但不承诺所有硬件首次打开零延迟。

新背景异步准备完成后才显示菜单，避免首次打开或壁纸变化时先显示旧材质；准备期间再次切换可取消，已取消的任务不会重新弹出菜单。背景未变化时继续复用现有缓存，抓屏不可用时保留可读回退。详见 [弹窗层级、图标与首帧修复](../../docs/POPUP_FOLLOWUP_20261002.md)。

鼠标移动触发边缘高光与控件光泽，最多每 33 ms 合并一帧；停止移动后高光收敛并停止动画定时器。重绘只覆盖受影响的边缘、胶囊和悬停区域，鼠标在图标内部移动不再反复重绘图标和文字，悬停与按压反馈保留。性能验证方法见 [鼠标重绘优化记录](../../docs/LAUNCHER_POINTER_REPAINT_20261001.md)。

## 启动、隐藏和退出

安装后的启动器与直接运行二进制有不同的首次启动行为：

| 启动器命令 | 行为 |
| --- | --- |
| `~/.local/bin/ukui-kaishicaidan-launcher` | 已运行则切换菜单；未运行则启动驻留进程，首次仍隐藏 |
| `~/.local/bin/ukui-kaishicaidan-launcher --autostart` | 启动驻留进程；已运行则隐藏菜单 |
| `~/.local/bin/ukui-kaishicaidan-launcher --show` | 显示已有实例；没有实例时不启动程序 |
| `~/.local/bin/ukui-kaishicaidan-launcher --hide` | 隐藏已有菜单，进程保留 |
| `~/.local/bin/ukui-kaishicaidan-launcher --quit` | 正常退出已有实例，包括 Win 键监听及覆盖层 |

要首次启动就显示，可直接运行 `~/.local/bin/ukui-kaishicaidan-v2 --show`。恢复系统菜单时先用启动器 `--quit` 退出，再移除自己添加的自启动项。

主会话总线名称为 `org.ukui.kaishicaidan.v2`，对象 `/ukuiKaishicaidanV2`；同时尝试注册兼容别名 `org.ukui.menu`。别名已被系统菜单占用时不保证取得该名称，主服务仍可用。

## 配置、依赖与开发文档

配置位于 `~/.config/ukui-kaishicaidan-v2/`：`settings.conf` 保存外观，`pinned.conf` 保存固定应用，`recent-apps.conf` 保存最近应用。最近文件来自 `~/.local/share/recently-used.xbel`。XDG 环境变量会影响用户目录。

构建需要 Qt 5、X11、Xtst 和 OpenGL 开发环境；运行集成按需使用 gdbus、gsettings、GIO（含 libgio-2.0 运行库）、dpkg-query、apt-get、pkexec 及系统电源服务。兼容没有 gio launch 子命令的麒麟版本。完整 Wayland 支持尚未实现。

[历史 UI 设计](UI美化.md) · [历史 V2 验证](V2_TEST.md) · [右键菜单尺寸修复](../../docs/LAUNCHER_CONTEXT_MENU_RESIZE_20260930.md) · [安装来源查询整改](../../docs/REVIEW_ROUND2_FIXES_20261001.md) · [来源与许可](../../THIRD_PARTY_NOTICES.md)
