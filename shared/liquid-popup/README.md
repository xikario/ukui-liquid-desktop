# 公共液态菜单与提示 · liquid-popup

Qt 5 Widgets / C++17 静态库 `ukui-liquid-popup`，供 [开始菜单](../../apps/ukui-kaishicaidan-v2/README.md)、[Fences](../../apps/ukui-fences/README.md) 和 [任务栏主题](../../apps/ukui-panel-liquid/README.md) 使用。负责菜单材质、提示气泡、矢量符号和弹出定位，不接管应用的业务动作。

[项目主页](../../README.md) · [公开 API](src/LiquidPopup.h) · [构建说明](../../docs/BUILD.md)

## 能力与范围

- QMenu 保留 Qt 的 QAction、子菜单、键盘导航、勾选、禁用、快捷键及滚动行为，附加缓存材质与动画。
- 标准 QWidget tooltip 或显式 `showText` 使用气泡壳；Shell 可承载自定义 QWidget，按屏幕边缘翻转/约束位置。
- 对号、子菜单左右箭头与滚动箭头使用矢量绘制，支持分数缩放和 RTL。
- 提供异步菜单文字宽度预留，避免“正在查询”变成最终结果时菜单伸缩；文字与快捷键保留独立间距。
- 圆角使用按 DPR 缓存的透明度蒙版；静止时不持续运行动画或抓屏。

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
    {QStringLiteral("卸载 deb 软件包"), QStringLiteral("移除快捷方式")});
LiquidPopup::execAt(menu, button);
```

预留函数只负责尺寸。异步请求还需使用 QPointer/接收者上下文检查 QAction 和菜单寿命，不能向已经关闭的菜单写结果。

`setBackdropProvider` 接收全局区域与 DPR；`theme()` 调整圆角和动画，距离单位为逻辑像素，背景图保留 DPR。进程环境 `UKUI_LIQUID_POPUP=0` 禁用适配；运行中可用 `setEnabled(false)`，或设 `theme().reducedMotion=true` 减弱动画。

## 单独构建与预览

从**仓库根目录**执行：

```sh
cmake -S shared/liquid-popup -B shared/liquid-popup/build -DLIQUID_POPUP_BUILD_DEMO=ON -DCMAKE_BUILD_TYPE=Release
cmake --build shared/liquid-popup/build -j2
(cd shared/liquid-popup/build && ctest --output-on-failure)
./shared/liquid-popup/build/liquid-popup-preview
```

该 CTest 配置使用 offscreen，不改变个人桌面。交互预览需要可用显示环境，示例动作不修改产品设置。Fences 默认也构建、安装此预览程序。

应用链接的是静态库，修改本模块后要重新构建、安装并重启各宿主；源码共享不等于已运行进程实时更新。相关历史修复见 [菜单快捷键间距](../../docs/FENCES_MENU_SHORTCUT_20260930.md) 和 [菜单尺寸稳定](../../docs/LAUNCHER_CONTEXT_MENU_RESIZE_20260930.md)。
