#include "MonitorDiagnosisDialog.h"
#include "DesktopCanvas.h"
#include "LiquidOpticsRenderer.h"
#include "LiquidMaterialPreparation.h"
#include "LiquidPopup.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTabBar>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QJsonArray>

namespace {
QStringList strings(const QJsonValue &value) {
    QStringList values;
    if (value.isString()) values << value.toString().trimmed();
    else for (const auto &item : value.toArray())
        if (item.isString()) values << item.toString().trimmed();
    values.removeAll(QString()); return values;
}
QString bullets(const QStringList &items) { return items.isEmpty() ? QString() : QStringLiteral("• ") + items.join("\n• "); }
QString css(QColor color) { return QString("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha()); }
QLabel *label(const QString &text, QWidget *parent, const QString &name = {}) {
    auto *value = new QLabel(text, parent); value->setObjectName(name);
    value->setTextFormat(Qt::PlainText); value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    value->setFocusPolicy(Qt::ClickFocus); return value;
}
void plain(QPlainTextEdit *editor, const QString &value) {
    if (editor->toPlainText() != value) editor->setPlainText(value);
}
}

MonitorDiagnosisReport MonitorDiagnosisReport::fromJson(const QJsonObject &object) {
    MonitorDiagnosisReport r; r.structured = true;
    r.status = object.value("overall_status").toString().trimmed();
    r.summary = object.value("summary").toString().trimmed();
    r.scope = object.value("sampling_scope").toString().trimmed();
    r.resources = strings(object.value("resource_assessment"));
    r.bottlenecks = strings(object.value("bottlenecks")); r.evidence = strings(object.value("evidence"));
    r.immediate = strings(object.value("immediate_actions")); r.longTerm = strings(object.value("long_term_actions"));
    r.risks = strings(object.value("risk_notes"));
    auto commands = object.value("command_suggestions");
    if (commands.isUndefined()) commands = object.value("commands");
    if (commands.isString()) commands = QJsonArray{commands};
    for (const auto &value : commands.toArray()) {
        Command c;
        if (value.isString()) c.command = value.toString().trimmed();
        else if (value.isObject()) {
            const auto o = value.toObject(); c.command = o.value("command").toString().trimmed();
            c.explanation = o.value("explanation").toString().trimmed();
            c.risk = o.value("risk").toString().trimmed(); c.conditions = o.value("conditions").toString().trimmed();
        }
        if (!c.command.isEmpty()) r.commands << c;
    }
    return r;
}

QString MonitorDiagnosisReport::toPlainText() const {
    QStringList blocks;
    if (!status.isEmpty()) blocks << "整体状态：" + status;
    if (!scope.isEmpty()) blocks << "采样对象与范围：" + scope;
    if (!summary.isEmpty()) blocks << summary;
    auto add = [&](const QString &title, const QStringList &items) {
        if (!items.isEmpty()) blocks << title + "\n" + bullets(items);
    };
    add("资源占用判断", resources); add("主要瓶颈", bottlenecks); add("判断依据", evidence);
    add("下一步建议（需自行确认）", immediate); add("长期建议", longTerm); add("风险提示", risks);
    for (const auto &c : commands) {
        QStringList lines{"建议命令（仅供手动核对，未执行）", c.command};
        if (!c.explanation.isEmpty()) lines << "用途：" + c.explanation;
        if (!c.conditions.isEmpty()) lines << "适用条件：" + c.conditions;
        if (!c.risk.isEmpty()) lines << "风险：" + c.risk;
        blocks << lines.join('\n');
    }
    return blocks.join("\n\n");
}

MonitorDiagnosisDialog::MonitorDiagnosisDialog(QWidget *parent, DesktopCanvas *canvas)
    : QDialog(parent, Qt::Window | Qt::WindowStaysOnTopHint), m_canvas(canvas) {
    setObjectName("monitorDiagnosisDetails"); setWindowTitle("智能诊断详情 · 只读建议");
    setAttribute(Qt::WA_DeleteOnClose); setSizeGripEnabled(true);
    setMinimumSize(520, 340);
    auto *root = new QVBoxLayout(this); root->setContentsMargins(18, 14, 18, 10); root->setSpacing(9);
    auto *heading = new QHBoxLayout;
    auto *title = label("智能诊断详情", this, "monitorDiagnosisHeading");
    QFont titleFont = title->font(); titleFont.setPixelSize(20); titleFont.setBold(true); title->setFont(titleFont);
    heading->addWidget(title, 1);
    m_badge = label({}, this, "monitorDiagnosisBadge"); m_badge->setWordWrap(false); heading->addWidget(m_badge);
    root->addLayout(heading);
    m_scope = label({}, this, "monitorDiagnosisScope"); root->addWidget(m_scope);
    m_meta = label({}, this, "monitorDiagnosisMeta"); root->addWidget(m_meta);
    m_progressText = label({}, this, "monitorDiagnosisProgressText"); root->addWidget(m_progressText);
    m_progress = new QProgressBar(this); m_progress->setObjectName("monitorDiagnosisProgress");
    m_progress->setTextVisible(true); root->addWidget(m_progress);
    m_tabs = new QTabWidget(this); m_tabs->setObjectName("monitorDiagnosisTabs");
    auto *style = QStyleFactory::create("Fusion"); style->setParent(this);
    m_tabs->setStyle(style); m_tabs->tabBar()->setStyle(style);
    m_tabs->addTab(buildReportPage(true), "诊断概览");
    m_tabs->addTab(buildReportPage(false), "详细分析");
    m_tabs->addTab(buildTelemetryPage(), "采样数据"); root->addWidget(m_tabs, 1);
    auto *notice = label("只读建议，不执行任何操作。结束进程前请确认身份、保存工作并了解影响。", this, "monitorDiagnosisNotice");
    root->addWidget(notice);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText("关闭"); buttons->button(QDialogButtonBox::Close)->setIcon(QIcon());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    m_copySummary = buttons->addButton("复制摘要", QDialogButtonBox::ActionRole);
    m_copySummary->setObjectName("monitorCopySummary");
    m_copyAll = buttons->addButton("复制全部", QDialogButtonBox::ActionRole); m_copyAll->setObjectName("monitorCopyDiagnosis");
    m_export = buttons->addButton("导出…", QDialogButtonBox::ActionRole); m_export->setObjectName("monitorExportDiagnosis");
    connect(m_copyAll, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_snapshot.text); });
    connect(m_copySummary, &QPushButton::clicked, this, [this] {
        const QString summary = m_snapshot.report.structured
            ? m_snapshot.scope + "\n整体状态：" + m_snapshot.report.status + "\n" + m_snapshot.report.summary
            : m_snapshot.text;
        QApplication::clipboard()->setText(summary);
    });
    connect(m_export, &QPushButton::clicked, this, &MonitorDiagnosisDialog::exportFromPicker);
    root->addWidget(buttons);
}
MonitorDiagnosisDialog::~MonitorDiagnosisDialog() = default;

