#pragma once

#include <QWidget>
#include <QPoint>
#include <QRect>
#include <QColor>
#include <QFont>
#include <QTimer>
#include <QUrl>
#include <QList>
#include "DesktopItem.h"

class QLineEdit;

class DesktopIcon : public QWidget
{
    Q_OBJECT
public:
    explicit DesktopIcon(const DesktopItem &item, QWidget *parent = nullptr);
    ~DesktopIcon() override;

    const DesktopItem &item() const { return m_item; }
    void setItem(const DesktopItem &item);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setCut(bool cut);
    bool isCut() const { return m_cut; }
    void setVisualScale(qreal scale);

    // 字体配置
    void setFontFamily(const QString &family);
    void setFontSize(int size);
    void setFontColor(const QColor &color);
    void setFontBold(bool bold);
    void setFontItalic(bool italic);

    // 回收站图标状态刷新
    void refreshTrashIcon();
    void startInlineRename();

signals:
    void activated(const DesktopItem &item);
    void undoRequested();
    void dragStarted(DesktopIcon *self, QList<QUrl> *urls = nullptr);
    void fileRemoved(const QString &filePath);
    void fileRenamed(const QString &oldPath, const QString &newPath);
    void clicked(DesktopIcon *self, Qt::KeyboardModifiers modifiers);
    void copyRequested(DesktopIcon *self, bool move);
    void systemIconMoved(DesktopIcon *self, const QPoint &newPos);
    void filesDroppedToTrash(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *)          override;
    void mousePressEvent(QMouseEvent *)     override;
    void mouseMoveEvent(QMouseEvent *)      override;
    void mouseReleaseEvent(QMouseEvent *)   override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void enterEvent(QEvent *)  override;
    void leaveEvent(QEvent *)  override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *)   override;
    void dropEvent(QDropEvent *)           override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateToolTip();
    QRect labelRect() const;
    void finishInlineRename(bool commit);

    DesktopItem m_item;
    bool        m_selected  = false;
    bool        m_cut       = false;
    bool        m_hovered   = false;
    QPoint      m_dragStart;
    QPoint      m_dragGlobalStart;
    QPoint      m_widgetStart;
    bool        m_systemDragging = false;
    qreal       m_scale     = 1.0;
    int         m_iconSize  = 48;
    int         m_cellW     = 80;
    int         m_cellH     = 104;

    // 字体配置
    QString     m_fontFamily;
    int         m_fontSize  = 11;
    QColor      m_fontColor = Qt::white;
    bool        m_fontBold   = false;
    bool        m_fontItalic = false;

    // 点击动画
    bool        m_clickAnim = false;
    qreal       m_clickAnimProgress = 0.0;
    QTimer      m_clickAnimTimer;
    QTimer      m_renameTimer;
    QLineEdit  *m_renameEdit = nullptr;
    bool        m_finishingRename = false;

    // 刷新闪烁动画
    bool        m_refreshBlink = false;
    qreal       m_refreshBlinkProgress = 0.0;
    QTimer      m_refreshBlinkTimer;

    static constexpr int ICON_SIZE = 48;
    static constexpr int CELL_W    = 80;
    static constexpr int CELL_H    = 104;
public:
    void triggerRefreshBlink();
};
