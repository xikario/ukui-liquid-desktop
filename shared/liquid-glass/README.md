# 公共光学材质 · liquid-glass

`ukui-liquid-optics` 是 Qt 5 / C++17 静态库，为缓存背景生成液态表面。宿主包括 [任务栏主题](../../apps/ukui-panel-liquid/README.md)、[开始菜单生态液态](../../apps/ukui-kaishicaidan-v2/README.md)、Fences 四类 LiquidDesklet 卡片、设置中心和诊断详情。Fences 分区与系统监视主卡片仍保留自己的 FenceGlassRenderer，智能空间也有独立绘制路径。

[项目主页](../../README.md) · [模块架构](../../docs/ARCHITECTURE.md) · [公开 API](src/LiquidOpticsRenderer.h)

## 渲染与缓存

GPU 后端使用 Snell 折射、色散、高光和焦散；CPU 降级采用扩散、染色及简化光学效果。调用方提供无 UI 内容的背景，不读取合成器的实时后窗画面。可用 `UKUI_LIQUID_GLASS_NO_GL=1` 强制 CPU 检查。

每个 renderer 独立持有背景；同一进程的存活实例共享 GL context/program，减少多组件恢复时重复编译 shader。调用方仍需缓存最终表面，悬停和内容刷新不应逐次运行光学渲染。

## API 与线程规则

宿主 CMake 引入本目录，链接 `ukui-liquid-optics`，随宿主构建。renderer **只能在 GUI 线程使用**：

```cpp
#include "LiquidOpticsRenderer.h"

LiquidOpticsRenderer renderer;
renderer.setOptics(3.5, 0.58, 0.55, 1.1);
renderer.setMaterial(0.0, 1.0);
renderer.setWallpaper(backgroundImage);
QImage panel = renderer.renderPanel(logicalRect, 16);
```

`setMaterial` 控制背景清晰度与液态强度；`renderPanel` 支持形状，`renderControl` 支持控件/按下态。`usedGpu()` 反映最近渲染后端，不能仅凭配置开关宣称 GPU 成功。

较大背景可先用 `LiquidMaterial::prepare` 准备扩散材质，再在 GUI 线程调用 `setPreparedWallpaper`；后台任务必须值捕获，不接触 GUI 对象，参见 [BackgroundTask](../async-work/README.md)。位置与尺寸使用逻辑像素，输入图像需正确携带 DPR。

缺少 OpenGL 开发头文件时传 `UKUI_LIQUID_GL_INCLUDE_DIR`。修改静态库后需重新构建、安装并重启对应宿主；参数没有跨应用实时同步。

## 检查与来源

随宿主配置 BUILD_TESTING 可构建 `liquid-material-parity`、`liquid-preparation`、`liquid-renderer-sharing-cpu` 和可用时的 GPU 共享检查。CPU 检查使用 offscreen；无 GL 的 GPU 用例可能跳过，不能计为 GPU 通过。完整入口见 [构建文档](../../docs/BUILD.md)。

Shader 直接来源为 xikario/NextKde-kylinos 提交 `0dcd60cbd8723a6da95349a82fbc63385081c22a`，原始许可保留在 [vendor/nextkde-glass/LICENSE](vendor/nextkde-glass/LICENSE)。上游关系和复用范围见 [第三方声明](../../THIRD_PARTY_NOTICES.md)，参数说明见 [材质升级记录](../../docs/MATERIAL_CONTROLS_20260929.md)。
