> 历史开发/设计记录，保留用于追溯。部分功能、路径、构建与部署状态已变化；当前版本以仓库根 README 和 docs/VALIDATION.md 为准，截图、配置和备份不随源码发布。

为了让您的界面更具时尚感与现代科技感，我们可以从以下几个核心维度对当前的 UI 进行美化与技术重构：

### 一、 核心视觉与交互优化思路

1. **动态微交互（Fluid Micro-transitions）**
   * **当前问题**：Hover、点击状态均使用布尔值（`m_hovered`）控制，背景切换过于生硬，产生廉价感。
   * **优化方案**：引入 `QVariantAnimation`，让 Hover 背景的淡入淡出、图标的轻微纵向位移（Translate）呈现出丝滑的平滑过渡效果（如 150ms 的 `OutCubic` 缓动）。

2. **深邃色彩与超细边框（Obsidian Slate & Fine Borders）**
   * **当前问题**：原有的 Cyber 皮肤色彩饱和度过高（亮青色与紫色），显得比较刺眼且不够沉稳。
   * **优化方案**：采用更深邃的黑曜石深灰色（Obsidian Space）作为底色，配合极其纤细的半透明渐变边框，利用精细的线条取代大面积的亮色。

3. **卡片式悬浮与层级深度（Glassmorphism & Card Layout）**
   * **当前问题**：列表、卡片没有层次划分，整体缺乏“纵深（Depth）”。
   * **优化方案**：对已固定应用、最近文件采用微弱的毛玻璃卡片背景包裹，并伴随边缘高光。

4. **矢量图形精细化重构**
   * **当前问题**：自绘的 Avatar、侧边栏按钮（Documents、Power等）线条比较硬，缺乏呼吸感。
   * **优化方案**：头像外侧增加动态仪表盘刻度环（虚线装饰），电源键采用圆角帽的精密圆弧绘制，极大地提升像素级的精致感。

---

### 二、 关键代码美化重构

以下是为您重写和调整后的核心代码部分，主要集中在 `StartMenuTheme.h`（调色板）和 `StartMenu.cpp`（自绘与动画）中。

#### 1. 调色板重构 (`src/StartMenuTheme.h` 修改)
我们将 `Cyber` 与 `Dark` 皮肤的配色调整为更高级的冷灰色调，并降低大面积发光色块的饱和度，改用微弱的渐变。

```cpp
// 替换 src/StartMenuTheme.h 中对应的部分
inline SkinPalette paletteForSkin(Skin skin, QColor wallpaperAccent = QColor())
{
    SkinPalette p;
    switch (skin) {
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
    // ... 其他保持不变
    return p;
}
```

#### 2. 自带平滑过渡动画的应用按钮 (`src/StartMenu.cpp` 修改)
我们重构 `AppIconButton`，为其添加 `QVariantAnimation`，当鼠标悬停时，背景色会平滑渐入，同时应用图标会轻微上浮（向上位移 2 像素），以此带来类似原生 OS 系统的灵动交互。

