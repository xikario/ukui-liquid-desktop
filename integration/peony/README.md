# Peony 魔壶动画的模糊兼容处理

在本机 UKUI KWin OEM 版本中，Peony 普通窗口请求整窗模糊。魔壶变形时，整块矩形模糊缓存仍留在原位置。真实 Peony 最小化/恢复录像确认：清除 `_KDE_NET_WM_BLUR_BEHIND_REGION` 后，矩形消失而魔壶继续正常变形。

```sh
python3 integration/peony/install-user.py
```

安装为用户服务 `ukui-peony-blur-compat.service`，随 `graphical-session.target` 启动。仅处理 X11 WM_CLASS 第一段为 `peony` 的普通窗口，不处理 Peony 桌面、菜单及其他程序。通过 X11 PropertyNotify 和客户端列表事件处理现有及新窗口，无定时轮询。窗口销毁竞态会被忽略。

**效果取舍：** Peony 普通窗口不再使用 KWin 背景模糊，透明区域仍可透出下层；其魔壶、圆角不变。其他应用、桌面小组件的液态与模糊不受影响。此兼容处理没有修改系统 KWin 或 Peony 安装文件，也没有改 GPU 频率。系统升级修复原问题后可停用。

回滚：`systemctl --user disable --now ukui-peony-blur-compat.service`，随后重新打开文件管理器窗口，让其重新申请模糊。服务安装脚本不会开启或关闭系统全局特效。

验证：`python3 integration/peony/test_blur_compat.py`。本机还向实际 Peony 窗口重新写入空模糊属性，200ms 后确认被清除；这覆盖应用再次申请属性的路径。未执行整机重启。
