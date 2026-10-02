# 公共弹窗标题栏与控件补齐 · 2026-10-02

开始菜单设置此前仍显示白色系统标题栏，下拉按钮有系统白边，字号按钮使用较大的原生三角。上一轮仅统一了玻璃背景、弹出列表和有限动画，本轮补齐自主窗口的标题栏及 combo/spin 按钮，覆盖 Fences、开始菜单和任务栏主题。

## 实现与覆盖范围

| 窗口 | 本轮行为 |
| --- | --- |
| 开始菜单设置 | 玻璃内标题、拖动、最小化、关闭；主题、透明度、字体、字号按钮统一 |
| 任务栏液态外观设置 | 公共标题栏与关闭；原 FormLayout、参数预览及关闭保存行为保留 |
| Fences 设置中心 | 保留已有自主标题栏，仅最小化和关闭；内部下拉与数字调节按钮统一，不添加第二条标题栏 |
| 诊断详情 | 原有玻璃缓存保留，显式增加公共标题栏；报告内容及只读行为保留 |
| 自主确认、输入、图标选择与智能空间独立配置 | 默认接入公共标题栏，原默认按钮、取消与草稿确认保留 |
| Qt 文件/目录及颜色选择 | 保留 Qt 原生模型与验证，给 VBox/Grid 根布局预留标题空间 |
| 文件操作进度 | 保留 QProgressDialog、取消信号和显示延迟；单独适配其无根布局的内部排版 |

外部 Peony、系统控制中心、认证、独立通知/音量等窗口仍由各自产品控制。本轮不改变文件操作、诊断建议或第三方应用的行为。

公共 `Options.chrome` 默认开启。标题使用纯文本与省略显示，可访问名称保留完整标题；关闭按钮调用宿主 `close()`，继续触发 Qt 的 reject/No/Cancel、进度取消和宿主 closeEvent。自主弹窗统一普通 Window 类型，仅最小化和关闭，清除 Qt 最大化标志；双击标题也不触发最大化。保留 Qt 父子归属与模态关系，并使窗口能够被本机任务栏识别。安装后不应修改框架管理的窗口 flags。

标题区域占 44 个逻辑像素，仅在首次显示时给原根布局增加一次上边距；不重建、重排业务控件层级。QProgressDialog 使用直接子 QLabel 的内容上边距，并以 sizeHint 限定最小尺寸，防止缩小时正文或进度条进入标题区域。Fences 现有框架使用 `installMotion(window)`；诊断详情使用 `installMotion(window, true)`，两者都不生成第二份玻璃材质。

下拉和数字按钮保留 Qt 自身的命中、选中、滚轮、键盘及长按步进，绘制层完全鼠标穿透且不参与焦点。普通模式使用细线 chevron；PlusMinus 保留加减符号；NoButtons 释放按钮占用的文字空间。禁用、只读、上下限和动态范围变化同步反映到符号颜色。字体预览 delegate 保留同一对象。

## 资源与特效边界

没有新增鼠标轮询、持续动画、逐帧抓屏或不断重算的材质。标题和符号随输入、状态和尺寸变化更新；静止表面和符号的 Paint 计数回归为 0。关闭/开启动画仍为有限时长，隐藏释放表面。

材质参数、DPR 和折射计算保持原实现；现有 48 次逐像素光学参考对照继续全部相同。移动和调整窗口尺寸复用本次打开的冻结材质。以上结果验证了没有新增持续重绘，未重新测量整个桌面的 CPU/RSS，也不将统一外观本身表述为整机资源下降。

## 验证

最终 **38 个不同 CTest 入口全部通过**：Fences/公共模块 29 项、开始菜单 3 项、任务栏 6 项。共享窗口和控件覆盖 100%、125%、150%、200% 缩放；实际 launcher 入口覆盖 100%、150%、200%；任务栏覆盖 offscreen 及四档 Xvfb。独立 UKUI 控件探针在 ukui、ukui-default、ukui-dark、ukui-light 四种实际系统样式、150% 缩放下全部通过，图像检查及目视检查均未出现系统白边或实心大三角。

检查包括标题拖动、最大化按钮和标志均移除、最小化/恢复、重开不叠加上边距、关闭与 Esc 的安全取消、确认返回值、文件选择与覆盖拒绝、颜色取消、进度 canceled、父对象销毁、快速重开、隐藏释放、字体 delegate，以及点击/键盘/滚轮/长按、动态范围、只读和无按钮模式。产品入口另验证设置打开不写配置、标题关闭能保存任务栏最后待处理参数、Fences 草稿与诊断流程保持原语义。`git diff --check` 通过。

在三个 build 目录中分别运行：

```sh
ctest --output-on-failure -j2 -R '^(liquid-(dialog|controls)|desktop_(appearance|settings-center|fence-settings|smart-interaction|monitor_diagnosis|clipboard_async)|smart_space_dbus_dialogs)'
ctest --output-on-failure -j2 -R '^launcher-pointer'
ctest --output-on-failure -j2 -R '^(panel-theme|panel-corners|menu-style)'
```

## 本机安装与证据

三个宿主已备份、安装并重新加载。Fences/开始菜单正常退出，任务栏使用既有恢复工具的 SIGTERM 机制并核对进程身份后重启。两个运行二进制、任务栏已映射插件均与本轮构建 SHA-256 一致。

Fences 配置和布局文件、开始菜单设置及任务栏外观配置哈希保持一致；桌面小组件的位置、尺寸和可见性，系统监视参数，以及任务栏/开始按钮几何保持一致。本机设置窗口截图与 X11 decorations=0 属性证实已去掉系统标题栏。开始菜单设置的窗口类型为 Normal、没有 SKIP_TASKBAR，最小化进入 Iconic，本机实际点击任务栏图标可恢复；通过显示桌面（Win+D 对应的 EWMH 状态）后再次点击任务栏图标仍可恢复，标题关闭后窗口正常销毁；另外补齐 QApplication 图标、desktop 身份及 StartupWMClass，使任务栏使用有效的系统开始菜单图标。已有 desktop 文件只更新图标和身份字段，其他字段保留且原文件备份。

源码和测试：[公共框架](../shared/liquid-popup/src/LiquidDialog.cpp)、[控件符号](../shared/liquid-popup/src/LiquidComboPopup.cpp)、[框架回归](../shared/liquid-popup/tests/dialog_test.cpp)、[控件回归](../shared/liquid-popup/tests/controls_test.cpp)、[开始菜单真实入口](../apps/ukui-kaishicaidan-v2/tests/pointer_repaint_test.cpp)、[任务栏设置入口](../apps/ukui-panel-liquid/tests/panel_test.cpp)。

原始日志、源文件哈希、隔离截图、UKUI 样式探针、本机截图、三份部署记录和本机交互检查保存在 `apps/ukui-fences/build/artifacts/dialog-chrome-20261002/`。备份包含私人配置，均位于忽略目录，不随源码发布。

本轮没有提交、推送 Git 或添加版本标签。