QFrame *MonitorDiagnosisDialog::addSection(QVBoxLayout *layout, const QString &id, const QString &title) {
    auto *card = new QFrame; card->setObjectName("monitorSectionCard");
    auto *contents = new QVBoxLayout(card); contents->setContentsMargins(16, 12, 16, 14); contents->setSpacing(8);
    auto *heading = label(title, card); heading->setProperty("sectionTitle", true); contents->addWidget(heading);
    auto *body = label({}, card, id); contents->addWidget(body);
    m_sections[id] = card; m_bodies[id] = body; layout->addWidget(card); return card;
}
QWidget *MonitorDiagnosisDialog::buildReportPage(bool overview) {
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(overview ? "monitorOverviewScroll" : "monitorAnalysisScroll");
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(8, 12, 8, 12); layout->setSpacing(12);
    if (overview) {
        auto *state = addSection(layout, "monitorDiagnosisState", "诊断状态");
        auto *settings = new QPushButton("检查 API 设置", state); settings->setObjectName("monitorDiagnosisCheckSettings");
        connect(settings, &QPushButton::clicked, this, &MonitorDiagnosisDialog::settingsRequested);
        state->layout()->addWidget(settings); settings->setVisible(false);
        addSection(layout, "monitorDiagnosisSummary", "结论摘要"); addSection(layout, "monitorDiagnosisResources", "关键观察");
        addSection(layout, "monitorDiagnosisRisks", "风险提示");
    } else {
        addSection(layout, "monitorDiagnosisScopeDetail", "采样对象与范围");
        addSection(layout, "monitorDiagnosisBottlenecks", "主要瓶颈"); addSection(layout, "monitorDiagnosisEvidence", "判断依据");
        addSection(layout, "monitorDiagnosisImmediate", "下一步建议"); addSection(layout, "monitorDiagnosisLongTerm", "长期建议");
        m_commandsCard = new QFrame(page); m_commandsCard->setObjectName("monitorSectionCard");
        m_commandsLayout = new QVBoxLayout(m_commandsCard); m_commandsLayout->setContentsMargins(16, 12, 16, 14);
        layout->addWidget(m_commandsCard);
        m_fallback = new QPlainTextEdit(page); m_fallback->setObjectName("monitorDiagnosisText");
        m_fallback->setReadOnly(true); m_fallback->setMinimumHeight(280); layout->addWidget(m_fallback);
    }
    layout->addStretch(); scroll->setWidget(page); return scroll;
}
QWidget *MonitorDiagnosisDialog::buildTelemetryPage() {
    auto *page = new QWidget; auto *layout = new QVBoxLayout(page); layout->setContentsMargins(8, 12, 8, 12);
    m_telemetryHint = label({}, page, "monitorDiagnosisTelemetryHint"); layout->addWidget(m_telemetryHint);
    auto *row = new QHBoxLayout; row->addStretch(); auto *copy = new QPushButton("复制 JSON", page);
    copy->setObjectName("monitorCopyTelemetry"); row->addWidget(copy); layout->addLayout(row);
    m_telemetry = new QPlainTextEdit(page); m_telemetry->setObjectName("monitorDiagnosisTelemetry"); m_telemetry->setReadOnly(true);
    m_telemetry->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); layout->addWidget(m_telemetry, 1);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_snapshot.telemetry); });
    return page;
}
void MonitorDiagnosisDialog::setColors(const Colors &colors, bool glass) {
    if (m_colors.panel == colors.panel && m_colors.card == colors.card && m_colors.border == colors.border
        && m_colors.text == colors.text && m_colors.muted == colors.muted && m_colors.accent == colors.accent
        && m_glass == glass) return;
    m_colors = colors; m_glass = glass;
    auto palette = this->palette(); QColor background = colors.panel; background.setAlpha(255);
    for (auto role : {QPalette::Window, QPalette::Base, QPalette::Button}) palette.setColor(role, background);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) palette.setColor(role, colors.text);
    setPalette(palette);
    QColor card = colors.card; card.setAlpha(glass ? 185 : 235);
    setStyleSheet(QString(
        "QLabel { color:%1; background:transparent; font-size:13px; }"
        "QLabel#monitorDiagnosisHeading { font-size:20px; font-weight:600; }"
        "QLabel#monitorDiagnosisMeta,QLabel#monitorDiagnosisNotice { color:%5; }"
        "QLabel[sectionTitle=true] { font-size:15px; font-weight:600; }"
        "QFrame#monitorSectionCard { background:%2; border:1px solid %3; border-radius:12px; }"
        "QTabWidget::pane { border:none; } QScrollArea, QScrollArea>QWidget>QWidget { background:transparent; }"
        "QTabBar::tab { color:%1; background:%2; padding:8px 14px; margin-right:4px; border-radius:8px; }"
        "QTabBar::tab:selected { border-bottom:2px solid %4; }"
        "QPlainTextEdit { color:%1; background:%2; border:1px solid %3; border-radius:9px; padding:10px; font-size:13px; }"
        "QPushButton { color:%1; background:%2; border:1px solid %3; border-radius:7px; padding:7px 10px; }"
        "QPushButton:hover { border-color:%4; } QPushButton:disabled { color:%5; }"
        "QProgressBar { color:%1; background:%2; border:1px solid %3; border-radius:6px; text-align:center; }"
        "QProgressBar::chunk { background:%4; border-radius:5px; }"
        "QScrollBar:vertical { background:transparent; width:10px; } QScrollBar::handle:vertical { background:%3; min-height:24px; border-radius:4px; }"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical {background:transparent;}")
        .arg(css(colors.text), css(card), css(colors.border), css(colors.accent), css(colors.muted)));
    update();
    if (isVisible()) refreshMaterial();
}
void MonitorDiagnosisDialog::setSection(const QString &id, const QString &body, bool visible) {
    auto *value = m_bodies.value(id); if (value->text() != body) value->setText(body);
    m_sections.value(id)->setVisible(visible);
}
void MonitorDiagnosisDialog::setSnapshot(const MonitorDiagnosisSnapshot &snapshot) {
    const bool reportChanged = !m_hasRenderedReport || snapshot.text != m_renderedText || snapshot.error != m_renderedError
        || snapshot.busy != m_renderedBusy || snapshot.report.structured != m_renderedStructured;
    m_snapshot = snapshot;
    m_scope->setText(snapshot.scope + " · 模型：" + snapshot.model);
    m_meta->setText((snapshot.started.isValid() ? "开始：" + snapshot.started.toString("MM-dd HH:mm:ss") : "尚未开始诊断")
        + (snapshot.completed.isValid() ? " · 完成：" + snapshot.completed.toString("HH:mm:ss") : QString())
        + (snapshot.targetState.isEmpty() ? QString() : "\n" + snapshot.targetState));
    QString state = !snapshot.error.isEmpty() ? "诊断失败" : snapshot.busy
        ? (snapshot.sampling ? "采样中" : "分析中") : snapshot.text.isEmpty() ? "待诊断" : "已完成";
    QColor accent = !snapshot.error.isEmpty() ? QColor("#b65b40") : m_colors.accent;
    if (!snapshot.busy && snapshot.error.isEmpty() && !snapshot.text.isEmpty()) {
        if (snapshot.report.status == "健康") { state += " · 健康"; accent = QColor("#23966e"); }
        else if (snapshot.report.status == "关注") { state += " · 关注"; accent = QColor("#b47826"); }
        else if (snapshot.report.status == "异常") { state += " · 异常"; accent = QColor("#b65b40"); }
    }
    m_badge->setText(state);
    m_badge->setStyleSheet("QLabel {padding:4px 10px;border-radius:8px;border:1px solid " + css(accent) + ";}");
    m_progress->setVisible(snapshot.busy); m_progressText->setVisible(snapshot.busy);
    m_progressText->setText(snapshot.progress);
    m_progress->setRange(0, snapshot.sampling ? snapshot.total : 0);
    m_progress->setValue(snapshot.samples); m_progress->setFormat("采样 %v / %m");
    const QString info = !snapshot.error.isEmpty() ? snapshot.error : snapshot.busy ? snapshot.progress
        : snapshot.text.isEmpty() ? "暂无诊断结果。请返回组件启动一次诊断。"
        : "模型判断：" + (snapshot.report.status.isEmpty() ? QString("未分级") : snapshot.report.status)
          + "\n本地规则评级：" + snapshot.localRating;
    setSection("monitorDiagnosisState", info, true);
    findChild<QPushButton *>("monitorDiagnosisCheckSettings")->setVisible(!snapshot.error.isEmpty());
    m_telemetryHint->setText((snapshot.telemetryFrozen ? "本次请求使用的固定采样快照。" : "本地采样预览，尚未生成固定请求快照。")
        + QStringLiteral("包含性能指标及进程元数据，不含 API 密钥、文件正文或截图；进程及可执行文件名可能透露应用用途。"));
    plain(m_telemetry, snapshot.telemetry.isEmpty() ? QString("暂无有效诊断采样数据。") : snapshot.telemetry);
    const bool hasResult = !snapshot.busy && snapshot.error.isEmpty() && !snapshot.text.isEmpty();
    m_copyAll->setEnabled(hasResult); m_copySummary->setEnabled(hasResult);
    m_export->setEnabled(hasResult || !snapshot.telemetry.isEmpty());
    if (reportChanged) refreshReport();
    m_renderedText = snapshot.text; m_renderedError = snapshot.error;
    m_renderedBusy = snapshot.busy; m_renderedStructured = snapshot.report.structured;
    m_hasRenderedReport = true;
}
void MonitorDiagnosisDialog::refreshReport() {
    const auto &r = m_snapshot.report;
    const bool complete = !m_snapshot.busy && m_snapshot.error.isEmpty() && !m_snapshot.text.isEmpty();
    const bool structured = complete && r.structured;
    setSection("monitorDiagnosisSummary", r.summary, structured && !r.summary.isEmpty());
    setSection("monitorDiagnosisResources", bullets(r.resources), structured && !r.resources.isEmpty());
    setSection("monitorDiagnosisRisks", bullets(r.risks), structured && !r.risks.isEmpty());
    setSection("monitorDiagnosisScopeDetail", r.scope, structured && !r.scope.isEmpty());
    setSection("monitorDiagnosisBottlenecks", bullets(r.bottlenecks), structured && !r.bottlenecks.isEmpty());
    setSection("monitorDiagnosisEvidence", bullets(r.evidence), structured && !r.evidence.isEmpty());
    setSection("monitorDiagnosisImmediate", bullets(r.immediate), structured && !r.immediate.isEmpty());
    setSection("monitorDiagnosisLongTerm", bullets(r.longTerm), structured && !r.longTerm.isEmpty());
    m_fallback->setVisible(complete && !structured); plain(m_fallback, m_snapshot.text);
    if (complete && !structured) setSection("monitorDiagnosisSummary", "服务以文本返回，完整正文保留在“详细分析”。", true);
    // Rebuild only when the report changes; progress updates never disturb selection.
    while (auto *item = m_commandsLayout->takeAt(0)) { delete item->widget(); delete item; }
    m_commandsCard->setVisible(structured && !r.commands.isEmpty());
    if (!structured || r.commands.isEmpty()) return;
    m_commandsLayout->addWidget(label("建议命令 · 仅供手动核对，未执行", m_commandsCard));
    for (int i=0; i<r.commands.size() && i<20; ++i) {
        const auto &command = r.commands[i];
        auto *copy = new QPushButton("复制命令", m_commandsCard); copy->setObjectName("monitorCopyCommand");
        connect(copy, &QPushButton::clicked, this, [command] { QApplication::clipboard()->setText(command.command); });
        auto *code = new QPlainTextEdit(m_commandsCard); code->setObjectName("monitorDiagnosisCommand"); code->setReadOnly(true);
        code->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); code->setPlainText(command.command);
        code->setMinimumHeight(70); code->setMaximumHeight(150);
        m_commandsLayout->addWidget(code); m_commandsLayout->addWidget(copy, 0, Qt::AlignRight);
        m_commandsLayout->addWidget(label("用途：" + (command.explanation.isEmpty() ? "服务未说明" : command.explanation)
            + "\n适用条件：" + (command.conditions.isEmpty() ? "需自行核对当前环境" : command.conditions)
            + "\n风险：" + (command.risk.isEmpty() ? "服务未说明，执行前须自行评估" : command.risk), m_commandsCard));
    }
    if (r.commands.size()>20) m_commandsLayout->addWidget(label("仅展开前 20 条，其余保留在复制全部与文本导出中。", m_commandsCard));
}
bool MonitorDiagnosisDialog::saveExport(const QString &path, bool telemetry, QString *error) const {
    const QString text = telemetry ? m_snapshot.telemetry : m_snapshot.text;
    if (text.isEmpty() || (!telemetry && (m_snapshot.busy || !m_snapshot.error.isEmpty()))) {
        if (error) *error = "当前没有可导出的完整内容。"; return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray data = text.toUtf8();
    if (file.write(data) != data.size() || !file.commit()) { if (error) *error = file.errorString(); return false; }
    return true;
}
void MonitorDiagnosisDialog::exportFromPicker() {
    const QString selected = m_tabs->currentIndex() == 2 ? "采样 JSON (*.json)" : "诊断文本 (*.txt)";
    QPointer<MonitorDiagnosisDialog> guard(this);
    // Keep the platform file picker independent of the report's glass palette.
    QFileDialog picker(nullptr, "导出诊断", "diagnosis-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"),
        "诊断文本 (*.txt);;采样 JSON (*.json)");
    picker.setAcceptMode(QFileDialog::AcceptSave); picker.setFileMode(QFileDialog::AnyFile);
    picker.selectNameFilter(selected); picker.setDefaultSuffix(selected.contains("JSON") ? "json" : "txt");
    connect(&picker, &QFileDialog::filterSelected, &picker, [&picker](const QString &filter) {
        picker.setDefaultSuffix(filter.contains("JSON") ? "json" : "txt");
    });
    if (picker.exec() != QDialog::Accepted || !guard || picker.selectedFiles().isEmpty()) return;
    QString error;
    if (!saveExport(picker.selectedFiles().first(), picker.selectedNameFilter().contains("JSON"), &error))
        QMessageBox::warning(this, "导出失败", error);
}
void MonitorDiagnosisDialog::refreshMaterial() {
    if (!m_glass || !m_canvas || !m_material.isNull() || m_materialPending) return;
    const auto source = m_canvas->wallpaperBackdrop(QRect(mapToGlobal(QPoint()), size()), devicePixelRatioF());
    if (source.isNull()) return;
    m_materialPending = true; m_optics = std::make_unique<LiquidOpticsRenderer>();
    m_preparation = new LiquidMaterial::Preparation(this);
    const QSize requested = size();
    m_preparation->request(source, [this, requested](const LiquidMaterial::Prepared &material) {
        m_materialPending = false;
        const auto &theme = LiquidPopup::theme(); m_optics->setOptics(theme.refraction, theme.tint, theme.highlight, 1);
        m_optics->setPreparedWallpaper(material); m_material = m_optics->renderPanel(QRect(QPoint(), requested), 14);
        setProperty("opticalGpu", m_optics->usedGpu()); setProperty("materialBuilds", 1);
        m_optics.reset(); update();
    });
}
void MonitorDiagnosisDialog::showEvent(QShowEvent *event) { QDialog::showEvent(event); refreshMaterial(); }
void MonitorDiagnosisDialog::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (m_glass && !m_material.isNull()) {
        p.drawImage(rect(), m_material);
        // Metadata and the advisory footer need stable contrast even over a
        // white wallpaper or a low global tint. Keep the report body optical.
        p.setRenderHint(QPainter::Antialiasing);
        QColor readingBase = m_colors.panel; readingBase.setAlpha(195);
        p.setPen(QPen(m_colors.border, 1)); p.setBrush(readingBase);
        const int headerEnd = m_tabs->geometry().top() - 6;
        const int footerTop = m_tabs->geometry().bottom() + 6;
        p.drawRoundedRect(QRect(8, 8, width()-16, qMax(0, headerEnd-8)), 12, 12);
        p.drawRoundedRect(QRect(8, footerTop, width()-16, qMax(0, height()-footerTop-8)), 12, 12);
    }
    else { QColor panel = m_colors.panel; panel.setAlpha(255); p.fillRect(rect(), panel); }
}
