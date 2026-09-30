# BackgroundTask

在 GUI 线程调用 `run(recipient, work, done)`。`work` 只捕获值，不能访问 GUI 对象；`done` 由接收者上下文交付。调用方合并重复请求。任务结果与工作线程具有独立寿命，接收者销毁后不交付回调。

在 QApplication 后声明 `ApplicationScope`，覆盖没有进入主事件循环的提前返回路径。正常 `aboutToQuit` 也会停止接收任务、请求中断，并在应用仍存活时处理事件直到所有线程结束。`run` 在退出期间返回 false；业务完成回调不再交付。任务需要自行观察 `QThread::isInterruptionRequested()` 才能提前结束，不能强制中断文件提交等不可分割操作。

普通退出、指定返回码退出和提前返回均有独立测试，另检查销毁接收者、任务清理顺序、新任务拒绝和等待期间的定时器响应。
