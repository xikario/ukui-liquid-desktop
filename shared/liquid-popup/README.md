# UKUI 公共液态气泡 v0.2

2026-09-27。Qt 5 Widgets / C++17，四个界面引用一份源码。

## 已实现

- `LiquidPopup::Shell`：圆角主体与短圆润连接颈合并轮廓、锚点展开、内容淡入、关闭回收；屏幕边缘翻转/约束，支持负坐标显示器。
- `renderMaterial`：背景缓存、柔化、沿圆角法线的边缘采样偏移、自适应深浅底色和方向高光。当前是 CPU 缓存图像后端，不是 KWin 实时折射，也不是 NextKde shader 的直接移植。
- `install(app)`：给应用内 QMenu（包括 Qt 创建的子菜单）附加材质与 X11 淡入，保留 Qt 的 QAction、exec、菜单导航、勾选、禁用项、快捷键与滚动行为；普通 QWidget tooltip 自动转入气泡。
- `showText/hideText`：兼容现有应用显式 QToolTip 调用的主要参数。
- `setBackdropProvider`：可注入壁纸或已有后台缓存；默认 X11 打开前采样。Wayland/抓取失败使用可读纯色材质。
- 静止时不运行动画定时器，不连续抓屏。主题参数位于 `LiquidPopup.h` 的 `Theme`；`theme()` 可在初始化时配置。
- `UKUI_LIQUID_POPUP=0` 可在进程启动时回到原生菜单和 QToolTip；`theme().reducedMotion=true` 关闭动画。

## 接入位置

开始菜单：`../../apps/ukui-kaishicaidan-v2`。

Fences、智能空间、系统监视：`../../apps/ukui-fences` 中共用一个主程序。面板插件位于 `../../apps/ukui-panel-liquid`。

三个应用 CMake 入口均 `add_subdirectory(UKUI_LIQUID_POPUP_ROOT)` 并链接 `ukui-liquid-popup`。源码没有复制到应用目录；分发源码包必须带上本模块，或者显式设置 `-DUKUI_LIQUID_POPUP_ROOT=/path/to/liquid-popup`。静态链接后每次修改本模块需要重编译对应应用。

应用入口仅调用 `LiquidPopup::install(app)`；开始菜单、SmartSpace、SystemMonitor 的显式 QToolTip 调用改为公共接口。主面板、布局、业务 QAction 均未重写。

## 构建与预览

```sh
cmake -S . -B build -DLIQUID_POPUP_BUILD_DEMO=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
cd build
ctest --output-on-failure
./liquid-popup-preview
```

机器上的 Qt SDK 若不在标准目录，需设置 `CMAKE_PREFIX_PATH`。预览含四个界面入口，悬停为气泡，点击为菜单；`--snapshot` 输出静态预览。

独立应用验证构建目录均为 `build-liquid-popup`。v0.2 已部署到开始菜单的 `build-v2/ukui-kaishicaidan-v2` 和 `~/.local/bin/ukui-fences`，并已重启两者。备份、日志和已安装文件校验和在 `releases/20260927-v02/`。

## 当前边界

- 气泡 Shell 有展开/收回，QMenu 保留原生关闭时序，只有淡入。延迟 QMenu::exec 返回或延后 QAction 执行会改变业务生命周期，v0.1 不这样做。
- 内容在后段淡入；暂未引入 overshoot、动态液滴融合、持续背景更新或实时 GPU 后端。
- 处理应用自己的 QWidget tooltips、item view 的 ToolTipRole、启用提示的菜单 QAction，以及显式替换的调用；不修改四个目标界面之外的应用。
- 菜单采样在 aboutToShow 前准备、Show 时按最终窗口坐标裁切；当前采样选取鼠标所在显示器，键盘在另一个屏幕打开菜单会降级为纯色材质，后续可由应用注入精确背景来源。
- 长文本超出屏幕时受窗口尺寸约束，尚无 tooltip 滚动视图；普通短说明是本版目标。
- Xvfb/offscreen 测试能验证功能和像素，不等于 FTG340 合成桌面的人工视觉验收。

