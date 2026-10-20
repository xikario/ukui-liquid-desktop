# X86 交付方窗口动画能力诊断（2026-10-19）

本文保留交付方随补丁提供的 Fantasy II-M 机器记录。下文“本机”均指该 X86 机器；本次 ARM 合入未在该 GPU 上重新验收，不能据此推断其他驱动或 OEM 包的支持情况。合入验证见 [2026-10-20 审查](OPENGL_TRIAL_REVIEW_20261020.md)。

## 结论与实测

本机 Fantasy II-M（PCI 1EC8:9810，内核驱动 fant-drv）具备硬件 OpenGL 能力，但当前 OEM KWin 按显卡兼容规则选择 XRender 并限制特效。不能将其描述为硬件不支持动画，也没有证据证明是最近一次重启才发生的变化。

- 独立 GLX 上下文创建和绑定成功，direct=true；GL_VENDOR=Fantasy，GL_RENDERER=Fantasy II-M，GL_VERSION=4.3 build 23.3@78987244，GLSL=4.30。未切换桌面合成器。
- `/Compositor` 的 `org.ukui.kwin.Compositing` 返回 active=true、compositingType=xrender、openGLIsBroken=false。
- `getGPUStatus` 返回 hasRestrictedDevice=true、devicesRestrictedCanUse=true、hasNormalDevice=false、shouldBeXRenderBackend=true，VID=1EC8、PID=9810。
- `/etc/xdg/kwin-quirk.list` 的 GpuList 包含 `1ec8:*:Plugins-disable:Description=innosilicon`，与本机厂商 ID 匹配。
- `~/.log/ukui_kwin_0.log` 记录 restricted GPU、pluginsID="disable"、setbackend="XRender"。`~/.config/ukui-kwinrc` 的 Backend、AutoBackend 均为 XRender。
- `~/.log/kwin_backend_auto_config_0.log` 从 10 月 9 日已有 XRender 记录；自动检测器发现显卡身份与缓存相同后跳过重测。
- Effects 接口实时返回 magiclamp supported=false、loaded=false；kwin4_effect_maximize supported=true、loaded=false。

因此魔壶禁用符合当前 KWin 的能力返回；最大化动画可独立开启。页面笼统的“当前系统不支持”容易被误解为硬件永久不支持，应区分当前合成环境不支持与连接失败。

## 后续验证边界

硬件 GLX 上下文成功不等于 KWin OpenGL 合成、纹理导入及魔壶实际渲染已通过。规则为什么覆盖整个厂商、是否针对旧驱动，现有证据无法确定。修改 Backend 单项也不能保证登录或显卡事件后不被 OEM 规则重新选择。

后续若要启用，应先在隔离显示环境测试 KWin OpenGL 路径，再对当前驱动做实机最小化/恢复及稳定性验证，并保留原配置和自动恢复路径；不能仅删除兼容规则或强制解锁控件就当作适配完成。本轮只进行了能力和日志诊断，没有修改系统显卡规则、后端或特效开关。

## 后续实现结果

随后已按用户要求实现 OpenGL 试用及恢复模块，并在本机成功保留 gl2 和魔壶。实际测试窗口最小化、恢复时均检测到 magiclamp 活跃；重启 KWin 进程后仍保持。系统显卡规则未修改。源码、设置页及测试均已更新，详情见 [窗口动画设置](PANEL_WINDOW_EFFECTS_20261015.md) 的 2026-10-19 节。以上“本轮只进行诊断”描述指最初诊断阶段，不代表后续实施阶段。

10 月 18 日的旧 ZIP 不包含本次实现，应使用 10 月 19 日的新源码包。
