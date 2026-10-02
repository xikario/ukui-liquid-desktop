#pragma once

#include <QColor>
#include <QFont>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

// ── 皮肤系统 ────────────────────────────────────────────
// 参考 ukui-fences SystemMonitor 的 Skin 设计
// 6 种皮肤：Dark / Light / Cyber / Glass / Wallpaper / EcoLiquid

enum class Skin {
    Dark      = 0,
    Light     = 1,
    Cyber     = 2,
    Glass     = 3,
    Wallpaper = 4,
    EcoLiquid = 5
};

struct SkinPalette {
    QColor panelBg;     // 主面板背景
    QColor railBg;      // 左侧轨背景
    QColor panelBorder; // 面板边框
    QColor railBorder;  // 轨边框
    QColor separator;   // 分隔线
    QColor textPrimary;     // 主文本
    QColor textSecondary;    // 次要文本
    QColor textMuted;        // 静默文本
    QColor hoverBg;          // hover 背景
    QColor focusBorder;      // 焦点边框
    QColor searchBg;          // 搜索框背景
    QColor searchBorder;      // 搜索框边框
    QColor powerNormal;       // 电源按钮常态
    QColor powerHover;         // 电源按钮悬停
    QColor avatarGrad1;        // 头像渐变起
    QColor avatarGrad2;        // 头像渐变止
    QColor iconPlaceholder;    // 图标占位文字色
    QColor accent;             // 强调色（用于交互元素）
    bool isLight = false;       // 浅色皮肤？影响文字反相
};

