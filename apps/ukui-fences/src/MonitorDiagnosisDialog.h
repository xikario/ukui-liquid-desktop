#pragma once
#include <QDialog>
#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QImage>
#include <QPointer>
#include <QStringList>
#include <QMap>
#include <memory>
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QTabWidget;
class QVBoxLayout;
class QFrame;
class QPushButton;
class DesktopCanvas;
class LiquidOpticsRenderer;
namespace LiquidMaterial { class Preparation; }

struct MonitorDiagnosisReport {
    struct Command { QString command, explanation, risk, conditions; };
    QString status, summary, scope;
    QStringList resources, bottlenecks, evidence, immediate, longTerm, risks;
    QList<Command> commands;
    bool structured = false;
    static MonitorDiagnosisReport fromJson(const QJsonObject &object);
    QString toPlainText() const;
};

struct MonitorDiagnosisSnapshot {
    MonitorDiagnosisReport report;
    QString text, error, progress, scope, model, localRating, targetState, telemetry;
    QDateTime started, completed;
    int samples = 0, total = 15;
    bool busy = false, sampling = false, telemetryFrozen = false;
};

class MonitorDiagnosisDialog final : public QDialog {
    Q_OBJECT
public:
    struct Colors { QColor panel, card, border, text, muted, accent; };
    explicit MonitorDiagnosisDialog(QWidget *parent, DesktopCanvas *canvas);
    ~MonitorDiagnosisDialog() override;
    void setColors(const Colors &colors, bool glass);
    void setSnapshot(const MonitorDiagnosisSnapshot &snapshot);
    // Atomic local export used by the file picker and by regression fixtures.
    bool saveExport(const QString &path, bool telemetry, QString *error = nullptr) const;
signals:
    void settingsRequested();
protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
private:
    QWidget *buildReportPage(bool overview);
    QWidget *buildTelemetryPage();
    QFrame *addSection(QVBoxLayout *layout, const QString &id, const QString &title);
    void setSection(const QString &id, const QString &body, bool visible);
    void refreshReport();
    void refreshMaterial();
    void exportFromPicker();
    QPointer<DesktopCanvas> m_canvas;
    MonitorDiagnosisSnapshot m_snapshot;
    Colors m_colors;
    QLabel *m_scope, *m_badge, *m_meta, *m_progressText, *m_telemetryHint;
    QProgressBar *m_progress;
    QTabWidget *m_tabs;
    QPlainTextEdit *m_fallback, *m_telemetry;
    QPushButton *m_copySummary, *m_copyAll, *m_export;
    QVBoxLayout *m_commandsLayout;
    QFrame *m_commandsCard;
    QMap<QString, QFrame *> m_sections;
    QMap<QString, QLabel *> m_bodies;
    QString m_renderedText, m_renderedError;
    bool m_renderedBusy = false, m_renderedStructured = false, m_hasRenderedReport = false;
    bool m_glass = false, m_materialPending = false;
    QImage m_material;
    std::unique_ptr<LiquidOpticsRenderer> m_optics;
    LiquidMaterial::Preparation *m_preparation = nullptr;
};
