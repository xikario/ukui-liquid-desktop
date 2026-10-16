# 整条任务栏缩短居中：独立预览

仅用于缺少原生 length/alignment 接口的 OEM UKUI 面板调研。
原生接口可用的系统继续使用正式插件中的设置项。
此目录不参与三个应用的默认构建或安装。

```sh
cmake -S experiments/panel-centered-preview -B experiments/panel-centered-preview/build
cmake --build experiments/panel-centered-preview/build -j2
```

插件 key 为 `centerprobe`，通过 QProxyStyle 包装 `ukuiliquidpanel`。
隔离会话内将 build/plugins 与正式液态插件所在 plugins 目录加入
`QT_PLUGIN_PATH` 后，运行 `/usr/bin/ukui-panel -style centerprobe`。
在独立 Xvfb 和 D-Bus 会话验证，避免在已有面板运行时启动第二个面板。

当前原型固定为 68% 宽度，只支持单个水平面板，不持久化、不修改 strut，
没有完成多屏、自动隐藏、四边停靠与图标溢出测试。
退出原型并按原入口启动面板即可恢复。完整研究与验证缺口见
[调研记录](../../docs/PANEL_CENTERED_COMPAT_RESEARCH_20261015.md)。
