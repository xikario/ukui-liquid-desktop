#include "SettingsDialog.h"
#include "StartMenu.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QSignalBlocker>

SettingsDialog::SettingsDialog(StartMenu *menu, QWidget *parent)
    : LiquidDialog::Dialog(parent, Qt::Window|Qt::WindowMinimizeButtonHint|Qt::WindowCloseButtonHint), m_menu(menu)
{
    setObjectName("launcherSettingsDialog");
    setWindowTitle(QString::fromUtf8("开始菜单设置"));
    setMinimumSize(400, 390);
    resize(460,440);
    buildUi();
    loadCurrent();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    // 皮肤组
    auto *skinBox = new QGroupBox(QString::fromUtf8("皮肤"), this);
    skinBox->setObjectName("launcherSkinGroup");
    auto *skinForm = new QFormLayout(skinBox);
    m_skinCombo = new QComboBox(this);
    m_skinCombo->setObjectName("launcherTheme");
    m_skinCombo->addItem(QString::fromUtf8("深色 (Dark)"),       static_cast<int>(Skin::Dark));
    m_skinCombo->addItem(QString::fromUtf8("浅色 (Light)"),      static_cast<int>(Skin::Light));
    m_skinCombo->addItem(QString::fromUtf8("赛博 (Cyber)"),      static_cast<int>(Skin::Cyber));
    m_skinCombo->addItem(QString::fromUtf8("玻璃 (Glass)"),      static_cast<int>(Skin::Glass));
    m_skinCombo->addItem(QString::fromUtf8("壁纸色 (Wallpaper)"),static_cast<int>(Skin::Wallpaper));
    m_skinCombo->addItem(QString::fromUtf8("极简液态 (Eco Liquid)"), static_cast<int>(Skin::EcoLiquid));
    skinForm->addRow(QString::fromUtf8("主题"), m_skinCombo);

    m_opacityCombo = new QComboBox(this);
    m_opacityCombo->setObjectName("launcherOpacity");
    m_opacityCombo->addItem(QString::fromUtf8("0 %"), 0);
    m_opacityCombo->addItem(QString::fromUtf8("30 %"), 30);
    m_opacityCombo->addItem(QString::fromUtf8("50 %"), 50);
    m_opacityCombo->addItem(QString::fromUtf8("70 %"), 70);
    m_opacityCombo->addItem(QString::fromUtf8("100 %"), 100);
    skinForm->addRow(QString::fromUtf8("透明度"), m_opacityCombo);
    root->addWidget(skinBox);

    // 字体组
    auto *fontBox = new QGroupBox(QString::fromUtf8("字体"), this);
    fontBox->setObjectName("launcherFontGroup");
    auto *fontForm = new QFormLayout(fontBox);
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setObjectName("launcherFont");
    m_fontCombo->setEditable(false);
    m_sizeSpin = new QSpinBox(this);
    m_sizeSpin->setObjectName("launcherFontSize");
    m_sizeSpin->setRange(9, 22);
    m_sizeSpin->setSingleStep(1);
    m_sizeSpin->setSuffix(QString::fromUtf8(" pt"));
    fontForm->addRow(QString::fromUtf8("字体"), m_fontCombo);
    fontForm->addRow(QString::fromUtf8("字号"), m_sizeSpin);
    root->addWidget(fontBox);

    root->addStretch();

    // 按钮区
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_resetBtn = new QPushButton(QString::fromUtf8("恢复默认"), this);
    m_resetBtn->setObjectName("launcherReset");
    m_applyBtn = new QPushButton(QString::fromUtf8("应用"), this);
    m_applyBtn->setObjectName("launcherApply");
    m_applyBtn->setDefault(true);
    m_closeBtn = new QPushButton(QString::fromUtf8("关闭"), this);
    m_closeBtn->setObjectName("launcherClose");
    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_closeBtn);
    root->addLayout(btnRow);

    connect(m_applyBtn, &QPushButton::clicked, this, &SettingsDialog::apply);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsDialog::reset);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_opacityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::apply);
}

void SettingsDialog::loadCurrent()
{
    const QSignalBlocker loading(m_opacityCombo);
    StartMenuConfig &cfg = StartMenuConfig::instance();
    const int skinIdx = m_skinCombo->findData(static_cast<int>(cfg.skin()));
    if (skinIdx >= 0) m_skinCombo->setCurrentIndex(skinIdx);

    const QString fam = cfg.fontFamily();
    if (!fam.isEmpty())
        m_fontCombo->setCurrentFont(QFont(fam));
    m_sizeSpin->setValue(cfg.fontSize());
    const int opVal = cfg.panelOpacity();
    int bestIdx = 4; // default to 100%
    int minDiff = 999;
    for (int i = 0; i < m_opacityCombo->count(); ++i) {
        int val = m_opacityCombo->itemData(i).toInt();
        int diff = qAbs(val - opVal);
        if (diff < minDiff) {
            minDiff = diff;
            bestIdx = i;
        }
    }
    m_opacityCombo->setCurrentIndex(bestIdx);
}

void SettingsDialog::apply()
{
    StartMenuConfig &cfg = StartMenuConfig::instance();
    const auto skin=static_cast<Skin>(m_skinCombo->currentData().toInt());
    const QString family=m_fontCombo->currentFont().family();
    const bool surfaceChanged=cfg.skin()!=skin || cfg.panelOpacity()!=m_opacityCombo->currentData().toInt();
    const bool fontChanged=cfg.fontFamily()!=family || cfg.fontSize()!=m_sizeSpin->value();
    if(!cfg.setAppearance(skin,family,m_sizeSpin->value(),m_opacityCombo->currentData().toInt()))return;

    // 立刻生效
    if(surfaceChanged)m_menu->applySkin(skin);
    if(fontChanged)m_menu->applyFont(family,m_sizeSpin->value());
}

void SettingsDialog::reset()
{
    const QSignalBlocker resetting(m_opacityCombo);
    m_skinCombo->setCurrentIndex(0); // Dark
    m_fontCombo->setCurrentFont(QFont("sans-serif"));
    m_sizeSpin->setValue(14);
    m_opacityCombo->setCurrentIndex(4); // 100%
    apply();
}
