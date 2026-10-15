#pragma once
#include <QWidget>
class QCheckBox;
class QSpinBox;
class QPushButton;
class QLabel;

// A view onto UKUI KWin, not another animation engine.
class WindowEffectsSettings final : public QWidget {
public:
    explicit WindowEffectsSettings(QWidget *parent=nullptr);
private:
    void reload();
    void setBusy(bool);
    void toggleEffect(const QString &,bool);
    void saveDuration(int);
    QCheckBox *m_magic=nullptr,*m_maximize=nullptr;
    QSpinBox *m_duration=nullptr;
    QPushButton *m_reset=nullptr;
    QLabel *m_status=nullptr;
    bool m_magicSupported=false,m_maximizeSupported=false;
};