namespace StartMenuTheme {

constexpr int kPanelWidth   = 680;
constexpr int kPanelHeight  = 720;
constexpr int kCornerRadius = 16;
constexpr int kRailWidth    = 64;

// ── 字体 ──────────────────────────────────────────────
inline QFont panelFont(int size = 14) {
    QFont f("sans-serif", size);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

inline QString sharedMenuStyleSheet()
{
    return QStringLiteral(
        "QMenu {"
        "  background-color: rgb(30,30,30);"
        "  color: rgba(241,245,249,245);"
        "  border: 1px solid rgba(255,255,255,31);"
        "  border-radius: 12px;"
        "  padding: 6px;"
        "}"
        "QMenu::item {"
        "  background-color: transparent;"
        "  padding: 6px 28px 6px 14px;"
        "  border-radius: 6px;"
        "  min-height: 18px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: rgba(59,130,246,217);"
        "  color: white;"
        "}"
        "QMenu::item:disabled {"
        "  color: rgba(241,245,249,170);"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background-color: rgba(255,255,255,20);"
        "  margin: 5px 7px;"
        "}"
        "QMenu::indicator {"
        "  width: 14px;"
        "  height: 14px;"
        "}"
    );
}

inline void applySharedMenuStyle(QMenu *menu)
{
    if (!menu) {
        return;
    }

    menu->setStyleSheet(sharedMenuStyleSheet());
}

inline QString tooltipStyleSheet()
{
    return QStringLiteral(
        "QToolTip {"
        "  background-color: rgb(15,23,42);"
        "  color: rgb(255,255,255);"
        "  border: 1px solid rgba(148,163,184,110);"
        "  border-radius: 6px;"
        "  padding: 5px 9px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
    );
}

// ── 应用图标强调色（按类别分配，所有皮肤通用） ────────────
inline QColor blue()    { return QColor(96, 165, 250); }
inline QColor green()   { return QColor(74, 222, 128); }
inline QColor yellow()  { return QColor(250, 204, 21); }
inline QColor purple()  { return QColor(192, 132, 252); }
inline QColor orange()  { return QColor(251, 146, 60); }
inline QColor red()     { return QColor(248, 113, 113); }
inline QColor pink()    { return QColor(244, 114, 182); }
inline QColor emerald() { return QColor(52, 211, 153); }

// ── 皮肤调色板（一对一参考 ukui-fences SystemMonitor::paletteForSkin） ──

inline SkinPalette paletteForSkin(Skin skin, QColor wallpaperAccent = QColor())
{
    SkinPalette p;
    switch (skin) {
    case Skin::Light:
        p.panelBg     = QColor(255, 255, 255, 245);
        p.railBg      = QColor(248, 250, 252, 230);
        p.panelBorder = QColor(226, 232, 240, 150);
        p.railBorder  = QColor(226, 232, 240, 100);
        p.separator   = QColor(226, 232, 240, 120);
        p.textPrimary    = QColor(15, 23, 42);
        p.textSecondary  = QColor(100, 116, 139);
        p.textMuted      = QColor(100, 116, 139, 180);
        p.hoverBg        = QColor(241, 245, 249, 200);
        p.focusBorder    = QColor(79, 70, 229, 160);
        p.searchBg       = QColor(241, 245, 249, 150);
        p.searchBorder   = QColor(226, 232, 240, 150);
        p.powerNormal    = QColor(220, 38, 38, 220);
        p.powerHover     = QColor(220, 38, 38, 50);
        p.avatarGrad1    = QColor(79, 70, 229);
        p.avatarGrad2     = QColor(168, 85, 247);
        p.iconPlaceholder = QColor(100, 116, 139);
        p.accent         = QColor(79, 70, 229);
        p.isLight = true;
        break;

    case Skin::Cyber:
        // 使用更深、更有质感的黑曜石蓝
        p.panelBg     = QColor(10, 11, 18, 245);
        p.railBg      = QColor(15, 17, 28, 235);
        p.panelBorder = QColor(0, 240, 255, 60); // 变细、降低不透明度
        p.railBorder  = QColor(0, 240, 255, 30);
        p.separator   = QColor(255, 255, 255, 15);
        p.textPrimary    = QColor(241, 245, 249);
        p.textSecondary  = QColor(148, 163, 184);
        p.textMuted      = QColor(148, 163, 184, 150);
        p.hoverBg        = QColor(0, 240, 255, 18); // 极其微妙的 Hover 发光
        p.focusBorder    = QColor(0, 240, 255, 180);
        p.searchBg       = QColor(0, 240, 255, 10);
        p.searchBorder   = QColor(0, 240, 255, 40);
        p.powerNormal    = QColor(255, 75, 100, 220); // 更有质感的荧光红
        p.powerHover     = QColor(255, 75, 100, 40);
        p.avatarGrad1    = QColor(0, 220, 255);
        p.avatarGrad2     = QColor(140, 60, 255);
        p.iconPlaceholder = QColor(148, 163, 184);
        p.accent         = QColor(0, 210, 255);
        break;

    case Skin::Glass:
        p.panelBg     = QColor(22, 29, 43, 175);
        p.railBg      = QColor(30, 41, 59, 150);
        p.panelBorder = QColor(255, 255, 255, 62);
        p.railBorder  = QColor(255, 255, 255, 40);
        p.separator   = QColor(255, 255, 255, 50);
        p.textPrimary    = QColor(241, 245, 249);
        p.textSecondary  = QColor(186, 200, 220);
        p.textMuted      = QColor(186, 200, 220, 180);
        p.hoverBg        = QColor(255, 255, 255, 28);
        p.focusBorder    = QColor(129, 140, 248, 160);
        p.searchBg       = QColor(255, 255, 255, 18);
        p.searchBorder   = QColor(255, 255, 255, 40);
        p.powerNormal    = QColor(248, 113, 113, 220);
        p.powerHover     = QColor(248, 113, 113, 50);
        p.avatarGrad1    = QColor(129, 140, 248);
        p.avatarGrad2     = QColor(168, 85, 247);
        p.iconPlaceholder = QColor(186, 200, 220);
        p.accent         = QColor(129, 140, 248);
        break;

    case Skin::EcoLiquid:
        // V2 snapshot glass: diffuse body plus upstream Snell/rim shader.
        p.panelBg     = QColor(15, 23, 42, 122);
        p.railBg      = QColor(8, 15, 25, 86);
        p.panelBorder = QColor(255, 255, 255, 72);
        p.railBorder  = QColor(255, 255, 255, 42);
        p.separator   = QColor(255, 255, 255, 22);
        p.textPrimary    = QColor(248, 250, 252, 248);
        p.textSecondary  = QColor(226, 232, 240);
        p.textMuted      = QColor(184, 197, 214);
        p.hoverBg        = QColor(255, 255, 255, 18);
        p.focusBorder    = QColor(56, 189, 248, 190);
        p.searchBg       = QColor(8, 15, 28, 210);
        p.searchBorder   = QColor(255, 255, 255, 24);
        p.powerNormal    = QColor(251, 113, 133, 230);
        p.powerHover     = QColor(251, 113, 133, 42);
        p.avatarGrad1    = QColor(6, 182, 212);
        p.avatarGrad2    = QColor(59, 130, 246);
        p.iconPlaceholder = QColor(203, 213, 225);
        p.accent         = QColor(56, 189, 248);
        break;

    case Skin::Wallpaper: {
        QColor accent = wallpaperAccent.isValid() ? wallpaperAccent : QColor(79, 70, 229);
        const int luminance =
            (accent.red() * 299 + accent.green() * 587 + accent.blue() * 114) / 1000;
        QColor panel = accent.darker(luminance > 145 ? 210 : 155);
        QColor card  = accent.darker(luminance > 145 ? 165 : 125);
        p.panelBg     = QColor(panel.red(), panel.green(), panel.blue(), 232);
        p.railBg      = QColor(card.red(), card.green(), card.blue(), 205);
        p.panelBorder = QColor(accent.red(), accent.green(), accent.blue(), 105);
        p.railBorder  = QColor(accent.red(), accent.green(), accent.blue(), 70);
        p.separator   = QColor(255, 255, 255, 50);
        p.textPrimary    = QColor(248, 250, 252);
        p.textSecondary  = QColor(203, 213, 225);
        p.textMuted      = QColor(203, 213, 225, 180);
        p.hoverBg        = QColor(accent.red(), accent.green(), accent.blue(), 38);
        {
            QColor fb = accent.lighter(145);
            fb.setAlpha(160);
            p.focusBorder = fb;
        }
        p.searchBg       = QColor(255, 255, 255, 18);
        p.searchBorder   = QColor(accent.red(), accent.green(), accent.blue(), 70);
        p.powerNormal    = QColor(248, 113, 113, 220);
        p.powerHover     = QColor(248, 113, 113, 50);
        p.avatarGrad1    = accent.lighter(145);
        p.avatarGrad2     = accent.lighter(120);
        p.iconPlaceholder = QColor(203, 213, 225);
        p.accent         = accent;
        break;
    }

    case Skin::Dark:
    default:
        // 现代 Slate (石板灰) 高级暗色
        p.panelBg     = QColor(15, 23, 42, 245);
        p.railBg      = QColor(30, 41, 59, 210);
        p.panelBorder = QColor(255, 255, 255, 20); // 极细半透明白色边框
        p.railBorder  = QColor(255, 255, 255, 12);
        p.separator   = QColor(255, 255, 255, 15);
        p.textPrimary    = QColor(248, 250, 252);
        p.textSecondary  = QColor(148, 163, 184);
        p.textMuted      = QColor(100, 116, 139);
        p.hoverBg        = QColor(255, 255, 255, 18);
        p.focusBorder    = QColor(14, 165, 233, 160); // 蔚蓝色焦点
        p.searchBg       = QColor(30, 41, 59, 140);
        p.searchBorder   = QColor(255, 255, 255, 15);
        p.powerNormal    = QColor(239, 68, 68);
        p.powerHover     = QColor(239, 68, 68, 40);
        p.avatarGrad1    = QColor(14, 165, 233);
        p.avatarGrad2     = QColor(124, 58, 237);
        p.iconPlaceholder = QColor(148, 163, 184);
        p.accent         = QColor(14, 165, 233);
        break;
    }
    return p;
}

} // namespace StartMenuTheme


// ── 配置存储 ────────────────────────────────────────────
class StartMenuConfig {
public:
    static StartMenuConfig &instance() {
        static StartMenuConfig inst;
        return inst;
    }