## 回退与接续

### 2026-09-28 侧栏气泡自适应修复

- 公共 `showText` 按宿主内的窄侧栏区域选择向内展开；`Shell::openAt` 支持 `Auto/Above/Below/Left/Right`，空间不足时尝试其他方向并约束在可用屏幕内。
- 侧向尖角和展开起点朝向锚点。短气泡保留无尖角圆角外形，避免尖角挤占圆角；X11 映射后再次应用目标位置。
- 定位只在打开时计算，继续使用单次背景采样，没有新增常驻轮询或逐帧抓屏。
- 本次应用目标为开始菜单 `build-v2/ukui-kaishicaidan-v2`，自启动入口指向同一路径。其他程序静态链接本模块，需要重新构建、部署和重启才会获得本次修复，不能仅凭公共源码已更新认定已生效。
- 回归覆盖左右尖角方向、屏幕边缘换向及原有菜单、DPR、动画中断行为。

`integration-backup/` 保存本次接入前涉及文件的副本（包括原有未提交修改）；这是历史快照，不应在后续有新修改时整文件覆盖回退。

后续优先：真机观察 → 调整公共 Theme → 引入独立 menu/tooltip/panel 预设 → 将已有 GPU 光学能力接到公共 renderer。源码检查点以本文件为准；会话压缩由宿主控制，不能在这里承诺精确的 270K 触发阈值。

## v0.1 验证记录

- 独立模块 Release 构建通过。
- `liquid-popup` offscreen 测试通过；同一测试在 Xvfb/X11 下再次通过（屏幕边界、DPR、缓存采样次数、子菜单、键盘勾选、动画中断重开）。
- 开始菜单 `build-liquid-popup/ukui-kaishicaidan-v2` 构建通过。
- Fences `build-liquid-popup/ukui-fences` 与 `fence-glass-test` 构建通过。
- 现有 `fence_glass_cpu` 回归通过，包括实际桌面菜单的液态开关操作。
- `fence_glass_gpu` 在隔离 Xvfb 环境缺少可用 OpenGL context，按原测试约定跳过（不是 GPU 测试通过）。
- 已在实际桌面打开独立预览程序，供人工观察；未部署两个应用的新二进制。
- 屏幕快照：`build/artifacts/bubble.png`、`menu.png`、`preview.png`。

## v0.2 修复与部署（2026-09-27）

- `execAt(menu, anchor)` 以按钮全局矩形居中定位，底部空间不足时翻到上方；用于预览按钮、智能空间更多/排序、监视器皮肤选择。右键位置和原生子菜单仍由 Qt 管理。
- 点击/按键立即隐藏说明气泡，避免菜单截图背景包含正在退出的提示框。
- 文件列表 ToolTipRole 和 QAction 帮助统一接入；文字按实际字体测量宽度，避免 CPU 型号末尾孤字换行。
- offscreen、Xvfb、150% 缩放下公共测试通过；Fences CPU 回归通过。
- 两个实际运行程序已更新，启动日志确认 v0.2 公共模块加载。已真机悬停系统监视 CPU 环验证液态气泡与完整型号文本，证据为 `releases/20260927-v02/monitor-live-full.png`。
- 旧开始菜单的 D-Bus quit 请求未正常退出，使用 SIGTERM 重启；Fences 使用自身 quitApp 正常保存布局并退出。备份未覆盖。
- 后续仍可继续调公共材质；目前为缓存光学效果，不是合成器实时玻璃。


### ukui-panel 接入（2026-09-27）

`code-work/ukui-panel-liquid`通过用户级 Qt 主题插件适配系统 OEM 面板。新增 `LiquidSurface.h` 提供常驻面板的透明缓存材质，无抓屏和持续动画；背景模糊沿用合成器。`setEnabled(bool)` / `isEnabled()` 提供进程内运行时启停，关闭菜单时恢复其原始样式与遮罩。

面板的设置单独存于 `~/.config/ukui/liquid-panel.ini`；尚未实现跨应用设置同步。本次面板插件链接新版公共库，其他已安装应用继续运行此前验证的版本。


