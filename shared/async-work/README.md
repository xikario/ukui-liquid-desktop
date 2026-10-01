# BackgroundTask · 后台任务与退出生命周期

[BackgroundTask.h](BackgroundTask.h) 为 Qt 宿主提供值捕获的后台任务入口，供背景准备、查询等耗时操作使用。它不是独立服务，也不会自动合并请求或给业务操作增加事务。

[项目主页](../../README.md) · [模块架构](../../docs/ARCHITECTURE.md)

## 接入

在 QApplication（或 QCoreApplication）之后、业务对象之前声明 `ApplicationScope`，保证正常退出和提前返回时应用状态仍存活：

```cpp
QApplication app(argc, argv);
BackgroundTask::ApplicationScope backgroundScope;
// 创建并使用业务对象
return app.exec();
```

在 GUI 线程提交 `run(recipient, work, done)`：

```cpp
BackgroundTask::run(label,
    [path] { return QFileInfo(path).size(); },
    [label](qint64 bytes) { label->setText(QString::number(bytes)); });
```

这个示例假定包含 `BackgroundTask.h`、QFileInfo 和 QLabel。`work` 只捕获值，不能访问 GUI 对象；`done` 交付到接收者线程上下文，接收者被销毁后不交付。捕获的其他指针不会自动受保护，需要独立寿命检查。

调用方应合并重复请求并丢弃过期结果。每次提交创建工作线程，不应在高频绘制或输入事件中无限提交。

## 退出与取消

`aboutToQuit` 或 scope 析构时停止接收新任务、请求中断，并在应用存活期间处理事件直到线程结束；退出期间 run 返回 false，业务完成回调不再投递。

接收者销毁仅停止回调，**不强制结束工作**。任务需要自行观察 `QThread::isInterruptionRequested()` 才能提前结束；文件提交等不可分割操作应自行决定安全边界。耗时任务也应有自己的超时，不能假设应用退出会强制终止阻塞系统调用。

这是头文件模块，宿主需 Qt Core、Threads 和 C++17。随宿主开启 BUILD_TESTING 后可运行 `background-task-quit`、`background-task-exit`、`background-task-scope`，覆盖正常退出、指定退出码和提前返回，以及接收者销毁、新任务拒绝和等待期间事件响应。
