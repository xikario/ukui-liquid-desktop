# 中国节假日数据

scripts/china_holidays_2026.json 来自 NateScarlet/holiday-cn（MIT）。
来源：https://github.com/NateScarlet/holiday-cn/blob/master/2026.json
2026-09-28 下载并逐段核对官方公告：
https://www.gov.cn/zhengce/zhengceku/202511/content_7047091.htm
《国务院办公厅关于2026年部分节假日安排的通知》，国办发明电〔2025〕7号。

系统已有历史数据读取 /usr/share/ukui-panel/plugin-calendar/html/jiejiari.json。
未收录年份不预测休班，界面明确提示。更新后续年份需加入已公布官方数据。

农历使用系统 ICU 的 Chinese calendar，显式采用 Asia/Shanghai 正午。
不依赖网络，不启动常驻农历进程；沿用日历变更与月份切换时的异步快照。
传统节日和放假调休是不同字段；不将周末自动标成法定假日。
农历显示独立于系统日程重复规则，未扩展农历重复事项。
