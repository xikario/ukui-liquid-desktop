# 公共光学材质

`ukui-liquid-optics` 静态库供 `apps/ukui-panel-liquid` 与 Fences 的时钟、活动、音乐、日历底板共用。开始菜单与 Fences 既有分区渲染器暂时保留各自实现。

Shader 为 xikario/NextKde-kylinos commit `0dcd60cbd8723a6da95349a82fbc63385081c22a` 的原样副本；GPL v3 许可保留于 `vendor/nextkde-glass/LICENSE`。

共享层负责背景扩散、Snell 折射、色散、高光和焦散；调用方提供无 UI 内容的背景并缓存结果。无法读取实时合成器背景，CPU 降级只有模糊/染色。可用 `UKUI_LIQUID_GLASS_NO_GL=1` 强制降级检查。

随引用应用构建。缺少系统 OpenGL 头文件时设置 `UKUI_LIQUID_GL_INCLUDE_DIR`；修改本库后需重新构建、安装并重启对应应用。
