# 任务栏居中兼容模块验证

基线为主线 `7d67f98` 合入后的 `17b5011`。`PanelLayoutCompat` 按运行时接口能力选择后端：提供 `setAlignment` 和 `setLength` 时使用原生接口；本机 ukui-panel 3.26.0.0-0k5.63 缺少这两个接口，因此使用 QWidget 兼容后端。

兼容配置写入 `~/.config/ukui/liquid-panel.ini` 的 `layout/<panel>/` 节，原生 `panel.conf` 保持不变。默认关闭，长度限制为 30–95%，按面板名恢复。停用时解除尺寸约束并调用原生 `realign()`，恢复全宽。原生整屏底边预留区域保留不变。

本机单屏 X11 底部任务栏已在隔离的真实 `/usr/bin/ukui-panel` 上验证：

- 1920x1080 下 30/68/95% 的实际窗口分别为 576/1305/1824 像素，并保持居中。
- 关闭并重新启动面板后，68% 的实际位置和宽度恢复；仅保存配置而未测量几何不计为通过。
- 原生 `realign()`、自动隐藏进入/退出、面板高度 48、整屏 strut、关闭居中恢复全宽均通过。
- 创建 24 个测试窗口后，原生任务区出现横向溢出，翻页按钮能推进滚动视口；清除窗口后恢复。
- 连续触发任务区翻页/应用点击时，兼容层在原生 `realign()` 的几何事件内同步收敛，未观察到全宽窗口帧；原生整屏 `_NET_WM_STRUT_PARTIAL` 横向范围会在重排后恢复。
- 设置入口、主题开关、独立面板配置文件不变性和空闲期间无持续几何修正通过。
- CTest `panel-layout-compat-{1,1.25,1.5,2}`、菜单/主题回归、行为和圆角测试通过。

兼容后端只声明支持 X11 单屏底部面板。顶部、左右停靠、多屏、负坐标、主屏切换、拔插、Wayland 以及不同 OEM 任务区实现尚未验证，运行时会禁用控件并显示范围说明。原生任务栏继续维护整屏 strut；缩短窗口不等同于非矩形最大化区域。

自动隐藏的 OEM 几何动画通过 Qt 的 QRect 插值能力在兼容面板的动画运行期间保持居中；普通布局事件合并到下一事件循环，原生可见底边重排事件在事件交付期间同步收敛，不使用常驻轮询。该插值注册是进程级 Qt 能力，判断同时要求目标为当前兼容面板的底部全宽/目标矩形，其他 QRect 动画保持默认整数插值。

隔离验收命令：

```sh
ctest --test-dir apps/ukui-panel-liquid/build -R 'panel-layout-compat|menu-style|panel-theme' --output-on-failure
xvfb-run -a -s '-screen 0 1920x1080x24 -dpi 96' \
  dbus-run-session -- python3 apps/ukui-panel-liquid/tests/oem_layout_test.py
```

## 2026-10-18 复验记录

在本机再次核对了正在运行的正式桌面：麒麟 V10 SP1 2503 / amd64、X11、单屏 1920×1200、ukui-panel 3.26.0.0-0k5.63。`ukui-panel` 以 `-style ukuiliquidpanel` 运行；任务栏窗口实测 x=307、y=1152、宽=1305、高=48，符合 68%（取整）居中。`_NET_WM_STRUT_PARTIAL` 为底部 48 像素、横向 0–1919，确认屏幕预留仍覆盖整条底边。

复跑隔离的真实 OEM 面板验收脚本，结果通过：30/68/95% 实际几何及原生 `realign()`；重启后的实际位置/宽度恢复；48→64→48 高度变化；液态外观关闭/开启后居中布局保留；自动隐藏进入/退出及动画帧横坐标；24 个窗口造成的原生任务区溢出和翻页；反复点击无全宽帧；原生整屏 strut 保留；空闲 2 秒无额外几何修正；停用居中恢复全宽；兼容配置不写入原生 `panel.conf`。本轮命令为：

```sh
ctest --test-dir apps/ukui-panel-liquid/build --output-on-failure -j2
xvfb-run -a -s '-screen 0 1920x1080x24 -dpi 96' \
  dbus-run-session -- python3 apps/ukui-panel-liquid/tests/oem_layout_test.py \
  --output /tmp/panel-layout-final-acceptance-expanded
```

整套面板/公共模块 CTest 为 54/54 通过；其中四档缩放兼容回归均通过。缩放回归使用 Qt 测试面板，不是四档物理缩放下的 OEM 实机覆盖。当前这轮也没有在 OEM 面板上实际点开托盘隐藏图标弹层或系统开始菜单三级子菜单；托盘弹层的单元行为测试通过，但这两项实机集成检查仍需单独做。支持范围仍限 X11 单屏底部面板；顶部、左右、多屏/负坐标、主屏切换、拔插、Wayland 不在本次验收范围，设置控件会在这些布局禁用。不能据本次结果宣称所有显示布局或所有 OEM 版本均已通过。

安装前应使用现有事务安装脚本备份用户插件、配置和启动包装器；实机验证失败时按 release 目录中的 manifest 回滚。
