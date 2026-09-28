# NextKde 桌面组件 Qt5 适配

来源：https://github.com/SuceV587/NextKde
参考提交：4aeb2be2e20979aa01be5899df0a8fcbf786a5cc（2026-09-27）
上游许可证：GNU GPL v3，完整文本见 LICENSE。

参考文件：shell/desktop/modules/deskcenter/DeskCenterWindow.qml、ActivityUsageService.qml。
本地重新实现 Qt5 Widgets/X11 版本：DeskletModels、DesktopWidgets、ActivityRecorder。
保留时钟/倒计时切换、倒计时环、60 天热力图、今日应用时长排行的交互设计。
不安装 Quickshell/Plasma6，不复制上游运行环境；材质复用 shared/liquid-glass 和 shared/liquid-popup。

日历新增参考同一提交的 DeskCenterWindow.qml（calendarContent）及 apps/calendar/qml/CalendarMonthView.qml。
保留大日期、周一起始月历、今日强调和事项展示设计；本地 CalendarDesklet 为 Qt5 液态版，数据对接麒麟系统日历 SQLite，只读，不安装上游 Kos.Pim 服务。
