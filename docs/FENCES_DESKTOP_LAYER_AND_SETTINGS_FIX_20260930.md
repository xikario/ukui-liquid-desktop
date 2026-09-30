# 桌面层级、下拉白条与设置图标修复（2026-09-30）

## 现象与根因

- Fences 服务和组件返回 `visible=true`，并不代表用户能看见它们。真实 KWin 的 `_NET_CLIENT_LIST_STACKING` 显示 Fences 排在 Peony 系统桌面下面，整个分区和小组件被覆盖。
- 旧代码直接 Lower 两个窗口的 X11 根框架，没有同步 KWin 内部顺序；后续窗口激活或恢复时，KWin 会重新使用 Peony 在上方的顺序。
- 设置中的“切换到系统桌面”是单向动作，缺少同一位置的返回入口。
- 普通下拉沿用了菜单式 QComboBox 的私有上下滚动条。背景改成玻璃后，UKUI 对这两个子控件的白底绘制形成横条。
- 设置继承桌面进程的通用图标与任务栏标识，没有自己的设置图标。

## 修复

1. 已受 WM 管理的窗口通过 `_NET_RESTACK_WINDOW` 请求 Fences 位于 Peony **桌面类型**窗口上方；交由 WM 维护桌面层，保留普通应用和面板在上方。尚未纳入管理或无 WM 测试环境继续使用保守的底层顺序，映射后的延迟检查补充受管理请求。排除 Peony 文件窗口和菜单。
2. 运行管理切换按钮随状态改为“切换到系统桌面”或“切换回 Fences 桌面”，外部隐藏/恢复会同步按钮。隐藏桌面时设置本身仍可使用，组件实例及布局保留。
3. 下拉局部样式关闭 `SH_ComboBox_Popup` 菜单模式，使用普通 QListView / QStyledItemDelegate，保证列表度量与显示一致；保留已有模型、选择信号和键盘交互，玻璃背景仍只在打开时生成。
4. 添加嵌入式 SVG 玻璃网格/齿轮图标，提供 16–256px 多尺寸窗口图标。设置拥有独立 WM_CLASS、window role 和桌面文件标识。新 `ukui-fences-settings.desktop` 使用 `ukui-fences-launcher --settings`，已有进程复用设置窗口，不启动重复桌面进程。

## 验证要求

- DPR 1/1.5/2 下核查真实智能空间皮肤下拉：没有可见菜单滚动条、没有横贯整行的白色带、最后一行完整可见。
- 桌面切换两次，验证按钮文本、设置可见性、现有组件实例和几何保持；通过外部方法隐藏也同步返回入口。
- 真实 KWin 下检查刷新、隐藏/返回、设置最小化及普通窗口激活后的客户端堆叠。不能仅使用 `isVisible()` 判断修复成功。
- 安装后比对二进制、五个分区和四个小组件布局，并核实设置的 `_NET_WM_ICON`、WM_CLASS 与面板显示。

## 验证结果与本机应用

- 外观、分区和设置回归 7 项通过；最后修正下拉行高后，设置回归在 DPR 1、1.5、2 全部通过。实际皮肤下拉截图确认白条消失、三项文字完整显示；不是仅按像素断言推断结果。
- 真实 KWin 已验证程序重载、刷新、系统桌面往返、设置最小化、激活普通应用后的层级：Peony < Fences < 普通应用/面板。修复前堆叠顺序明确相反，服务 `visible=true` 曾导致错误的恢复判断；现增加真实窗口堆叠与桌面画面核验。
- 设置的 WM_CLASS 与 `_KDE_NET_WM_DESKTOP_FILE` 均为 `ukui-fences-settings`，`_NET_WM_ICON` 已含多尺寸图标。启动器 `--settings` 成功复用现有进程。
- 已安装二进制、启动器、设置桌面入口和图标；二进制 SHA-256 与构建产物一致。四个小组件的几何及可见状态保留，系统监视与智能空间正常。
- 备份：`apps/ukui-fences/releases/20260930-114814-before-desktop-layer-popup-icon-fix`。部署记录 `/tmp/fences-three-deployment.json`；真实窗口检查 `/tmp/fences-three-live-check.json`；回归 `/tmp/fences-three-fixes-tests.log`、`/tmp/fences-dropdown-size-check.log`、`/tmp/fences-dropdown-size-scales.log`。
- 未推送 Git，未执行系统重启。最后的设置最小化由验证脚本主动执行，桌面没有随之隐藏。

- 面板实屏复核发现 UKUI 返回的用户主题搜索路径为相对路径 `.local/share/icons`，导致仅安装主题名时仍显示默认图标。用户安装脚本现在为设置入口生成绝对 Icon 路径与带引号的绝对 Exec；本机刷新桌面文件缓存并重新打开设置后，面板已实际显示新图标，没有重启面板。截图：`/tmp/fences-settings-panel-icon-final.png`。
