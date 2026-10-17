#pragma once
#include "ai.h"
#include "config.h"
#include "oauth.h"
#include "portalfilepicker.h"
#include "provider.h"
#include "store.h"
#include <QLockFile>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(bool aiBusy READ aiBusy NOTIFY statusChanged)
    Q_PROPERTY(bool demo READ demo CONSTANT)
  public:
    Backend(const QString &directory, const QString &configDirectory, bool demo = false,
            QObject *parent = nullptr, FilePicker *picker = nullptr);
    QVariantMap data() const { return m_data; }
    QVariantMap settings() const { return m_config.values().toVariantMap(); }
    QString status() const { return tr(m_status.toUtf8().constData()); }
    QString error() const { return m_error; }
    QString summary() const { return m_summary; }
    bool busy() const { return m_busy; }
    bool aiBusy() const { return m_aiBusy; }
    bool demo() const { return m_demo; }
    bool startService();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void selectProfile(const QString &profile);
    Q_INVOKABLE void selectFolder(const QString &folder);
    Q_INVOKABLE void selectThread(const QString &account, const QString &thread);
    Q_INVOKABLE void search(const QString &term);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void fetchOlder(const QString &account, const QString &query);
    Q_INVOKABLE bool setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveProfile(const QString &id, const QString &name, const QString &color);
    Q_INVOKABLE void assignAccount(const QString &account, const QString &profile);
    Q_INVOKABLE void connectAccount(const QString &provider, const QString &profile);
    Q_INVOKABLE void importGoogleClient();
    Q_INVOKABLE void openLink(const QString &url);
    Q_INVOKABLE void openGuide();
    Q_INVOKABLE void threadAction(const QString &action);
    Q_INVOKABLE void correctPriority(const QString &message, bool priority);
    Q_INVOKABLE void setTopic(const QString &message, const QString &topic);
    Q_INVOKABLE void dismissPriority();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void newDraft(const QString &mode = "new");
    Q_INVOKABLE void editDraft(const QString &id);
    Q_INVOKABLE bool saveDraft(const QVariantMap &draft);
    Q_INVOKABLE bool approveDraft(const QVariantMap &draft, const QString &when = "");
    Q_INVOKABLE void cancelSchedule(const QString &id);
    Q_INVOKABLE void deleteDraft(const QString &id);
    Q_INVOKABLE void addAttachments(const QVariantMap &draft);
    Q_INVOKABLE void loadAttachments(const QString &message);
    Q_INVOKABLE void saveAttachment(const QString &message, const QVariantMap &attachment);
    Q_INVOKABLE void analyzeAttachment(const QString &message, const QVariantMap &attachment);
    Q_INVOKABLE void summarize(bool draft = false);
    Q_INVOKABLE void ask(const QString &question);
    Q_INVOKABLE void checkModels();
    Q_INVOKABLE void classifySelected();
    Q_INVOKABLE void calendarNavigate(int amount);
    Q_INVOKABLE void calendarToday();
    Q_INVOKABLE void calendarDate(const QString &date);
    Q_INVOKABLE void newEvent(const QString &date = "");
    Q_INVOKABLE void editEvent(const QString &id);
    Q_INVOKABLE void saveEvent(const QVariantMap &event);
    Q_INVOKABLE void respondEvent(const QString &id, const QString &response);
    Q_INVOKABLE void deleteEvent(const QString &id);
    Q_INVOKABLE void openSource(const QVariantMap &source);
  signals:
    void changed();
    void settingsChanged();
    void statusChanged();
    void draftReady(const QVariantMap &draft);
    void draftSaved();
    void eventReady(const QVariantMap &event);
    void eventSaved();
    void sourceOpened(const QString &tab);

  private slots:
    void prepareForSleep(bool sleeping);

  private:
    void fail(const QString &error);
    void complete(const QString &error, const QString &success);
    void schedulerTick();
    void classifyNext();
    void sendClaimed(const QJsonObject &draft, std::shared_ptr<QLockFile> lock = {});
    QJsonObject findEvent(const QString &id) const;
    void notify(const QString &title, const QString &body);
    Config m_config;
    Store m_store;
    HttpClient m_http;
    SecretVault m_vault;
    Provider m_provider;
    OAuth m_oauth;
    Ai m_ai;
    PortalFilePicker m_portal;
    FilePicker *m_picker;
    QVariantMap m_data;
    QString m_profile = "personal", m_folder = "inbox", m_search, m_account, m_thread;
    QString m_status, m_error, m_summary;
    QDate m_anchor = QDate::currentDate();
    bool m_demo = false, m_busy = false, m_aiBusy = false, m_classifying = false;
    QTimer m_poll, m_syncTimer, m_scheduler, m_classifier;
    qint64 m_version = -1;
    QDateTime m_previousTick, m_retryAi;
    bool m_onlineInterval = false;
    std::unique_ptr<QLockFile> m_serviceLock;
    struct LocalEdit {
        QString sql;
        QVariantList before, after;
        QString extraSql{};
        QVariantList extraBefore{}, extraAfter{};
    };
    QList<LocalEdit> m_undo, m_redo;
};
