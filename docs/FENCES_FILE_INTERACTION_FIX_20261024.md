# Fences 文件快捷键与 Peony 拖放修复（2026-10-24）

## Shift+Delete

桌面和分区此前先匹配 `QKeySequence::Cut`，而 Shift+Delete 也是 Qt 的标准剪切组合键。因此它没有执行用户预期的永久删除。

现在在标准快捷键之前识别 Shift+Delete，并弹出默认“取消”的永久删除确认。普通 Delete 仍移到回收站，Ctrl+X 仍剪切。多选和单个图标右键共用后台永久删除操作；取消在项目之间生效，失败项目明确报告。删除目录链接和悬空链接时仅移除链接本身。永久删除不生成撤回记录。

## 拖出文件到 Peony

本机版本：`peony / libpeony3 3.26.0.0-0k5.32oemccd3000m2.4.u`。

在独立 Xvfb 和 D-Bus 会话中，旧版真实 DesktopIcon → 本机 Peony 的拖放可复现失败。事件追踪表明 Peony 收到了标准文件 URL，但在拖动阶段拒绝了操作。本机 IconView 的实现会将普通拖动交给 QListView；FileItemModel 沿用默认 Qt 模型 MIME 类型检查，与它自身接受 URL 的 drop 处理不一致。

修复保留 `text/uri-list` 和 Fences 内部标记，额外提供合法的 `application/x-qabstractitemmodeldatalist` 文件名/路径显示记录。Peony 通过模型格式校验后，仍根据标准 URL 操作磁盘文件。没有伪装成系统下载器，也不修改或替换系统 Peony。临时注入相同兼容数据的旧版对照测试已成功移动文件，证实失败环节。

## 回归入口

- `desktop_permanent_delete`：桌面/分区多选、取消、默认按钮、剪贴板不变、目录、有效/悬空链接、部分失败、无回收站条目。
- `desktop_external_drag_1`、`desktop_external_drag_2`：真实独立 Peony、原生鼠标输入、1/2 倍缩放、桌面/分区、移动/Ctrl 复制、中文与空格/%/# 文件名、完整子目录内容。
- 既有回收站撤回、文件夹拖放撤回、剪贴板、分区排序和框选测试共同核验。

所有破坏性测试仅使用自动创建的临时目录。Xvfb 测试不等同于每一种 OEM 文件管理器或真实合成器的覆盖。

## 本轮结果

2026-10-24 本机 Release 主程序构建通过，上述 13 项 CTest 全部通过。测试入口的大型单文件以 `-O0` 编译以缩短迭代，其余生产源对象和部署主程序仍为 Release。

扩展测试时纠正了两个测试时序问题：外部拖放等待整批文件落盘，并在投递鼠标松开之后短暂保留 Ctrl；旧排序测试等待上一轮取消后的布局动画结束，再采样下一轮目标坐标。没有放宽文件内容与移动/复制语义断言。

已备份并原子替换 `~/.local/bin/ukui-fences`，通过正常退出入口重启，进程 PID 3054。部署 SHA-256：`157b0282d07de0fe67110223cc39f8021fcc29c65d4676d2f43792665ea74c6f`。备份：`apps/ukui-fences/releases/20261024-101545-file-interaction/`。D-Bus 核验时钟、日历、音乐、活动组件的位置、尺寸、可见状态与更新前一致。

验证记录：构建工作树 `ukui-liquid-desktop-ai-space-extraction/apps/ukui-fences/build/artifacts/file-interaction-20261024/regression-final.log`。本轮真实文件管理器交互发生在隔离 Xvfb 内，没有声称在用户当前真实桌面上拖动过文件，也没有覆盖 X86 OEM Peony 实机。
