# 任务栏视频壁纸同步修复 · 2026-10-06

## 问题与原因

实际桌面切换为 `【哲风壁纸】剑客背影-古风.mp4` 后，任务栏仍显示此前图片壁纸的绿色玻璃背景。任务栏原先只读取 `kyfences/layout.json` 的静态壁纸及系统背景，没有读取 Fences INI 中的视频选择，也没有监听视频首帧缓存。

## 改动

- `WallpaperBackdrop` 读取 `kylin/ukui-fences.ini` 的 `wallpaper/videoPath`，按 Fences v2 缓存规则定位 `poster.png`：规范路径、大小、修改时间组成 SHA-256 身份，缓存位于用户数据目录的 `kylin/ukui-fences/video-previews/`。
- 有首帧时用其按 Fill 模式取样，与 Fences 视频背景一致；首帧缺失或视频身份变化时沿用既有静态／系统背景路径。保留显式 `UKUI_LIQUID_WALLPAPER` 覆盖的优先级。
- 监听 Fences INI、视频源和首帧；缓存目录尚未创建时监听最近的已有父目录，收到通知后重新绑定。文件原子替换后也能恢复监听。
- 沿用 250 ms 合并通知和材质缓存，源路径、大小、修改时间及模式未变时不重建。没有增加视频播放器、抽帧任务或持续轮询。

此修复采用已保存的视频选择；未接入视频后端的实时故障状态。它不处理损坏的首帧文件，也不代表所有视频播放失败场景都已验证。

## 验证

Release 构建通过，8 项相关 CTest 全部通过：`wallpaper-source`、`panel-theme`、`optics-fallback`、`menu-style`，以及 1、1.25、1.5、2 倍缩放的 `panel-corners`。

新增来源测试使用隔离的配置／数据目录，由实际 Fences `VideoWallpaperCache` 写入测试首帧，验证首帧取样、视频切换、源文件身份变化、静态回退、环境覆盖优先级及无关设置不触发重建。面板测试通过实际文件通知验证：先选没有缓存的视频、再原子写入首帧；仅修改 INI 切换视频；移除视频选择恢复静态材质。四档缩放均通过。

本机已安装并重新加载任务栏插件。前后截图确认旧绿色底色消失，背景改为当前视频首帧。部署检查全部通过：构建与安装文件一致，运行进程映射新插件，任务栏位置尺寸和设置未变，Fences 布局、小组件、已保存视频及播放器进程未变。验证期间视频仍为原始 3840×2160、60 fps、VAAPI 播放；没有为任务栏启动额外播放器。

本次未做新的 CPU 对照采样，不将截图或回归测试当作资源节省量的测量。现有 OEM 面板日志未输出插件的 `qInfo` 性能计数，因此也不据此声称实际运行中的材质重建次数为零。

本机可复核记录（不随源码发布）：

- `apps/ukui-panel-liquid/build/artifacts/panel-video-wallpaper-20261006/`：部署检查、前后截图、运行输出。
- `/tmp/panel-video-wallpaper-tests.log`：8 项测试结果。
- 安装前备份：`apps/ukui-panel-liquid/releases/20261006-020037-before-video-wallpaper-sync/`。

更改保留在本地，未提交或推送 Git。