```cpp
class AppIconButton : public QPushButton {
public:
    AppIconButton(const SkinPalette *pal, const AppEntry &app, int index, QWidget *parent, bool isRecent = false)
        : QPushButton(parent), m_palette(pal), m_app(app), m_index(index), m_isRecent(isRecent)
    {
        setFixedSize(84, 88); // 略微放大，留足呼吸感
        setCursor(Qt::PointingHandCursor);
        setToolTip(app.name);

        m_icon = iconForDesktopIcon(app.name, app.iconName, app.desktopPath);

        // 初始化 Hover 动画
        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(160);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value){
            m_hoverFactor = value.toReal();
            update();
        });
    }

    int appIndex() const { return m_index; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 平滑渐变的 Hover 背景
        if (m_hoverFactor > 0.01) {
            p.save();
            p.setOpacity(m_hoverFactor);
            QPainterPath btnBg;
            btnBg.addRoundedRect(rect().adjusted(2, 2, -2, -2), 10, 10);
            p.fillPath(btnBg, m_palette->hoverBg);
            p.restore();
        }

        // 根据 Hover 状态产生微小的垂直位移 (0 ~ -3 像素)
        const double offset = -3.0 * m_hoverFactor;

        const int iconW = 38;
        const int iconH = 38;
        const int iconX = (width() - iconW) / 2;
        const double iconY = 12.0 + offset;

        const QRectF iconRect(iconX, iconY, iconW, iconH);
        m_icon.paint(&p, iconRect.toRect(), Qt::AlignCenter);

        p.setPen(m_palette->textSecondary);
        QFont labelFont = font();
        labelFont.setPixelSize(m_isRecent ? 10 : 11);
        p.setFont(labelFont);

        if (m_isRecent) {
            const QRect labelRect(2, iconY + iconH + 4, width() - 4, 12);
            const QString elided = p.fontMetrics().elidedText(m_app.name, Qt::ElideRight, width() - 8);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, elided);

            p.setPen(m_palette->textMuted);
            QFont timeFont = font();
            timeFont.setPixelSize(8);
            p.setFont(timeFont);
            const QRect timeRect(2, labelRect.bottom() + 2, width() - 4, 11);
            QString timeStr = m_app.lastUsed.isValid() ? RecentFiles::timeAgoString(m_app.lastUsed) : QString::fromUtf8("刚刚");
            p.drawText(timeRect, Qt::AlignHCenter | Qt::AlignTop, timeStr);
        } else {
            const QRect labelRect(0, iconY + iconH + 6, width(), 16);
            const QString elided = p.fontMetrics().elidedText(m_app.name, Qt::ElideRight, width() - 8);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, elided);
        }
    }

    void enterEvent(QEvent *) override {
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverFactor);
        m_hoverAnim->setEndValue(1.0);
        m_hoverAnim->start();
    }

    void leaveEvent(QEvent *) override {
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverFactor);
        m_hoverAnim->setEndValue(0.0);
        m_hoverAnim->start();
    }

private:
    const SkinPalette *m_palette;
    AppEntry m_app;
    int m_index = -1;
    QIcon m_icon;
    bool m_isRecent = false;

    // 动画状态
    QVariantAnimation *m_hoverAnim = nullptr;
    double m_hoverFactor = 0.0;
};
```

#### 3. 极客风 Avatar 头像与电源按钮的精密绘制 (`src/StartMenu.cpp` 修改)
为了摆脱“色块拼凑”的感觉，我们对侧边栏的头像（加入精密的虚线动态盘、微缩在线状态灯）和电源按键进行重构，采用具有现代科技感的高级矢量笔触：

```cpp
void StartMenu::drawAvatarButton(QPainter &p)
{
    const int cx = StartMenuTheme::kRailWidth / 2;
    const int cy = 50;
    const int r = 16;

    if (m_hoveredRailBtn == RailButton::Avatar) {
        QRect avatarRect(cx - 20, cy - 20, 40, 40);
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        p.drawRoundedRect(avatarRect, 10, 10);
    }

    // 1. 头像边缘精致的科技感仪表环（虚线）
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_palette.accent, 1, Qt::DashLine));
    p.drawArc(cx - r - 4, cy - r - 4, (r + 4) * 2, (r + 4) * 2, 45 * 16, 270 * 16);
    p.restore();

    // 2. 头像渐变
    QLinearGradient g(cx - r, cy - r, cx + r, cy + r);
    g.setColorAt(0, m_palette.avatarGrad1);
    g.setColorAt(1, m_palette.avatarGrad2);

    QPainterPath circle;
    circle.addEllipse(QPointF(cx, cy), r, r);
    p.fillPath(circle, g);

    // 3. 极简线条刻画半身像
    p.setPen(QPen(QColor(255, 255, 255, 220), 1.2));
    p.drawEllipse(QPointF(cx, cy - 4), 5, 5);
    p.drawEllipse(QPointF(cx, cy + 9), 9, 5);

    // 4. 头像右下角：高亮在线状态指示灯
    p.setPen(QPen(m_palette.panelBg, 1.5));
    p.setBrush(QColor(34, 197, 94)); // 现代莹光绿 (Green-500)
    p.drawEllipse(QPointF(cx + 10, cy + 10), 4.5, 4.5);
}

void StartMenu::drawRailButtons(QPainter &p)
{
    const int cx = StartMenuTheme::kRailWidth / 2;
    const int panelH = StartMenuTheme::kPanelHeight;

    const int sepY = panelH - 72;
    p.setPen(QPen(m_palette.separator, 1));
    p.drawLine(16, sepY, 48, sepY);

    // Documents 按钮绘制
    const int docY = railDocumentsY();
    QRect docRect(cx - 18, docY - 18, 36, 36);
    if (m_hoveredRailBtn == RailButton::Documents) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        p.drawRoundedRect(docRect, 8, 8);

        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, docY - 8, 3, 16), 1.5, 1.5);
    }
    p.setPen(m_palette.textSecondary);
    p.drawRect(docRect.adjusted(7, 5, -7, -11));
    p.drawLine(docRect.left() + 11, docRect.bottom() - 8, docRect.right() - 11, docRect.bottom() - 8);

    // Settings 按钮绘制
    const int setY = railSettingsY();
    QRect setRect(cx - 18, setY - 18, 36, 36);
    if (m_hoveredRailBtn == RailButton::Settings) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        p.drawRoundedRect(setRect, 8, 8);

        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, setY - 8, 3, 16), 1.5, 1.5);
    }
    p.setPen(m_palette.textSecondary);
    p.drawEllipse(setRect.adjusted(6, 6, -6, -6));
    p.drawEllipse(setRect.center().x() - 3, setRect.center().y() - 3, 6, 6);

    // Power 按钮绘制（使用圆角笔触描绘精确的开关弧线）
    const int powY = railPowerY();
    QRect powRect(cx - 18, powY - 18, 36, 36);
    if (m_hoveredRailBtn == RailButton::Power) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_palette.hoverBg);
        p.drawRoundedRect(powRect, 8, 8);

        p.setBrush(m_palette.accent);
        p.drawRoundedRect(QRectF(3, powY - 8, 3, 16), 1.5, 1.5);
    }
    
    // 开关机图标：圆角断开的环 + 竖线
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_palette.powerNormal, 1.8, Qt::SolidLine, Qt::RoundCap));
    
    QPainterPath powerArc;
    powerArc.addArc(powRect.adjusted(7, 7, -7, -7), 120 * 16, 300 * 16);
    p.drawPath(powerArc);
    p.drawLine(cx, powY - 9, cx, powY + 2);
    p.restore();
}
```

