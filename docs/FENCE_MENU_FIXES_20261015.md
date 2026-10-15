# 分区动画白底与原开始菜单回弹修复

## 应用点击后立即退出（后续修复）

原启动辅助进程调用 g_app_info_launch，应用继承菜单 QProcess 的 stdout/stderr
管道。辅助进程结束、QProcess 销毁后，应用输出日志可能触发 SIGPIPE 并退出。
改用 GLib 2.28 的 g_desktop_app_info_launch_uris_as_manager，应用输出重定向到
/dev/null；辅助进程自身的启动错误仍反馈给菜单，保留 desktop Exec、Terminal 等语义。
回归用例延迟写 stdout/stderr 并恢复默认 SIGPIPE 处理，确认父级关闭管道后应用存活：
修复前失败，修复后通过。实际从菜单点击软件商店已确认进程和窗口正常。

## 分区展开泛白

`QWidget::grab()` 默认带 `DrawWindowBackground`。内容视口本来不填背景，
但作为动画独立图层截图时仍被填入系统 Window 调色板颜色。
改为按设备像素比创建透明 QPixmap，仅用 `DrawChildren` 渲染内容。
保留已有 X11 默认 visual 兼容处理，玻璃底层由分区绘制。

fence_features 中加入亮色 Window 调色板下静止/动画帧背景比较：
修复前失败，修复后 100% 和 200% 缩放通过；保留原抽屉反转、图标与布局持久化检查。
OEM 真实合成器展开/收起验证无白底。

## Win 键出现原系统菜单

旧菜单仍由会话管理器自动重启并持有 `org.ukui.menu`。
抓取真实消息确定系统调用 `/org/ukui/menu` 的 `org.ukui.menu.WinKeyResponse`。
V2 原有别名只有旧对象路径，且 XRecord 监听无法阻止原菜单响应。

新增 UkuiMenuCompat 适配器提供真实路径和接口。XRecord 可用时消费重复的
WinKeyResponse 通知；XRecord 不可用时回退到该通知。Activate 转到新菜单。
此适配只覆盖开始菜单激活，不实现原菜单的安全配置和包白名单查询接口。

可选用户级 install-menu-routing.py 备份并写入原菜单 Hidden 自启动覆盖、
同名 D-Bus service 的 V2 启动入口，随后 ReloadConfig 刷新当前会话缓存。
实测仅写 service 文件而不刷新时，当前总线仍可启动原菜单。
会话管理器需用完整模块名 `org.ukui.menu.desktop` 停止原菜单自动重启。
安装与恢复命令见[开始菜单说明](../apps/ukui-kaishicaidan-v2/README.md)。

隔离测试覆盖按钮跟随、Win 开关、重复通知、Super 组合和原生 Activate。
安装脚本测试覆盖空格路径、配置备份、缺少二进制时不写配置。
真实桌面连续 8 次左右 Win 正常交替开关，旧菜单进程消失；
V2 退出后通过原生 Activate 重新激活，服务所有者仍为 V2。
尚未注销重登验证，也未验证其他 UKUI 版本的所有 D-Bus 客户端。
