# 资源监控与桌面图标重叠修复

交付日期：2026-10-20。基于主线 `d18e32337ed44426428f0dd9715fd459cc198f60` 的 X86 增量，交付时尚未提交或推送。下文 10 月 20 日的部署信息属于 X86 交付方记录；2026-10-21 的 ARM 复核另列，不将两台机器的验证混为一项。

## 问题与修复

桌面图标布局的预留区域只枚举四种 LiquidDesklet，遗漏了继承 QWidget 的 SystemMonitor。资源监控移动、缩放或显隐时也没有通知图标布局。图标刷新时的 raise 进一步导致文件被监控遮住，或文件图标绘制到监控上方；底层文件并未因此移动到其他目录。

DesktopCanvas 改为统一枚举桌面小组件，包括资源监控，用于预留区域、布局后的层级恢复与几何事件判定。资源监控安装相同的事件过滤器，Move/Resize/Show/Hide 复用已有 35ms 单次合并计时器；关闭时立即隐藏并解除过滤，再延迟销毁并调度布局。刷新图标后恢复小组件层级。没有新增常驻轮询。

## 隔离回归

新增 desktop_monitor_icon_avoidance_1、_1.5、_2，覆盖打开监控时已有文件占位、移动、缩放、真实桌面文件 MoveAction 拖放、刷新、重建桌面恢复与关闭后重开。使用私有 HOME、Xvfb 和 D-Bus，不改真实桌面文件。

同一测试在未修复源码上运行：打开、移动、拖放、缩放、刷新、重启恢复和重开七个避让断言失败。修复后以下 9 项全部通过：

- desktop_monitor_icon_avoidance_1、_1.5、_2。
- desktop_refresh_1、_1.5、_2。
- desktop_monitor_placement、desktop_selection_drag、desktop_desklets_1。

Release 的 ukui-fences 与 fence-glass-test 构建通过，git diff --check 通过。日志保存在本机 build-review-20261020/monitor-*，未纳入源码。

## X86 交付方部署与观察

在麒麟 V10 SP1 amd64 / Qt 5.12.12 的真实 X11 桌面安装并重启 Fences；未重启整机，未安装本轮构建的面板或开始菜单。

- 备份：`/home/kylinos/.local/share/ukui-liquid-backups/fences-monitor-overlap-20261020-211747`。
- 安装：`~/.local/bin/ukui-fences`，SHA-256 `18f2d91cfc4d5dca57f48fa7a519fefd93d95a44addf9ddce4e86800ec50aa77`，与修复后构建一致。
- 单实例运行，监控、时钟、活动小组件的显隐状态与可见时钟/活动小组件位置保留。
- 本机监控位于右上角；重启及调用实际 refreshAll 后，13 个桌面散落文件的布局矩形与监控预留区域无交集；屏幕截图确认原先被遮挡的文件已出现在监控左侧空闲区域。
- 本机几何核对与截图：build-review-20261020/monitor-fix-desktop-geometry.json、monitor-fix-desktop.png。

多倍缩放、拖放、缩放小组件和桌面重建使用隔离回归验证。本轮没有新增多屏热插拔或全部网格已占满时的溢出机制验收。

## 2026-10-21 ARM 合入前复核

原始粘贴附件 SHA-256：`3a4d90989d81e9aa107c47c29f5ecdc5d8d1aa63e13c21ae2d6378f5ec7a46e0`。规范化换行后，补丁可直接应用于 `d18e323`。修复只涉及 Fences 的小组件枚举、预留空间及几何/显隐事件，不涉及 x86 专用分支，也不改变文件所在目录。

在隔离评审工作区使用 ARM / Qt 5.12 Release 构建 `ukui-fences` 和 `fence-glass-test`。先只加入回归测试、保留主线实现，实际复现打开、移动、拖放、缩放、刷新、桌面重建和重开七个失败；再应用修复，交付方列出的 9 项回归全部通过。

补充真实控件的直接隐藏后拖放、同一控件重新显示、桌面隐藏时刷新再显示及文件内容校验。最终 12 项通过，零失败、零跳过：三档监视器避让、三档刷新、三档桌面小组件、两档框选拖动、监视器定位。文件仍在原路径且内容保持 `keep accessible`。未运行与本次几何变更无关的 GPU 或全仓回归。

未发现阻止该补丁合入的问题。复用原有 35 ms 单次布局合并计时器；没有新增常驻定时器，也没有把桌面小组件改为置顶普通窗口。桌面网格完全占满时仍沿用现有布局回退策略，本次不宣称解决溢出或新增多屏热插拔验收。

构建、修复前失败和最终 12 项结果保存在忽略目录 `apps/ukui-fences/build/artifacts/monitor-icon-avoidance-20261021/`。本次只在评审分支验证，没有替换真实桌面程序或重启 Fences。
