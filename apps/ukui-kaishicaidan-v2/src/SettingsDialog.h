#pragma once

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QFontComboBox>
#include <QPushButton>
#include <QSlider>
#include "StartMenuTheme.h"

class StartMenu;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(StartMenu *menu, QWidget *parent = nullptr);

private slots:
    void apply();
    void reset();

private:
    void buildUi();
    void loadCurrent();

    StartMenu *m_menu;
    QComboBox   *m_skinCombo = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QSpinBox    *m_sizeSpin  = nullptr;
    QComboBox   *m_opacityCombo = nullptr;
    QPushButton *m_applyBtn  = nullptr;
    QPushButton *m_resetBtn  = nullptr;
    QPushButton *m_closeBtn  = nullptr;
};