v1.1 性能修正：QMenu 在 Show（定位完成、原生映射前）只采样自己的矩形，取消整屏快照。LiquidSurface 使用窄幅反射边缘，避免大范围金属渐变；不实现实时场景折射。面板插件已部署此版本，其余应用仍运行此前验证的二进制。

2026-09-28 菜单抗锯齿修复：新增 `renderMenuMaterial` 在 DPR 分辨率上一次性缓存圆角 alpha 和边缘高光；取消沿着可见圆角的硬 painter clip，将原生 QRegion 移到抗锯齿轮廓之外。修复 150% 缩放下的阶梯边，圆角大小不变。1×/1.5×/2× 覆盖测试、菜单交互和 Xvfb 回归通过，已部署 ukui-panel 插件，其他应用未重新部署。

同日菜单符号与状态补齐：新增 `drawMenuGlyph` 供宿主 QStyle 接入勾选和四向箭头，采用圆头、圆角路径，按当前 DPR 直接抗锯齿绘制，遵守正常/选中/禁用调色板。面板主题已接入；其他宿主需接入后重编才会使用矢量符号。菜单样式明确勾选列、文字和箭头留白、圆角选中底色及分隔线，避免小位图放大和状态底色丢失。布局样式在 `aboutToShow` 和 `execAt` 测量前准备，防止菜单大小变化造成锚点偏移。菜单开关关闭后恢复原样式。

2026-09-30 动态菜单尺寸修复：应用安装来源查询在弹出后改变 QAction 文本时，同步重建当前尺寸的材质与原生圆角轮廓，并限制更新后的位置在打开时所在屏幕内。复用打开前的单次背景采样，保留重叠区域的像素位置，未采到的新增区域只延展原背景边缘；不抓取已显示的菜单，也不拉伸整张背景。关闭时释放背景和材质。回归覆盖变窄/变宽、重复开关、四角透明、两侧完整、屏幕边缘约束和采样次数，offscreen 与 X11 的 1×/1.5×/2× 均通过。已重新构建并部署开始菜单，实机 WPS 右键菜单连续三次检查通过；其他静态链接宿主需要重新构建后生效。

同日追加稳定宽度接口：reserveActionTextWidth(menu, action, texts) 在菜单隐藏时按最终菜单样式测量异步状态项的可能文字，预留最大所需宽度后恢复原文。开始菜单在安装来源查询前使用该接口，避免“检查中”切换为短结果时收缩；其他动态菜单继续保留原生调整尺寸的能力。回归验证从弹出到结果更新无 Resize/Move 事件，实际开始菜单在 X11 三档缩放的整个延迟查询过程均只出现一种几何，实机 PhyFusion 复验也保持尺寸。

2026-09-29 Fences 接入：独立 Qt 应用可在 `install(app)` 前调用
`installMenuGlyphStyle(app)`，通过代理样式复用矢量勾选与箭头；Fences 已接入。
液态菜单打开时临时隐藏可勾选动作的装饰图标，让 Qt 的状态列始终显示对号，
关闭后恢复动作原本的图标设置。原生键盘、勾选和单选行为保留，菜单圆角不变。

`installMenuGlyphStyle()` 只开启每个液态菜单拥有的局部代理，不替换 QApplication 样式。`UKUI_LIQUID_POPUP=0` 同时禁用适配器与该接入；关闭或销毁菜单时恢复仍存活的外部 QAction 图标偏好。

2026-09-30 Fences 快捷键重叠修复：统一预留最长标签、公共图标列和快捷键间隔，
修正 Qt 5 样式在隐藏图标时测量与绘制空间不一致导致的“撤回 / Ctrl+Z”重叠。
动作或字体变化后重新测量，保留原生快捷键和异步菜单稳定宽度。
新增实际文字绘制范围检查，覆盖真实分区内容区、标题栏、桌面菜单及三档缩放；
详见 [修复和部署记录](../../docs/FENCES_MENU_SHORTCUT_20260930.md)。
