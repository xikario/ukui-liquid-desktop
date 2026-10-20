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

## 2026-10-19：显卡兼容策略与 OpenGL 试用

以下机型、驱动和实机验收是 X86 交付方随补丁提供的记录；本次 ARM 合入验证不将其计作本机实测。10 月 20 日的补充修复及验证见 [OpenGL 试用合入审查](OPENGL_TRIAL_REVIEW_20261020.md)。

本机 Fantasy II-M（1EC8:9810，fant-drv）可创建硬件 OpenGL 4.3 上下文，但系统显卡规则将它列为受限设备，默认使用 XRender。XRender 下 magiclamp 的支持查询返回 false；最大化动画仍可用。历史日志已有 XRender，不能说是最近一次重启才造成。具体证据见 [显卡诊断](WINDOW_EFFECTS_GPU_DIAGNOSIS_20261019.md)。

窗口动画页新增当前后端、GPU 兼容限制说明、“试用 OpenGL 动画…”和“恢复切换前的后端”。按当前桌面显卡和 KWin 实际接口判断，不按 x86/ARM 或独显/集显硬编码。该设置切换桌面合成后端，不负责混合显卡中的应用显卡选择。只在可读取 XRender 后端且试用程序已安装时提供试用。

试用由独立的 `ukui-liquid-effects-trial` 进程执行，设置页关闭或面板退出不取消它的倒计时。切换前保存目标配置项及魔壶/竞争动画的实际加载状态；切换后等待初始化，再验证 OpenGL、魔壶支持和实际加载。20 秒期限从开始切换前计算；确认按钮只有验证通过后才可用。未确认、主动恢复或验证失败都会发起恢复。保留时写用户 `Backend=OpenGL` 并启用魔壶、关闭两个竞争的挤压动画，保留 OEM 的 AutoBackend 和系统显卡规则。`loadEffect` 对已加载特效可能返回 false，因此验证先读取状态，不对已加载特效重复加载。

恢复记录为 `~/.config/ukui/liquid-effects-trial.ini`，单独操作锁防止两个试用互相覆盖。保留后仍记录切换前状态，供页面的恢复按钮使用。中断留下 pending 记录时，下次液态面板启动调用 `--recover`；恢复不成功保留记录，不宣告成功。仅恢复本模块触及的 Backend 和四个动画开关，不覆盖无关窗口特效或动画时长。辅助程序与插件一起由事务安装器备份、部署。

本机实测：独立试用并自动恢复成功；保留 OpenGL 后，测试窗口最小化及恢复阶段的 `activeEffects` 均出现 magiclamp，窗口恢复正常；KWin 配置重载后保持启用；用系统支持的 `ukui-kwin_x11 --replace` 重启 KWin，新进程仍为 gl2、magiclamp 已加载。任务栏保持 x=307、y=1152、1305×48。此次重启的是 KWin 和面板进程，没有重启整台电脑；不能将该结果写成整机重启或驱动长期稳定性验收。

最终部署后还直接点击正式设置页验证：恢复按钮回到 XRender；再次试用时出现独立倒计时窗口；不确认时自动恢复，确认“保留 OpenGL 与魔壶”后回到 gl2、magiclamp 已加载，设置页魔壶及动画时长可操作。最后保留的是 OpenGL 与魔壶，最大化动画仍尊重原来的独立关闭设置。

最终面板/公共模块 CTest 55/55 通过。新增 `panel-backend-trial` 在独立 D-Bus 和临时配置中验证真实独立辅助进程：20 秒未确认恢复、确认保留、显式恢复、后端拒绝、特效加载失败、pending 恢复和无 pending 的空操作；再补充恢复被拒时保留 journal 的回归。窗口动画测试还覆盖读取后端与 GPU 限制不修改系统，安装事务测试覆盖第七个部署文件。

复现：

```sh
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build
cmake --build apps/ukui-panel-liquid/build -j4
(cd apps/ukui-panel-liquid/build && ctest --output-on-failure -j2)
python3 apps/ukui-panel-liquid/scripts/install.py
```

本机辅助诊断命令（会改变桌面状态，应按实际需要运行）：`~/.local/bin/ukui-liquid-effects-trial --restore` 恢复切换前配置，`--recover` 只恢复中断的试用。默认无参数是带确认和倒计时的试用；`--verify-only` 验证后自动恢复；`--enable` 用于已授权的实机验收，验证通过后直接保留。

本轮测量证据位于忽略目录 `apps/ukui-panel-liquid/build/artifacts/effects-20261019/`，不随源码提交；本机安装备份见 `releases/` 中 20261019 对应 manifest。
