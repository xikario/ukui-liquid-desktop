#pragma once
#include <QWidget>
#include <QPointer>
#include <QMap>
#include <QImage>
#include <memory>
class DesktopCanvas;
class LiquidOpticsRenderer;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

class FencesSettingsWindow final : public QWidget {
    Q_OBJECT
public:
    explicit FencesSettingsWindow(DesktopCanvas *canvas);
    ~FencesSettingsWindow() override;
    void openPage(const QString &id);
protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void changeEvent(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    QWidget *buildPage(const QString &id);
    void attachForm(QVBoxLayout *, QWidget *);
    void refreshMaterial();
    void refreshStates();
    bool hasDrafts() const;
    QPointer<DesktopCanvas> m_canvas;
    QWidget *m_content;
    QListWidget *m_navigation;
    QStackedWidget *m_stack;
    QMap<QString,QPointer<QWidget>> m_pages;
    std::unique_ptr<LiquidOpticsRenderer> m_optics;
    QImage m_material;
    QPoint m_dragOffset;
    bool m_dragging = false;
    int m_materialBuilds = 0;
};
