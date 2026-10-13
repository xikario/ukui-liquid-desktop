# 视频壁纸应用后回退图片：Python 运行时修复

日期：2026-10-13。

## 原因与修复

实机 `videoWallpaperTrialStatus` 返回 `state=error`、`active=false`，错误为 `No module named 'gi'`。桌面进程的 PATH 首位 Python 来自用户目录，不具备 PyGObject；同一环境下 `/usr/bin/python3` 可以导入 GLib/Gio。旧代码按 PATH 寻找 Python，导致视频助手启动失败，随后按设计回退到原图片。问题发生在播放启动阶段，不是“应用”按钮将类型或配置写回静态。

视频助手改用 `/usr/bin`、`/bin` 中的系统 Python，并以 `-I -u` 启动：隔离 PYTHONHOME、PYTHONPATH 和用户 site-packages，保留标准输出即时传输。没有修改用户 Python、环境变量或第三方应用，也没有通过启用软件解码绕过硬件播放要求。

设置页仍在硬件播放确认后保存视频；失败保留未保存草稿与旧视频配置，并显示具体错误。选择静态图片并应用仍可正常停止视频。

## 回归

模拟 PATH 首位的用户 Python 被调用就失败，并由播放助手检查自身确实处于隔离模式。通过实际视频启动和设置页 Apply，验证硬件就绪、保存路径、未调用错误 Python，并保留取消、失败、恢复和静态切换测试。

本轮构建 Fences 与测试程序成功，5 项 CTest 全部通过：`desktop_video_trial`、`video_wallpaper_geometry`、设置中心 1 / 1.5 / 2 倍缩放。完整构建与测试日志保存在本地 `apps/ukui-fences/build/artifacts/video-apply-20261013/`，不纳入 Git。

## 本机部署与播放核验

用户选择先暂停 OCR 索引再更新。安装前备份程序、视频助手、配置、布局和 OCR 断点，正常退出并重启 Fences。运行文件与构建文件哈希一致，小组件位置、系统监视可见状态、智能空间贴边状态和配置核验一致；OCR 断点保持，后续点击 OCR 可续扫。原安装的视频助手也同步为仓库版本。

通过桌面服务重新应用用户刚才选中的视频（H.264、3840×2160、60 fps、5.1 秒），实际助手命令使用 `/usr/bin/python3 -I -u`。状态返回 `decoder=vaapi`、`hardwareReady=true`、`persistent=true`、`currentIsSaved=true`，再次采样仍保持活动且没有 GI 错误。采样时因普通窗口遮挡处于 `paused`，这是原有节省资源策略，不是回退静态类型。实机核验没有重新安装 Python 包或修改用户的 Python 链接。
