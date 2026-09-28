# 分区全局液态玻璃（测试版）

入口：**桌面空白处右键 → 桌面设置 → 全局液态玻璃（所有分区）**。

默认关闭。切换立即作用于已有分区，新分区继承；重启后保持选择。
配置为 QSettings 的 `appearance/fenceLiquidGlass`，不改变布局 JSON
的格式或已有颜色、文件归属。关闭恢复原有样式。智能空间的皮肤独立配置。

## 材质

- 复用开始菜单 V2 的 Qt5 适配器和 NextKde Snell 折射、色散、边缘高光。
- 采样桌面壁纸，不捕获其它窗口或分区里的文字、图标。
- 全部分区共享一次壁纸扩散缓存和一个 OpenGL 后端。
- 每个分区缓存自己的材质；位置、大小、轮廓或壁纸变化时重新生成。
  最近一次异形距离场单独缓存，同形状平移不会重新计算轮廓距离。
- 鼠标靠近轮廓时有局部反光；离开渐隐，静止不重复重画，无全桌面动画循环。
- 异形磁吸边缘使用局部有符号距离纹理计算法线和折射，保留原形状；
  扩展一像素的输入区域以免整数区域裁掉抗锯齿边缘。
- 玻璃预设使用中性底色，原有分区颜色/透明度保留给关闭玻璃时使用。
- 无可用 OpenGL 时回退到带抗锯齿的模糊底色，不宣称有真实折射。
  可用 `UKUI_FENCES_GLASS_NO_GL=1` 强制验证回退。

## 构建与验证

只构建测试目录，不执行 install，不修改生产二进制、自启动或运行中的桌面。
最小麒麟 SDK 若缺少 `GL/gl.h`，配置时提供
`-DUKUI_FENCES_GL_INCLUDE_DIR=<含 GL/gl.h 的 SDK include 目录>`。

```sh
cmake -S . -B build-smart-space-glass
cmake --build build-smart-space-glass -j2
cd build-smart-space-glass
env -u PYTHONPATH ctest -R 'fence_glass_|smart_space_(glass|new_features)_smoke' --output-on-failure
```

UI 测试使用独立 Xvfb、D-Bus 会话与临时配置，验证开关、新建/恢复分区、
菜单入口、磁吸形状、鼠标高光和恢复原始像素。缺少 OpenGL 的虚拟显示器
会明确跳过 GPU 用例，而 CPU 回退用例仍完整执行。

实际桌面显卡的渲染验证（不创建 DesktopCanvas、不打断生产桌面）：

```sh
./build-smart-space-glass/fence-glass-test test-results/fence-glass-hardware --render-only
```

该检查覆盖 DPR 1 / 1.5 / 2、透明角落预乘色、边缘覆盖、矩形及异形折射、
共享模糊缓存、采样位置与折叠尺寸。生成的 PNG 在指定输出目录。
异形分区首次生成材质比普通矩形更贵；多分区拖动的真实帧率还需要桌面试用。

## 本机验证记录（2026-09-27）

- 优化构建 `RelWithDebInfo` 编译通过。
- `fence_glass_cpu`：通过（含真实菜单开关、普通/磁吸分区、开关像素还原、
  新建继承、重启保持、鼠标高光、系统监视器皮肤持久化/还原）。
- `smart_space_glass_smoke`、`smart_space_new_features_smoke`：通过。
- Xvfb 的 `fence_glass_gpu`：因虚拟显示器没有可用 OpenGL 而明确跳过。
- 真实桌面显示器上的 `--render-only`：实际 GPU 着色器路径通过，
  DPR 1 / 1.5 / 2 的普通和异形轮廓均通过。该模式不创建桌面窗口。
- 420×240、约 50 顶点、DPR 1.5 的合成磁吸轮廓：冷生成约 100 ms（含共享
  背景扩散），缓存轮廓后平移重绘约 20 ms。这不是整桌面帧率承诺。
- 扩展回归中 `smart_space_policy_smoke` 未打开设置窗口，
  `smart_space_ui_smoke` 未得到预期的 Project B 排除配置。
  用修改前五个原文件重建基线后，两项同样失败；policy 截图显示面板仍是
  贴边隐藏状态，脚本却直接按展开坐标点击。此次未改动这些旧脚本或智能空间源码。
  历史基线及日志未随源码发布。不能把扩展回归表述为全部通过。

