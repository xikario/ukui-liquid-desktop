# 公共液态弹窗 · liquid-popup

Qt 5 Widgets / C++17 静态库 `ukui-liquid-popup`，供 [开始菜单](../../apps/ukui-kaishicaidan-v2/README.md)、[Fences](../../apps/ukui-fences/README.md) 和 [任务栏主题](../../apps/ukui-panel-liquid/README.md) 使用。负责菜单、对话框、下拉列表、提示气泡、矢量符号和弹出定位，不接管应用的业务动作。

[项目主页](../../README.md) · [公开 API](src/LiquidPopup.h) · [构建说明](../../docs/BUILD.md)

## 能力与范围

- QMenu 保留 Qt 的 QAction、子菜单、键盘导航、勾选、禁用、快捷键及滚动行为，附加缓存材质与动画。
- 标准 QWidget tooltip 或显式 `showText` 使用气泡壳；Shell 可承载自定义 QWidget，按屏幕边缘翻转/约束位置。
- 对号、子菜单左右箭头与滚动箭头使用矢量绘制，支持分数缩放和 RTL。
- 提供异步菜单文字宽度预留，避免“正在查询”变成最终结果时菜单伸缩；文字与快捷键保留独立间距。
- 对话框显式接入 `LiquidDialog`，共用玻璃内标题栏、拖动、关闭、材质、控件颜色和有限动画；未接入的 OEM 窗口不被全局换肤。
- 下拉及数字调节按钮共用细线矢量符号，保留 Qt 点击、长按、滚轮和键盘操作；字体选择器保留字体预览 delegate。
- 材质几何按尺寸、DPR、圆角和折射参数缓存，成本预算 2 MiB；每次打开重新取得背景，不缓存旧屏幕截图。
- 高频菜单悬停按屏幕刷新节奏合并，点击/键盘输入前提交待处理位置；静止时不持续运行动画或抓屏。

当前后端是 **CPU 缓存材质**，不是主底板的 GPU Snell shader，也不是 KWin 实时后窗折射。默认打开前采样背景；宿主可提供壁纸/已有缓存，抓取失败时使用可读回退材质。完整 Wayland 产品集成尚未实现。

## 在应用中接入

宿主 CMake 引入本目录并链接 `ukui-liquid-popup`；不要把源码再复制到每个应用。初始化示例：

```cpp
#include <QApplication>
#include "LiquidPopup.h"

QApplication app(argc, argv);
LiquidPopup::install(app);
LiquidPopup::installMenuGlyphStyle(app);
```

`installMenuGlyphStyle` 为该进程中受适配的菜单启用局部代理样式，不替换整个 QApplication 样式。面板已有自己的 PanelStyle，可直接使用 `drawMenuGlyph`；关闭液态效果时恢复原菜单样式。

按钮菜单先完成宿主样式准备，再以 `execAt(menu, button)` 按按钮锚点定位；普通鼠标右键仍可用 QMenu::exec(globalPos)。异步动作在弹出前预留所有可能标签：

```cpp
QMenu menu;
QAction *action = menu.addAction(QStringLiteral("正在检查安装来源…"));
LiquidPopup::reserveActionTextWidth(menu, *action,
    {QStringLiteral("卸载 deb 软件包"), QStringLiteral("移到回收站（文件夹版）")});
LiquidPopup::execAt(menu, button);
```

预留函数只负责尺寸。异步请求还需使用 QPointer/接收者上下文检查 QAction 和菜单寿命，不能向已经关闭的菜单写结果。

`setBackdropProvider` 接收全局区域与 DPR；`theme()` 调整圆角和动画，距离单位为逻辑像素，背景图保留 DPR。进程环境 `UKUI_LIQUID_POPUP=0` 禁用适配；运行中可用 `setEnabled(false)`，或设 `theme().reducedMotion=true` 减弱动画。

## 对话框和下拉列表

```cpp
#include "LiquidDialog.h"
LiquidDialog::Dialog dialog(parent); // 添加业务内容和按钮，继续使用 exec/open
LiquidPopup::installComboPopups(&dialog);
const auto answer = LiquidDialog::question(parent, "确认", "是否继续？",
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
```

消息、名称输入、文件/目录选择和颜色选择使用对应公共函数，保持 Qt 返回值和取消语义。文件选择使用进程内 Qt 控件；保存时的覆盖确认也走公共消息框，选择器本身不写入文件。文件进度窗可通过 `LiquidDialog::install(progress)` 接入，保留任务进度、300 ms 显示延迟和取消后续项目的原行为。

窗口本次打开只生成一次材质，移动/缩放复用冻结图像，隐藏时释放；关闭动画期间禁止重复交互，`reopen` 可取消待关闭状态。默认 `Options.chrome=true`，在原根布局上预留标题栏区域，保留 Qt 的模态、取消、文件验证和进度信号。标题关闭走 `close()`，仍会触发宿主草稿确认。自主弹窗只保留最小化和关闭，登记为普通应用窗口以供任务栏恢复，保留 Qt 模态与父子归属。

已经有独立玻璃缓存的窗口可安装 `installMotion`，避免生成第二份底板。Fences 设置保留自己的标题栏；诊断详情通过 `installMotion(window, true)` 显式加公共标题栏。普通组合框共用一个实现，特殊 API 模型菜单仍由业务控制。`installControls` 可单独统一 combo/spin 按钮，绘制层不接管输入，不启动定时器；无按钮模式和加减模式保留原语义。验证见 [标题栏与控件补齐](../../docs/DIALOG_CHROME_CONTROLS_20261002.md)。

普通弹窗继承所属顶层窗口的置顶层级，并在显示时提升和激活；不会给所有窗口一律置顶。标准按钮容器显式透明，避免 UKUI 样式在确认按钮下绘制黑色底板；输入控件保留自身底色。图标由宿主按窗口用途设置。详见 [后续修复](../../docs/POPUP_FOLLOWUP_20261002.md)。

普通原生确认框本来没有持续绘制。统一玻璃外观会增加一次打开计算和可见期间的位图内存；节省来自计算复用、事件合并、精准失效及隐藏释放，不能将换肤本身作为总体 CPU 或 RSS 下降的证据。验证记录见 [弹窗优化](../../docs/POPUP_RESOURCE_20261002.md)。

## 单独构建与预览

从**仓库根目录**执行：

```sh
cmake -S shared/liquid-popup -B shared/liquid-popup/build -DLIQUID_POPUP_BUILD_DEMO=ON -DCMAKE_BUILD_TYPE=Release
cmake --build shared/liquid-popup/build -j2
(cd shared/liquid-popup/build && ctest --output-on-failure)
./shared/liquid-popup/build/liquid-popup-preview
```

该 CTest 配置使用 offscreen；若有 Xvfb，另在独立显示中验证有限动画，不改变个人桌面。交互预览需要可用显示环境，示例动作不修改产品设置。Fences 默认也构建、安装此预览程序。

应用链接的是静态库，修改本模块后要重新构建、安装并重启各宿主；源码共享不等于已运行进程实时更新。相关历史修复见 [菜单快捷键间距](../../docs/FENCES_MENU_SHORTCUT_20260930.md) 和 [菜单尺寸稳定](../../docs/LAUNCHER_CONTEXT_MENU_RESIZE_20260930.md)。
