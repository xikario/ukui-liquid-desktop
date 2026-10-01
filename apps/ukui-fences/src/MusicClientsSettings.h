#pragma once
#include <QWidget>
#include "MprisPlayer.h"
class QListWidget;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class MusicClientsSettings final : public QWidget {
    Q_OBJECT
public:
    explicit MusicClientsSettings(QWidget *parent=nullptr);
signals:
    void configurationApplied();
private:
    void rebuild(int selected);
    void showProfile(int row);
    void editProfile();
    void addProfile(const QString &service);
    void refreshAvailable();
    bool identifyProfile(int row);
    QList<MusicClientProfile> m_profiles;
    MprisPlayer *m_discovery;
    QListWidget *m_clients;
    QComboBox *m_available;
    QLineEdit *m_name, *m_service, *m_program;
    QPlainTextEdit *m_arguments;
    QLabel *m_status, *m_launchHint;
    bool m_loading=false;
};
