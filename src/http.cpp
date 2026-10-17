#include "http.h"
#include <QJsonDocument>
#include <QNetworkReply>

QJsonObject HttpResult::object() const { return QJsonDocument::fromJson(bytes).object(); }
void HttpClient::request(const QByteArray &method, const QUrl &url, const QByteArray &body,
                         const QMap<QByteArray, QByteArray> &headers, HttpCallback callback,
                         int timeout) {
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout);
    // Authorization is never forwarded to an API-supplied redirect target.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("User-Agent", "Omail/0.1.0");
    for (auto it = headers.begin(); it != headers.end(); ++it)
        request.setRawHeader(it.key(), it.value());
    auto *reply = m_network.sendCustomRequest(request, method, body);
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        HttpResult result;
        result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.bytes = reply->readAll();
        if (result.status >= 200 && result.status < 300 && result.status != 202 &&
            result.status != 204) {
            QJsonParseError parseError;
            QJsonDocument::fromJson(result.bytes, &parseError);
            if (parseError.error != QJsonParseError::NoError)
                result.error = QObject::tr("The server returned an invalid JSON response.");
        }
        if (reply->error() != QNetworkReply::NoError || result.status < 200 ||
            result.status >= 300) {
            const auto object = result.object();
            QString reason = object["error"].isObject()
                                 ? object["error"].toObject()["message"].toString()
                                 : object["error_description"].toString();
            if (reason.isEmpty())
                reason = reply->errorString();
            result.error =
                QObject::tr("Request failed (%1): %2").arg(result.status).arg(reason.left(500));
        }
        reply->deleteLater();
        callback(result);
    });
}
