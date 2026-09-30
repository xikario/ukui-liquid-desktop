#pragma once
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QAbstractTextDocumentLayout>
#include <QProcess>
#include <QElapsedTimer>
#include <cstring>

// Read-only API fixtures: no real account, key, network, or telemetry submission.
class MonitorModelReply : public QNetworkReply {
public:
    MonitorModelReply(const QNetworkRequest &request, QByteArray body, int status, QObject *parent)
        : QNetworkReply(parent), m_body(body) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(10, this, [this] {
            if (isFinished()) return;
            emit readyRead();
            if (isFinished()) return;
            setFinished(true); emit finished();
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(QNetworkReply::OperationCanceledError, "cancelled");
        setFinished(true); emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_position + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 size) override {
        const qint64 count = qMin(size, qint64(m_body.size() - m_position));
        if (!count) return -1;
        std::memcpy(data, m_body.constData() + m_position, count);
        m_position += count; return count;
    }
private:
    QByteArray m_body;
    qint64 m_position = 0;
};

class MonitorModelNetwork : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;
    QByteArray body = R"({"data":[{"id":"deepseek-flash"},{"id":"deepseek-v4-pro"},{"id":"deepseek-flash"}]})";
    int status = 200, requests = 0;
    QNetworkRequest lastRequest;
protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *) override {
        check(op == GetOperation, "model discovery uses read-only GET");
        ++requests; lastRequest = request;
        return new MonitorModelReply(request, body, status, this);
    }
};

