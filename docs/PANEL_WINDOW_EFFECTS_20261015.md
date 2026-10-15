# 任务栏窗口动画设置（2026-10-15）

入口：任务栏右键 → 外观与特效 → 液态外观设置 → 窗口动画。

## 提供的设置

- 魔壶效果：最小化与从任务栏恢复窗口。
- 魔壶动画时长：0–3000 ms，0 为系统默认；“恢复默认时长”只恢复该项，不覆盖其他系统特效。系统全局动画速度仍影响实际时长。
- 最大化与还原动画：独立控制，避免与最小化动画混为一项。

设置影响整个桌面，动画继续由系统 UKUI KWin 实现。任务栏外观与窗口动画分为两个页签，窗口动画页首次打开时才查询系统。没有新增常驻特效计时器或轮询。

## 接口与保存

服务 `org.ukui.KWin`，对象 `/Effects`，接口 `org.ukui.kwin.Effects`。查询支持情况与实际加载状态后再启用控件；加载、卸载后复查实际状态，成功才保存对应开关。每次 D-Bus 调用异步进行，超时上限 1500 ms。系统不支持时禁用对应选项，服务缺失、超时、加载失败等显示原因。

魔壶的 ID 为 `magiclamp`；保存 `~/.config/ukui-kwinrc` 的 `Plugins/magiclampEnabled`，同时保持 OEM 的 `kwin4_effect_magiclampEnabled` 别名一致。开启时关闭竞争的 `kwin4_effect_squash` 和 `kwin4_effect_squashtablet`，关闭魔壶不自动开启另一种动画。

时长保存到 `Effect-MagicLamp/AnimationDuration`，通过 `reconfigureEffect("magiclamp")` 更新。该组名与 0 的默认值已对照本机配置模块及 [KWin 参数定义](https://raw.githubusercontent.com/KDE/kwin/Plasma/5.17/effects/magiclamp/magiclamp.kcfg)。最大化动画 ID 为 `kwin4_effect_maximize`。

切换失败尝试恢复此前运行状态；时长重配置失败则恢复旧配置值。错误时控件保持禁用，重新打开设置可重新读取。修改任务独立于设置页存活，避免关闭窗口中断保存。没有修改全局动画速度、模糊、圆角或 GPU 调频策略。

构建新增依赖 Qt5 DBus（Qt 基础开发组件中的模块）。

## 验证

本轮最终构建成功，10 项回归全部通过。其中 9 项现有回归为：`panel-theme`，1、1.25、1.5、2 倍的 `panel-corners-*` 与 `panel-behavior-*`。新增 `panel-window-effects` 在独立 D-Bus 会话和临时配置目录中通过，覆盖：

- 读取状态不修改系统；开关与独立最大化状态；时长保存和默认恢复。
- 竞争动画关闭；保留 blur 与全局速度等非目标配置。
- 加载被拒、竞争动画卸载失败后的回退；重配置失败后的旧值恢复。
- 关闭页面后完成在途保存；不支持、无服务、无响应三种状态。
- 无响应服务使用独立进程模拟，确认有界超时期间 UI 定时事件仍在处理。最初同进程模拟受 Qt D-Bus 本地调用捷径影响，已改为独立进程并重新通过，非产品失败。

真实本机 UKUI KWin 也验证了魔壶与最大化的关闭/开启、650 ms 时长及默认恢复；测试结束恢复目标配置项与四种动画原有加载状态。没有重启合成器。证据在忽略目录 `apps/ukui-panel-liquid/build/artifacts/window-effects-20261015/`。

已部署用户插件并正常重启任务栏，构建与安装哈希及新进程映射一致，原任务栏位置与尺寸保持不变。部署备份：`apps/ukui-panel-liquid/releases/20261015-091805-before-window-effects/`。
