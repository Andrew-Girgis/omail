#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

struct HttpResult {
    int status = 0;
    QByteArray bytes;
    QString error;
    QJsonObject object() const;
    bool ok() const { return error.isEmpty() && status >= 200 && status < 300; }
};
using HttpCallback = std::function<void(const HttpResult &)>;
class HttpClient : public QObject {
    Q_OBJECT
  public:
    explicit HttpClient(QObject *parent = nullptr) : QObject(parent) {}
    virtual void request(const QByteArray &method, const QUrl &url, const QByteArray &body,
                         const QMap<QByteArray, QByteArray> &headers, HttpCallback callback,
                         int timeout = 45000);

  private:
    QNetworkAccessManager m_network;
};