#### 4. 搜索框精细高光阴影与样式美化 (`src/StartMenu.cpp` 修改)
为了让搜索框有更立体的“浮雕”与悬浮感，我们优化其 CSS。通过极其纤细的 1 像素内发光与边框色，当搜索框获得焦点时呈现出呼吸般的蓝色辉光效应：

```cpp
void StartMenu::applySearchStyle()
{
    if (!m_searchEdit)
        return;

    // 当输入框处于焦点时，让背景颜色呈现微弱高光
    QColor focusBg = m_palette.searchBg;
    focusBg.setAlpha(std::min(255, focusBg.alpha() + 15));

    const QString style = QStringLiteral(
        "QLineEdit {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 20px;"             // 略微降低圆角使结构稍显挺拔
        "  padding: 10px 16px 10px 48px;"
        "  color: %3;"
        "  font-size: 14px;"
        "  selection-background-color: %4;"
        "  selection-color: %5;"
        "}"
        "QLineEdit:focus {"
        "  border: 1.5px solid %6;"           // 焦点态高亮发光
        "  background: %7;"
        "}")
        .arg(rgbaCss(m_palette.searchBg),
             rgbaCss(m_palette.searchBorder),
             rgbaCss(m_palette.textPrimary),
             rgbaCss(m_palette.focusBorder),
             rgbaCss(m_palette.panelBg),
             rgbaCss(m_palette.accent),       // 焦点边框采用高对比度 accent
             rgbaCss(focusBg));
    m_searchEdit->setStyleSheet(style);

    QPalette pal = m_searchEdit->palette();
    pal.setColor(QPalette::Text, m_palette.textPrimary);
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::PlaceholderText, m_palette.textMuted);
    m_searchEdit->setPalette(pal);
}
```

---

### 三、 总结
通过以上修改：
1. 主题上采用降低高纯度 neon 色彩的方案，改用 **Obsidian Slate（黑曜石灰色调）**，使界面观感更加深邃。
2. 按钮添加了 **基于 `QVariantAnimation` 的微小物理运动和淡入淡出效果**，极大缓解了操作时的生硬感。
3. 重新规范了自绘细节，头像区域加入了 **刻度环、状态指示灯**，电源键采用了 **圆角帽路径描绘**。

您可以直接将上述重构部分替换您当前的对应代码块，即可在不破坏业务逻辑的前提下快速提升 UI 的时尚感和科技感。