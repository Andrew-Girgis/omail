#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QVariantList>

class Store {
  public:
    explicit Store(const QString &directory, bool readOnly = false);
    ~Store();
    bool good() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    QString directory() const { return m_directory; }
    bool exec(const QString &sql, const QVariantList &args = {});
    QJsonArray query(const QString &sql, const QVariantList &args = {}) const;
    QJsonObject one(const QString &sql, const QVariantList &args = {}) const;
    QJsonArray profiles() const;
    QJsonArray accounts(const QString &profile = {}) const;
    QJsonArray conversations(const QString &profile, const QString &folder,
                             const QString &search = {}, int limit = 500) const;
    QJsonArray thread(const QString &account, const QString &conversation) const;
    QJsonArray events(const QString &profile, const QString &from, const QString &to) const;
    QJsonArray sources(const QString &profile, const QString &search, int limit = 12) const;
    bool saveProfile(const QString &id, const QString &name, const QString &color);
    bool saveAccount(const QJsonObject &account);
    bool upsertMessage(const QJsonObject &message);
    bool upsertEvent(const QJsonObject &event);
    bool applyClassification(const QString &id, const QJsonObject &result);
    bool correctMessage(const QString &id, bool priority, const QString &reason);
    bool dismissThread(const QString &account, const QString &conversation);
    bool saveDraft(QJsonObject draft);
    bool queueDraft(const QString &id, const QDateTime &when);
    QJsonArray takeDue(const QDateTime &now, const QDateTime &previousTick, bool available);
    bool finishSend(const QString &id, bool accepted, const QString &error = {});
    void recoverInterrupted();
    void seedDemo();

  private:
    bool migrate();
    QString m_directory;
    QString m_connection;
    mutable QString m_error;
    QSqlDatabase m_db;
};
