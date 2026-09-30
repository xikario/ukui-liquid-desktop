#include "DesktopCanvas.h"
#include "DesktopIcon.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

void DesktopCanvas::configureIconAppearance(DesktopIcon *icon, IconSurface surface)
{
    const bool liquid=m_iconAppearance.style==IconVisualStyle::LiquidPlate &&
        (surface==IconSurface::Desktop || m_iconAppearance.fencePlates);
    icon->setIconAppearance(liquid?IconVisualStyle::LiquidPlate:IconVisualStyle::Native,
        surface, m_iconAppearance.wallpaperTint ? m_iconAccent : QColor("#bcc8d6"),
        m_iconAppearance.strength/100. * (surface==IconSurface::Fence?.65:1.));
}

void DesktopCanvas::applyIconAppearanceToAll()
{
    for(auto *icon:findChildren<DesktopIcon *>())
        configureIconAppearance(icon,icon->iconSurface());
}

void DesktopCanvas::showIconAppearanceDialog() { showSettingsPage("icons"); }

QWidget *DesktopCanvas::createIconSettingsPage(QWidget *parent)
{
    auto *form=new QWidget(parent); QWidget &dialog=*form; dialog.setWindowTitle("桌面图标样式"); dialog.setObjectName("iconAppearanceDialog");
    auto *layout=new QFormLayout(&dialog);
    auto *style=new QComboBox(&dialog); style->setObjectName("iconVisualStyle");
    style->addItems({"系统原始","液态玻璃底座"});
    auto *strength=new QSlider(Qt::Horizontal,&dialog); strength->setObjectName("iconGlassStrength");
    strength->setRange(0,100);
    auto *value=new QLabel(&dialog);
    auto *tint=new QComboBox(&dialog); tint->addItems({"跟随当前壁纸","中性玻璃"});
    auto *fence=new QCheckBox("分区内部图标也使用液态底座",&dialog);
    fence->setObjectName("fenceIconPlates");
    layout->addRow("图标外观",style); layout->addRow("玻璃强度",strength); layout->addRow("",value);
    layout->addRow("颜色",tint); layout->addRow(fence);
    auto *hint=new QLabel("原始图标保持清晰；分区内底座自动减弱。应用后立即生效。",&dialog);
    hint->setWordWrap(true); layout->addRow(hint);
    auto fill=[=, &dialog](const IconAppearance &a) {
        style->setCurrentIndex(a.style==IconVisualStyle::Native?0:1);
        strength->setValue(a.strength); tint->setCurrentIndex(a.wallpaperTint?0:1);
        fence->setChecked(a.fencePlates);
    };
    auto enable=[=, &dialog] { const bool on=style->currentIndex()==1;
        strength->setEnabled(on); tint->setEnabled(on); fence->setEnabled(on); };
    connect(strength,&QSlider::valueChanged,&dialog,[=, &dialog](int n){value->setText(QString::number(n)+"%");});
    connect(style,QOverload<int>::of(&QComboBox::currentIndexChanged),&dialog,[=, &dialog](int){enable();});
    fill(m_iconAppearance); enable();
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::RestoreDefaults|QDialogButtonBox::Apply,&dialog);
    layout->addRow(buttons);
    auto apply=[=, &dialog] {
        m_iconAppearance.style=style->currentIndex()==0?IconVisualStyle::Native:IconVisualStyle::LiquidPlate;
        m_iconAppearance.strength=strength->value(); m_iconAppearance.wallpaperTint=tint->currentIndex()==0;
        m_iconAppearance.fencePlates=fence->isChecked(); m_iconAppearance.save(); applyIconAppearanceToAll(); dialog.setProperty("settingsDirty",false);
    };
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[=, &dialog]{fill(IconAppearance());});
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,apply);
    return form;
}
