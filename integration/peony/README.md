# UKUI 系统应用模糊兼容

可选的用户服务，针对部分 OEM KWin 中魔壶动画后残留的矩形模糊区域，清除指定应用普通窗口的 `_KDE_NET_WM_BLUR_BEHIND_REGION` 请求。不会修改 KWin / Peony 二进制，也不调整 GPU 频率。

[项目主页](../../README.md) · [安装与恢复](../../docs/INSTALL_AND_RESTORE.md)

## 范围与取舍

仅处理 X11 WM_CLASS 第一段精确匹配的以下应用，且只处理普通窗口：

| 应用 | WM_CLASS |
| --- | --- |
| 文件管理器 | `peony` |
| 软件商店 | `kylin-software-center` |
| 系统设置 | `ukui-control-center` |
| 麒麟管家 | `kylin-os-manager` |

不按 ukui/kylin 通配，不处理桌面、任务栏和弹出菜单。通过窗口/属性事件处理现有与新窗口，不定时轮询，忽略窗口销毁竞态。

效果取舍是这些普通窗口不再使用 KWin 背景模糊，透明区域仍可透出下层；魔壶、圆角和其他窗口的模糊不由本服务关闭。只在目标 OEM 存在该问题时需要启用，系统修复后可停用。

## 安装与停用

需要 X11 会话、Python 3、Python Xlib（发行版包通常为 python3-xlib）及可用的用户 systemd。在**仓库根目录**执行：

```sh
python3 integration/peony/install-user.py
systemctl --user status ukui-peony-blur-compat.service
```

安装器把脚本复制到 `~/.local/libexec/ukui-liquid-desktop/peony-blur-compat.py`，写用户服务，导入 DISPLAY/XAUTHORITY，并立即启用/重启。登录后随 graphical-session.target 运行。该动作会处理当前目标窗口的模糊请求，不是仅复制文件。

停用并取消自启动：

```sh
systemctl --user disable --now ukui-peony-blur-compat.service
```

随后重新打开目标应用窗口，让应用重新申请原模糊属性；停服务本身不会重新写回之前清除的属性。需要移除安装文件时，在停用后自行删除对应用户服务和脚本，并运行 `systemctl --user daemon-reload`。

## 隔离检查与历史证据

```sh
python3 integration/peony/test_blur_compat.py
xvfb-run -a python3 integration/peony/test_blur_compat_x11.py
```

单元测试核对目标范围与窗口类型；X11 测试验证现有/新窗口、重复申请、延迟属性、销毁竞态和非目标表面保留模糊，不改个人桌面窗口。

2026-09-29 的实机记录确认四个目标应用的属性清除与再次申请后的自动清除；Peony 的最小化/恢复观察确认矩形残留消失。该结果不等于其他 OEM 或全部应用动画均已验证，也没有为该轮执行整机重启。更多背景见 [交互修复记录](../../docs/SMART_SPACE_INTERACTION_REVIEW.md)。
