#pragma once
#include "DesktopWidgets.h"
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
class QProcess;
class QFileSystemWatcher;
class QListWidget;
class CalendarDesklet final : public LiquidDesklet {
    Q_OBJECT
public:
    explicit CalendarDesklet(DesktopCanvas *canvas);
    ~CalendarDesklet() override;
    int scheduleCount() const {return m_items.size();}
    QDate selectedDate() const {return m_selected;}
    void selectDate(QDate date);
    void syncCalendarData();
    void setAgendaCollapsed(bool collapsed, bool animate = false);
protected:
    void paintContent(QPainter &) override;
    void paintDrawerContent(QPainter &) override;
    void arrangeControls() override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void extendMenu(QMenu &) override;
private:
    void reload();
    void showYearMenu();
    void showMonthMenu();
    void updateDateButtons();
    void rewatch();
    void updateList();
    void paintAgendaHeader(QPainter &);
    void openSystemCalendar(QDate date);
    QRect gridRect() const;
    QRect dateCell(int day) const;
    QString m_database,m_warning,m_calendarWarning;
    QJsonObject m_dates;
    QDate m_selected,m_month,m_today;
    QJsonArray m_items;
    QListWidget *m_list=nullptr;
    QPushButton *m_previous=nullptr,*m_next=nullptr,*m_todayButton=nullptr,*m_all=nullptr,*m_open=nullptr;
    QPushButton *m_yearButton=nullptr,*m_monthButton=nullptr,*m_collapse=nullptr;
    bool m_agendaCollapsed=false;
    int m_expandedHeight=390;
    QProcess *m_reader;
    QProcess *m_holidaySync=nullptr;
    QFileSystemWatcher *m_watcher;
    QTimer m_debounce,m_dayTimer,m_timeout;
    bool m_allDates=true,m_again=false;
};