struct SystemMonitorTestAccess {
    static int run() {
        qunsetenv("DEEPSEEK_API_KEY"); qunsetenv("DEEPSEEK_API_URL");
        QSettings settings;
        QWidget host; host.resize(1600, 950); host.show();
        SystemMonitor monitor(&host);
        monitor.setSkin(SystemMonitor::Skin::Dark);
        monitor.m_timer.stop(); monitor.move(20, 20); monitor.show(); settle(80);
        monitor.m_aiText = QStringLiteral("完整诊断：资源占用与下一步建议\n")
            + QStringLiteral("只提供文字建议；请先保存工作并确认进程身份。\n").repeated(240)
            + QStringLiteral("<a href=\"file:///tmp/test\">这只是文本</a>\n最后一行：结束前再三确认");
        const QString fullText = monitor.m_aiText;
        monitor.m_diagnosisSamplesTaken = 15;
        monitor.m_diagnosisCpuSamples = {22, 24, 28};
        auto doubleClick = [&](QPoint logical) {
            const QPoint p(qRound(logical.x() * monitor.m_scale), qRound(logical.y() * monitor.m_scale));
            QMouseEvent event(QEvent::MouseButtonDblClick, p, monitor.mapToGlobal(p),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&monitor, &event); settle(30);
        };
        for (double scale : {0.65, 1.0, 1.6}) {
            monitor.m_scale = scale; monitor.resize(monitor.baseSize() * scale); monitor.updateHitRects();
            check(monitor.tooltipAt(monitor.m_aiContentRect.center()).contains("双击展开更多"), "content has expansion hint");
            doubleClick(monitor.m_aiContentRect.center());
            auto *dialog = monitor.m_aiDetailsDialog.data();
            check(dialog && dialog->isVisible(), "double click opens separate details at each widget scale");
            if (!dialog) return 1;
            for (auto *label : dialog->findChildren<QLabel *>())
                check(label->palette().color(QPalette::WindowText).lightness()
                      - dialog->palette().color(QPalette::Window).lightness() > 70,
                      "dark-theme scope and advisory labels remain readable");
            auto *text = dialog->findChild<QTextEdit *>("monitorDiagnosisText");
            check(text && text->isReadOnly() && text->toPlainText() == fullText, "long answer is untruncated read-only plain text");
            check(!monitor.m_compact, "opening details does not collapse monitor");
            doubleClick(monitor.m_aiContentRect.center());
            check(monitor.m_aiDetailsDialog == dialog, "repeat expansion reuses window");
            auto *sample = dialog->findChild<QTextEdit *>("monitorDiagnosisTelemetry");
            check(sample && sample->isReadOnly() && sample->toPlainText().contains("diagnosis_scope"), "sampling data is inspectable read-only text");
            // Long QTextDocuments lay out lazily; finish initial reflow before scrolling.
            text->document()->documentLayout()->documentSize(); settle(80);
            text->verticalScrollBar()->setValue(text->verticalScrollBar()->maximum());
            const int bottom = text->verticalScrollBar()->value(); settle(550);
            check(text->verticalScrollBar()->value() == bottom, "refresh preserves scroll for unchanged answer");
            dialog->findChild<QPushButton *>("monitorCopyDiagnosis")->click();
            check(QApplication::clipboard()->text() == fullText, "copy retains complete diagnosis");
            QDir().mkpath("artifacts");
            if (scale == 1.0) dialog->grab().save(QString("artifacts/monitor-details-%1.png").arg(dialog->devicePixelRatioF()));
            dialog->close(); settle(30);
            check(monitor.m_aiDetailsDialog.isNull(), "close releases window and timer");
        }
        monitor.setEditMode(true); doubleClick(monitor.m_aiContentRect.center());
        check(!monitor.m_aiDetailsDialog, "layout editing does not open details");
        monitor.setEditMode(false); monitor.m_scale = 1;
        monitor.resize(monitor.baseSize()); monitor.updateHitRects();
        doubleClick(monitor.m_aiContentRect.center());
        auto *text = monitor.m_aiDetailsDialog->findChild<QTextEdit *>("monitorDiagnosisText");
        monitor.m_aiBusy = true; monitor.m_aiProgressText = "测试采样 3/15"; settle(550);
        check(text->toPlainText() == monitor.m_aiProgressText, "details follows sampling progress");
        monitor.m_aiBusy = false; monitor.m_aiText = "完成：下一步建议"; settle(550);
        check(text->toPlainText() == monitor.m_aiText, "details follows completed response");
        monitor.m_aiDetailsDialog->close(); settle(30);

        auto *form = monitor.createSettingsPage(&host);
        auto *model = form->findChild<QComboBox *>("monitorApiModel");
        auto *custom = form->findChild<QLineEdit *>("monitorCustomApiModel");
        auto *key = form->findChild<QLineEdit *>("monitorApiKey");
        auto *endpoint = form->findChild<QLineEdit *>("monitorApiEndpoint");
        auto *button = form->findChild<QPushButton *>("monitorRefreshModels");
        auto *status = form->findChild<QLabel *>("monitorModelStatus");
        auto *help = form->findChild<QLabel *>("monitorApiHelp");
        check(help && help->openExternalLinks() && help->text().contains("https://api-docs.deepseek.com/zh-cn/"), "API help links to official docs");
        check(model && !model->isEditable() && !model->lineEdit(), "automatic model identifiers cannot be edited");
        if (!model || !custom || !key || !endpoint || !button || !status) return 1;
        check(model->property("usesGlobalLiquidMenu").toBool() && custom->isHidden() && !custom->isEnabled(),
              "selector uses global liquid menu and hides manual input for automatic model");
        form->resize(700, 920); form->show(); settle(80);
        bool sawMenu = false;
        QTimer::singleShot(80, form, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu *>(widget);
                if (!menu || menu->objectName() != "monitorModelMenu") continue;
                sawMenu = true;
                check(menu->actions().last()->text().contains("自定义"), "shared menu includes explicit Custom action");
                menu->grab().save(QString("artifacts/monitor-model-menu-%1.png").arg(menu->devicePixelRatioF()));
                menu->setActiveAction(menu->actions().last());
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(menu, &enter);
            }
        });
        QTimer menuTimeout;
        menuTimeout.setSingleShot(true);
        QObject::connect(&menuTimeout, &QTimer::timeout, form, [] {
            if (auto *popup = QApplication::activePopupWidget()) popup->close();
        });
        menuTimeout.start(1500); model->showPopup(); menuTimeout.stop();
        check(sawMenu && model->currentData().toString().isEmpty() && custom->isEnabled() && !custom->isHidden(),
              "global menu keyboard selection reveals custom input");
        custom->setText("custom-model-1");
        auto *apply = form->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
        apply->click();
        check(settings.value("systemMonitor/apiModel") == "custom-model-1"
              && settings.value("systemMonitor/apiModelCustom").toBool(), "custom model and source persist separately");
        custom->clear(); apply->click();
        check(settings.value("systemMonitor/apiModel") == "custom-model-1", "empty custom draft cannot overwrite active model");
        custom->setText("custom-model-1");
        model->setCurrentText("deepseek-v4-pro");
        form->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        check(settings.value("systemMonitor/apiModel") == "deepseek-v4-pro", "Apply persists selected model");
        check(!custom->isEnabled() && custom->isHidden()
              && !settings.value("systemMonitor/apiModelCustom").toBool(), "automatic selection locks out custom editing");
        model->setEditText("accidental-deletion");
        check(model->currentData() == "deepseek-v4-pro", "editable-combo calls cannot mutate automatic model ID");
        const QJsonObject payload = QJsonDocument::fromJson(monitor.buildDiagnosisPayload()).object();
        check(payload.value("model") == "deepseek-v4-pro", "payload uses selected model");
        check(payload.value("thinking").toObject().value("type") == "disabled"
              && payload.value("reasoning_effort") == "none", "diagnosis budget goes to the answer instead of hidden reasoning");
        // Share the real generated prompt for a small optional API smoke test.
        // Replace all telemetry with explicitly synthetic data before writing.
        QJsonObject apiFixture = payload;
        apiFixture.insert("model", "deepseek-flash");
        QJsonArray fixtureMessages = apiFixture.value("messages").toArray();
        fixtureMessages.replace(1, QJsonObject{{"role","user"},{"content",QStringLiteral(
            "这是虚构的接口回归样本，不是真实设备数据。diagnosis_scope=whole_system，"
            "采样15秒/15个有效样本，逻辑核心8，整机CPU均值22%/峰值31%，"
            "内存占用45%，Swap占用0%，iowait均值1%，主目录磁盘使用55%，"
            "平均读取2MB/s、写入1MB/s。没有可识别的目标PID，不要给出结束进程命令。")}});
        apiFixture.insert("messages", fixtureMessages);
        QFile fixtureFile("artifacts/monitor-api-request-fixture.json");
        if (fixtureFile.open(QIODevice::WriteOnly)) fixtureFile.write(QJsonDocument(apiFixture).toJson());
        const QString prompt = payload.value("messages").toArray().first().toObject().value("content").toString();
        check(prompt.contains("整机健康诊断") && prompt.contains("sampling_scope") && prompt.contains("resource_assessment")
              && prompt.contains("三项确认") && prompt.contains("不得执行"), "system prompt covers scope, resources and manual confirmation");
        check(!payload.contains("tools") && !payload.contains("functions"), "payload exposes no execution tools");
        monitor.m_aiRequestTelemetry = monitor.diagnosticTelemetry();
        monitor.m_aiRequestModel = monitor.m_apiModel;
        monitor.m_apiModel = "next-request-model";
        monitor.m_diagnosisCpuSamples = {99};
        const auto snapshot = QJsonDocument::fromJson(monitor.buildDiagnosisPayload()).object();
        check(snapshot.value("model") == "deepseek-v4-pro"
              && snapshot.value("messages").toArray().last().toObject().value("content") == monitor.m_aiRequestTelemetry,
              "retained request keeps the actual model and sampled data despite later changes");
        monitor.m_aiRequestTelemetry.clear(); monitor.m_aiRequestModel.clear();
        monitor.m_hasDiagnosisTarget = true;
        const QString processPrompt = QJsonDocument::fromJson(monitor.buildDiagnosisPayload()).object()
            .value("messages").toArray().first().toObject().value("content").toString();
        check(processPrompt.contains("单进程深度诊断") && processPrompt.contains("PID身份变化") && processPrompt.contains("单核100%"),
              "process prompt addresses identity, exit and CPU units");
        const QString formatted = monitor.formatDiagnosisJson(QJsonObject{
            {"overall_status","关注"}, {"summary",fullText}, {"sampling_scope","目标进程15秒"},
            {"resource_assessment",QJsonArray{"CPU均值24%"}}, {"immediate_actions",QJsonArray{"先等待并保存工作"}}});
        check(formatted.contains(fullText) && formatted.contains("资源占用判断") && formatted.contains("下一步建议")
              && formatted.contains("不执行任何操作"), "formatter preserves full answer and advisory warning");

        auto *network = new MonitorModelNetwork(form);
        auto fetch = [&] { monitor.fetchAvailableModels(model, custom, endpoint, key, button, status, network); };
        key->clear(); fetch();
        check(network->requests == 0 && status->text().contains("API Key"), "empty key does not issue request");
        key->setText("test-key"); endpoint->setText("http://api.deepseek.com/chat/completions"); fetch();
        check(network->requests == 0, "HTTP cannot carry discovery credentials");
        endpoint->setText("https://api.deepseek.com/v1/chat/completions");
        model->setCurrentIndex(model->count()-1); custom->setText("custom-preserved");
        int selectionChanges = 0;
        QObject::connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged), form, [&](int) { ++selectionChanges; });
        fetch(); check(!button->isEnabled(), "refresh prevents duplicate requests"); settle(50);
        check(button->isEnabled() && network->requests == 1 && model->count() == 3
              && custom->text() == "custom-preserved" && custom->isEnabled() && selectionChanges == 0,
              "discovery deduplicates and preserves current draft without dirty signals");
        check(!model->isEditable() && model->itemData(0) == "deepseek-flash"
              && model->itemData(1) == "deepseek-v4-pro", "fetched model IDs remain locked automatic choices");
        apply->click();
        auto *restoredForm = monitor.createSettingsPage(&host);
        auto *restoredModel = restoredForm->findChild<QComboBox *>("monitorApiModel");
        auto *restoredCustom = restoredForm->findChild<QLineEdit *>("monitorCustomApiModel");
        check(restoredModel->count() == 3 && restoredModel->currentData().toString().isEmpty()
              && restoredCustom->text() == "custom-preserved", "reopening preserves cached catalog and custom selection");
        delete restoredForm;
        check(network->lastRequest.url() == QUrl("https://api.deepseek.com/v1/models")
              && network->lastRequest.rawHeader("Authorization") == "Bearer test-key"
              && network->lastRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy,
              "request derives models endpoint and disables redirects");
        check(monitor.modelsEndpoint("https://api.deepseek.com/chat/completions") == QUrl("https://api.deepseek.com/models")
              && monitor.modelsEndpoint("https://user:pass@example.com/chat/completions").isEmpty()
              && monitor.modelsEndpoint("https://example.com/chat/completions?x=1").isEmpty(), "models URL rejects embedded credentials and query strings");
        network->status = 401; fetch(); settle(50);
        check(custom->text() == "custom-preserved" && status->text().contains("认证失败"), "401 preserves model and explains failure");
        network->status = 200; network->body = "not JSON"; fetch(); settle(50);
        check(model->count() == 3 && status->text().contains("未返回有效"), "malformed response preserves choices");
        network->body = R"({"data":[{"id":"new-model"}]})"; fetch();
        endpoint->setText("https://other.example.com/chat/completions"); settle(50);
        check(model->count() == 3 && status->text().contains("配置已改变"), "stale response cannot overwrite changed endpoint");
        network->body = QByteArray(1024 * 1024 + 2, 'x'); fetch(); settle(50);
        check(button->isEnabled() && status->text().contains("过大"), "oversized response is bounded and recoverable");
        network->body = R"({"data":[{"id":"new-model"}]})"; fetch();
        QPointer<QNetworkAccessManager> guard(network); delete form; settle(50);
        check(guard.isNull(), "closing settings safely tears down outstanding request");
        auto response = [](const QString &content, const QString &finish = "stop", const QString &reasoning = QString()) {
            return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
                {"finish_reason",finish}, {"message",QJsonObject{{"content",content},{"reasoning_content",reasoning}}}}}}}).toJson();
        };
        monitor.m_apiModel = "deepseek-flash";
        monitor.m_aiRetryCount = 0;
        check(monitor.consumeDiagnosisResponse(response("", "length", "only reasoning")) && monitor.m_aiRetryCount == 1,
              "reasoning-only exhausted response triggers one recovery");
        const auto retryPayload = QJsonDocument::fromJson(monitor.buildDiagnosisPayload()).object();
        check(!retryPayload.contains("response_format") && retryPayload.value("max_tokens").toInt() == 8192
              && retryPayload.value("thinking").toObject().value("type") == "disabled",
              "recovery requests direct text with a bounded larger answer budget");
        check(!monitor.consumeDiagnosisResponse(response("")) && !monitor.m_aiError.isEmpty(), "repeated empty response stops after one recovery");
        monitor.m_aiRetryCount = 0;
        check(monitor.consumeDiagnosisResponse(response("kill -TERM ", "length")) && monitor.m_aiText.isEmpty(),
              "truncated command advice is never presented as complete");
        check(!monitor.consumeDiagnosisResponse(response("kill -TERM ", "length")) && monitor.m_aiError.contains("截断"),
              "second truncated response reports cause rather than looping");
        monitor.m_aiRetryCount = 0;
        check(monitor.consumeDiagnosisResponse(response("{}")), "empty structured answer is not misreported as successful diagnosis");
        monitor.m_aiRetryCount = 0;
        check(monitor.consumeDiagnosisResponse(response("{\"summary\":\"unfinished")) && monitor.m_aiText.isEmpty(),
              "malformed JSON triggers recovery even when service claims normal stop");
        check(!monitor.consumeDiagnosisResponse(response("采样对象：测试进程。资源占用：正常。下一步：保存工作后观察。"))
              && monitor.m_aiText.contains("测试进程") && monitor.m_aiError.isEmpty(), "direct text recovery displays the diagnosis");
        monitor.m_aiRetryCount = 0;
        check(!monitor.consumeDiagnosisResponse(response("", "content_filter")) && monitor.m_aiRetryCount == 0,
              "filtered results are not retried to bypass the service");
        check(!monitor.consumeDiagnosisResponse(response("", "tool_calls")) && monitor.m_aiError.contains("不执行"),
              "unexpected tool requests never execute");
        check(!monitor.consumeDiagnosisResponse("not JSON") && monitor.m_aiError.contains("解析"), "invalid envelope gives a parse error");
        const QString answer = R"({"overall_status":"健康","summary":"采样范围与资源占用正常","evidence":["CPU均值22%"]})";
        check(!monitor.consumeDiagnosisResponse(response(answer)) && monitor.m_aiText.contains("CPU均值22%"),
              "normal structured responses still render correctly");
        const QString fence(3, QChar(96));
        check(!monitor.consumeDiagnosisResponse(response(fence+"json\n"+answer+"\n"+fence))
              && monitor.m_aiText.contains("整体状态") && !monitor.m_aiText.contains(fence),
              "fenced structured answer is decoded instead of exposing JSON markup");
        return runTransport(monitor);
    }

    static int runTransport(SystemMonitor &monitor) {
        QTemporaryDir fixture;
        const QString calls = fixture.path()+"/calls.jsonl";
        QFile fake(fixture.path()+"/curl");
        check(fake.open(QIODevice::WriteOnly), "create isolated API transport fixture");
        fake.write(R"PY(#!/usr/bin/python3
import json,os,sys
path=os.environ['MONITOR_FIXTURE_CALLS']
payload=json.load(sys.stdin)
with open(path,'a+') as f:
    f.write(json.dumps(payload)+'\n')
    f.seek(0)
    count=len(f.readlines())
empty=count==1 or os.environ.get('MONITOR_FIXTURE_EMPTY')=='1'
message={'content': None if empty else '采样对象：测试进程。资源占用正常，建议保存工作后观察。',
         'reasoning_content':'fixture reasoning' if empty else ''}
print(json.dumps({'choices':[{'message':message,'finish_reason':'length' if empty else 'stop'}]}))
)PY");
        fake.close(); fake.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
        const auto originalPath=qgetenv("PATH");
        qputenv("PATH",fixture.path().toUtf8()+":"+originalPath);
        qputenv("MONITOR_FIXTURE_CALLS",calls.toUtf8());
        for (bool alwaysEmpty : {false,true}) {
            QFile::remove(calls);
            qputenv("MONITOR_FIXTURE_EMPTY",alwaysEmpty?"1":"0");
            monitor.m_aiRetryCount=0; monitor.m_aiRequestTelemetry.clear(); monitor.m_aiRequestModel.clear();
            monitor.m_apiKey="fixture-only-key"; monitor.m_apiUrl="https://api.deepseek.com/chat/completions";
            monitor.m_aiBusy=true; monitor.sendDiagnosisRequest();
            // Edits made while waiting must not retarget the recovery request.
            monitor.m_apiModel="other-model"; monitor.m_apiUrl="https://other.example.com/chat/completions";
            QElapsedTimer time; time.start();
            while(monitor.m_aiBusy && time.elapsed()<5000)settle(30);
            QFile capture(calls); capture.open(QIODevice::ReadOnly);
            const auto requests=capture.readAll().trimmed().split('\n');
            check(!monitor.m_aiBusy && requests.size()==2 && monitor.m_aiRetryCount==1,
                  "actual QProcess completion performs exactly one bounded retry");
            if(requests.size()==2){
                const auto first=QJsonDocument::fromJson(requests.first()).object();
                const auto second=QJsonDocument::fromJson(requests.last()).object();
                check(first.value("model")==second.value("model")
                      && first.value("messages").toArray().last()==second.value("messages").toArray().last(),
                      "transport retry reuses original model and telemetry");
            }
            check(alwaysEmpty ? !monitor.m_aiError.isEmpty() : monitor.m_aiText.contains("测试进程"),
                  "transport reports final empty failure or displays successful retry");
            check(monitor.m_aiRequestKey.isEmpty() && !monitor.m_aiAuthFile, "completed request releases credential snapshot and header file");
        }
        qputenv("PATH",originalPath);qunsetenv("MONITOR_FIXTURE_CALLS");qunsetenv("MONITOR_FIXTURE_EMPTY");
        return failures ? 1 : 0;
    }
};
