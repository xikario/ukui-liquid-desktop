# UKUI Liquid Desktop

面向银河麒麟 / UKUI X11 的 Qt 5 液态桌面组件集合。集中保存公共材质、开始菜单、桌面分区与小组件，以及系统任务栏的液态主题插件。

## 上游代码与致谢

本项目沿用、适配了 [SuceV587/NextKde](https://github.com/SuceV587/NextKde) 相关液态玻璃代码，并参考其桌面小组件设计。感谢原项目作者及贡献者的开源工作。

- **液态玻璃代码**：`glass.glsl`、`snells-glass.glsl` 的直接引入来源为 [xikario/NextKde-kylinos](https://github.com/xikario/NextKde-kylinos/tree/0dcd60cbd8723a6da95349a82fbc63385081c22a)。本项目保留这些 shader，并在 Qt 5 / UKUI X11 下适配折射、色散和高光渲染，供公共材质模块及相关应用使用。
- **桌面小组件设计**：时钟与倒计时、活动统计、日历参考 NextKde 的 DeskCenter 等组件，在本项目中以 Qt 5 Widgets 重新实现，并接入本地桌面和系统待办。
- **来源与许可**：相关第三方文件保留原始署名及 GPL v3 许可证；具体来源提交、复用范围和许可证位置见 [第三方来源与许可](THIRD_PARTY_NOTICES.md)。

## 包含什么

| 目录 | 内容 |
| --- | --- |
| `shared/liquid-popup` | 公共菜单、提示气泡、矢量对号与箭头、锚点定位、圆角抗锯齿及预览程序 |
| `shared/liquid-glass` | 公共 NextKde Snell 光学渲染器，GPU 折射/色散/高光与 CPU 降级 |
| `apps/ukui-kaishicaidan-v2` | 开始菜单、透明开始按钮覆盖层、Win 键、应用搜索与玻璃控件 |
| `apps/ukui-fences` | 桌面分区、智能空间、系统监视、时钟与倒计时、活动统计、Strawberry 音乐、日历与系统待办 |
| `apps/ukui-panel-liquid` | 加载到系统 `ukui-panel` 的 Qt 5 样式插件、壁纸适应和登录恢复 |
| `extras/ukui-desktop-performance` | 可选的 FTG340 接电调频策略，独立安装，不是通用显卡优化 |

四个新小组件统一布局编辑、边缘吸附、图标避让、壁纸取样及液态底板。音乐小组件包含最新的封面飘动音符：播放且可见时局部刷新，暂停、隐藏或播放器退出时停止。日历支持折叠待办、五行年月滚轮、农历及中国节假日；随仓库提供的调休数据为 **2026 年**，日历右键可手动同步已发布年份的最新数据。

桌面散落图标默认使用缓存液态底座，保留原始应用图标。右键 → 外观与特效 → 桌面图标样式，可切换系统原始样式、调整强度与壁纸染色；分区内部默认关闭，可单独允许。

## 构建

需要 CMake 3.16+、C++17、Qt 5 Core/Gui/Widgets/DBus/Network、X11、Xtst、OpenGL 开发头文件、Zlib，以及 Python 3。已验证环境为麒麟 V10 / ARM64 / Qt 5.12 / X11；其他发行版、OEM 面板版本和架构需自行验证，当前没有完整 Wayland 支持。

在仓库根目录分别构建三个应用，共享模块会随应用构建：

```sh
cmake -S apps/ukui-fences -B apps/ukui-fences/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-fences/build -j2
cmake -S apps/ukui-kaishicaidan-v2 -B apps/ukui-kaishicaidan-v2/build-v2 -DCMAKE_BUILD_TYPE=Release -DBUILD_GLASS_TESTS=ON
cmake --build apps/ukui-kaishicaidan-v2/build-v2 -j2
cmake -S apps/ukui-panel-liquid -B apps/ukui-panel-liquid/build -DCMAKE_BUILD_TYPE=Release
cmake --build apps/ukui-panel-liquid/build -j2
```

构建不会安装、重启应用或改变当前桌面。SDK 配置、测试和可选工具见 [构建说明](docs/BUILD.md)；安装与恢复见 [安装说明](docs/INSTALL_AND_RESTORE.md)。

## 效果边界

- 公共模块是静态库，修改后需要重新构建、安装并重启使用它的应用。各应用设置还没有跨进程实时同步。
- 开始菜单打开时采样背景；面板和桌面小组件使用缓存壁纸。不是实时透视背后所有窗口的合成器特效。
- 公共弹出菜单使用 CPU 缓存材质；GPU Snell 渲染用于主面板/小组件等指定表面。GPU 不可用时降级为模糊和染色。
- 面板插件适配 OEM `/usr/bin/ukui-panel`，仓库不包含完整系统面板源码，也不会替换该系统二进制。独立进程弹窗及部分 QML 表面不在适配范围内。
- 魔壶动画来自系统 KWin，本仓库不复制或实现 KWin 魔壶特效。

## 文档与来源

- [模块依赖与数据边界](docs/ARCHITECTURE.md)
- [源码收录范围及发布整理](docs/SOURCE_SCOPE.md)
- [本次源码验证结果](docs/VALIDATION.md)
- [代码审阅与液态图标实现记录](docs/LIQUID_ICON_REVIEW.md)
- [桌面小组件说明](apps/ukui-fences/docs/DESKTOP_WIDGETS.md)
- [第三方来源与许可](THIRD_PARTY_NOTICES.md)

项目按 **GPL-3.0-or-later** 发布；第三方文件保留各自原始许可，详见 [LICENSE](LICENSE) 和第三方声明。