    StartMenuConfig() {
#if defined(UKUI_KAISHICAIDAN_V2)
        const QString configDir = QStringLiteral("ukui-kaishicaidan-v2");
#else
        const QString configDir = QStringLiteral("ukui-kaishicaidan");
#endif
        m_settings = new QSettings(
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
            + "/" + configDir + "/settings.conf",
            QSettings::IniFormat);
    }

    Skin skin() const {
#if defined(UKUI_KAISHICAIDAN_V2)
        constexpr int defaultSkin = static_cast<int>(Skin::EcoLiquid);
#else
        constexpr int defaultSkin = static_cast<int>(Skin::Dark);
#endif
        return static_cast<Skin>(m_settings->value("skin",
            defaultSkin).toInt());
    }
    void setSkin(Skin skin) {
        m_settings->setValue("skin", static_cast<int>(skin));
        m_settings->sync();
    }

    QString fontFamily() const {
        return m_settings->value("fontFamily", QString()).toString();
    }
    void setFontFamily(const QString &family) {
        m_settings->setValue("fontFamily", family);
        m_settings->sync();
    }

    int fontSize() const {
        return m_settings->value("fontSize", 14).toInt();
    }
    void setFontSize(int size) {
        m_settings->setValue("fontSize", size);
        m_settings->sync();
    }

    int panelOpacity() const {
#if defined(UKUI_KAISHICAIDAN_V2)
        constexpr int defaultOpacity = 50;
#else
        constexpr int defaultOpacity = 92;
#endif
        return m_settings->value("panelOpacity", defaultOpacity).toInt();
    }
    void setPanelOpacity(int opacity) {
        m_settings->setValue("panelOpacity", qBound(0, opacity, 100));
        m_settings->sync();
    }
    bool setAppearance(Skin value,const QString &family,int size,int opacity) {
        opacity=qBound(0,opacity,100);
        if(skin()==value && fontFamily()==family && fontSize()==size && panelOpacity()==opacity)return false;
        m_settings->setValue("skin",static_cast<int>(value));
        m_settings->setValue("fontFamily",family);
        m_settings->setValue("fontSize",size);
        m_settings->setValue("panelOpacity",opacity);
        m_settings->sync();
        return true;
    }

    QStringList railPinnedApps() const {
        return m_settings->value("railPinnedApps").toStringList();
    }
    void setRailPinnedApps(const QStringList &paths) {
        m_settings->setValue("railPinnedApps", paths);
        m_settings->sync();
    }

private:
    QSettings *m_settings = nullptr;
};
