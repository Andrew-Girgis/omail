#pragma once
#include "config.h"
#include "http.h"
#include "store.h"
#include "vault.h"
#include <QObject>
#include <QSet>
#include <functional>

using Done = std::function<void(const QString &)>;
class Provider : public QObject {
    Q_OBJECT
  public:
    Provider(Store *store, Config *config, HttpClient *http, CredentialVault *vault,
             QObject *parent = nullptr);
    void api(const QJsonObject &account, const QByteArray &method, const QUrl &url,
             const QJsonObject &body, HttpCallback callback);
    void syncMail(const QJsonObject &account, Done done, const QString &search = {});
    void syncCalendar(const QJsonObject &account, const QDateTime &from, const QDateTime &to,
                      Done done);
    void mailAction(const QJsonObject &message, const QString &action, Done done);
    void send(const QJsonObject &draft, Done done);
    void saveEvent(const QJsonObject &event, Done done);
    void respondEvent(const QJsonObject &event, const QString &response, Done done);
    void deleteEvent(const QJsonObject &event, Done done);
    void attachments(const QJsonObject &message,
                     std::function<void(const QJsonArray &, const QString &)> done);
    void attachmentData(const QJsonObject &message, const QJsonObject &attachment,
                        std::function<void(const QByteArray &, const QString &)> done);
    static QUrl mailUrl(const QJsonObject &account, const QString &path);
    static QUrl calendarUrl(const QJsonObject &account, const QString &path);
  signals:
    void changed();
    void progress(const QString &text);

  private:
    void token(const QJsonObject &account,
               std::function<void(const QString &, const QString &)> done, int retry = 0);
    Store *m_store;
    Config *m_config;
    HttpClient *m_http;
    CredentialVault *m_vault;
    QSet<QString> m_syncing;
};
