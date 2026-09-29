#pragma once

#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <memory>
#include <type_traits>

// Work must capture values, never GUI objects. Callers coalesce requests before
// submitting. The thread owns its result until GUI delivery; destroying the
// recipient drops delivery without destroying a running QThread or waiting on
// slow filesystem I/O during widget/application teardown.
namespace BackgroundTask {
template<class Work, class Done>
void run(QObject *recipient, Work work, Done done)
{
    using Result = typename std::decay<decltype(work())>::type;
    auto result = std::make_shared<Result>();
    auto *thread = QThread::create([work, result]() mutable { *result = work(); });
    QObject::connect(thread, &QThread::finished, recipient,
                     [result, done] { done(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
}
