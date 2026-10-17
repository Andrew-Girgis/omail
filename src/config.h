#pragma once
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>

class Config : public QObject {
    Q_OBJECT
  public:
    explicit Config(const QString &directory, QObject *parent = nullptr);
    QJsonObject values() const { return m_values; }
    QJsonValue get(const QString &key) const { return m_values.value(key); }
    bool set(const QString &key, const QJsonValue &value, QString *error);
    static QJsonObject defaults();
    static QJsonObject schema();
    static bool validate(const QString &key, const QJsonValue &value, QString *error = nullptr);
    static QString dataDirectory();
    static QString configDirectory();
    QString path() const { return m_path; }
    void reload();
  signals:
    void changed();

  private:
    QString m_path;
    QJsonObject m_values;
    QFileSystemWatcher m_watcher;
};
