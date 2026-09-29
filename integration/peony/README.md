# UKUI 系统应用魔壶动画的模糊兼容处理

在本机 UKUI KWin OEM 版本中，Peony 普通窗口请求整窗模糊。魔壶变形时，整块矩形模糊缓存仍留在原位置。真实 Peony 最小化/恢复录像确认：清除 `_KDE_NET_WM_BLUR_BEHIND_REGION` 后，矩形消失而魔壶继续正常变形。

```sh
python3 integration/peony/install-user.py
```

安装为用户服务 `ukui-peony-blur-compat.service`，随 `graphical-session.target` 启动。保留原服务名与安装路径，复用同一个进程。仅处理以下已确认受影响应用的普通窗口（X11 WM_CLASS 第一段精确匹配）：

| 应用 | WM_CLASS |
| --- | --- |
| 文件管理器 | `peony` |
| 软件商店 | `kylin-software-center` |
| 设置 | `ukui-control-center` |
| 麒麟管家 | `kylin-os-manager` |

不按 `ukui-*` / `kylin-*` 通配，避免误改 Fences、开始菜单及其他液态表面；不处理桌面、任务栏、弹出菜单类型。通过 X11 PropertyNotify 和客户端列表事件处理现有及新窗口，无定时轮询。窗口销毁竞态会被忽略。

**效果取舍：** 以上应用的普通窗口不再使用 KWin 背景模糊，透明区域仍可透出下层；其魔壶、圆角不变。其他应用、桌面小组件的液态与模糊不受影响。此兼容处理没有修改系统 KWin 或 Peony 安装文件，也没有改 GPU 频率。系统升级修复原问题后可停用。

回滚：`systemctl --user disable --now ukui-peony-blur-compat.service`，随后重新打开受影响应用窗口，让其重新申请模糊。服务安装脚本不会开启或关闭系统全局特效。

验证：`python3 integration/peony/test_blur_compat.py`。本机还向实际 Peony 窗口重新写入空模糊属性，200ms 后确认被清除；这覆盖应用再次申请属性的路径。未执行整机重启。


## 2026-09-29 扩展验证

软件商店、设置、麒麟管家的实际窗口都带有空 `_KDE_NET_WM_BLUR_BEHIND_REGION` 属性，即整窗模糊请求。复用 Peony 已验证的清除路径，安装后核对三个应用和 Peony 当前窗口均已清除；用 Xlib 对实际窗口重新写入空属性后，事件服务再次自动清除。

- `python3 integration/peony/test_blur_compat.py`：应用范围与窗口类型单元测试通过。
- `xvfb-run -a python3 integration/peony/test_blur_compat_x11.py`：现有/新建窗口、反复请求、延迟设置 WM_CLASS/窗口类型、销毁竞态及非目标表面保留模糊全部通过。
- 服务 active/enabled，复用原自启动单元；没有新增定时轮询。安装后内存约 4.8 MiB，2 秒空闲采样未观察到 CPU tick 增长；不是长期资源基准。
- 本轮确认了实际窗口属性生效，没有录制三个应用新的最小化过程或执行整机重启。

备份：`~/.local/state/ukui-liquid-desktop/system-blur-compat-20260929-184459`；安装哈希和实机验证记录：`system-blur-compat-installed.json`。
