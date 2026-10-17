#include "oauth.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {
QString randomToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (int i = 0; i < 32; ++i)
        bytes[i] = char(QRandomGenerator::system()->generate() & 0xff);
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
} // namespace
OAuth::OAuth(Config *config, HttpClient *http, CredentialVault *vault, QObject *parent)
    : QObject(parent), m_config(config), m_http(http), m_vault(vault) {
    connect(&m_server, &QTcpServer::newConnection, this, &OAuth::receive);
    connect(this, &OAuth::ready, this, [this] {
        m_busy = false;
        m_secret.clear();
    });
    connect(this, &OAuth::failed, this, [this] {
        m_busy = false;
        m_secret.clear();
        m_server.close();
        m_timeout.stop();
    });
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(300000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_server.close();
        emit failed(tr("Sign-in timed out. Please try again."));
    });
}
QString OAuth::scopes(const QString &provider) {
    return provider == "microsoft"
               ? "openid profile offline_access User.Read Mail.ReadWrite "
                 "Mail.Send Calendars.ReadWrite"
               : "openid email https://www.googleapis.com/auth/gmail.modify "
                 "https://www.googleapis.com/auth/gmail.send "
                 "https://www.googleapis.com/auth/calendar.events "
                 "https://www.googleapis.com/auth/calendar.calendarlist.readonly";
}
QUrl OAuth::tokenUrl(const QString &provider) {
    return QUrl(provider == "microsoft"
                    ? "https://login.microsoftonline.com/common/oauth2/v2.0/token"
                    : "https://oauth2.googleapis.com/token");
}
void OAuth::begin(const QString &provider, const QString &profile, const QString &googleSecret) {
    if (m_busy) {
        emit status(tr("Finish the existing sign-in first."));
        return;
    }
    if (provider != "microsoft" && provider != "google") {
        emit failed(tr("Choose Microsoft or Google."));
        return;
    }
    m_clientId = m_config->get(provider + "ClientId").toString().trimmed();
    if (m_clientId.isEmpty()) {
        emit failed(tr("Add the %1 OAuth client ID in Settings before connecting.").arg(provider));
        return;
    }
    m_provider = provider;
    m_profile = profile;
    m_secret = googleSecret;
    if (!m_server.listen(QHostAddress::LocalHost, 0)) {
        emit failed(tr("Cannot open the local sign-in callback: %1").arg(m_server.errorString()));
        return;
    }
    m_busy = true;
    m_state = randomToken();
    m_verifier = randomToken();
    m_redirect = QString("http://%1:%2/")
                     .arg(provider == "microsoft" ? "localhost" : "127.0.0.1")
                     .arg(m_server.serverPort());
    const auto challenge =
        QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl url(provider == "microsoft"
                 ? "https://login.microsoftonline.com/common/oauth2/v2.0/authorize"
                 : "https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery query;
    query.addQueryItem("client_id", m_clientId);
    query.addQueryItem("redirect_uri", m_redirect);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", scopes(provider));
    query.addQueryItem("state", m_state);
    query.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("prompt", provider == "google" ? "consent" : "select_account");
    if (provider == "google")
        query.addQueryItem("access_type", "offline");
    url.setQuery(query);
    if (!QDesktopServices::openUrl(url)) {
        m_server.close();
        emit failed(tr("Could not open the system browser."));
        return;
    }
    m_timeout.start();
    emit status(tr("Finish signing in through your browser."));
}
void OAuth::receive() {
    while (auto *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            if (socket->property("handled").toBool())
                return;
            buffer->append(socket->readAll());
            if (buffer->size() > 16384) {
                socket->disconnectFromHost();
                return;
            }
            if (!buffer->contains("\r\n\r\n"))
                return;
            socket->setProperty("handled", true);
            const auto parts = buffer->split('\n').first().trimmed().split(' ');
            const QUrl callback(parts.size() > 1 ? QString::fromUtf8(parts[1]) : QString());
            const QUrlQuery query(callback);
            const bool valid = !m_state.isEmpty() && parts.size() == 3 && parts[0] == "GET" &&
                               callback.path() == "/" &&
                               query.queryItemValue("state", QUrl::FullyDecoded) == m_state;
            const QByteArray message =
                valid ? "Sign-in received. You can return to Omail." : "Invalid sign-in response.";
            socket->write(QByteArray("HTTP/1.1 ") + (valid ? "200 OK" : "400 Bad Request") +
                          "\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: "
                          "no-store\r\nConnection: close\r\nContent-Length: " +
                          QByteArray::number(message.size()) + "\r\n\r\n" + message);
            socket->disconnectFromHost();
            if (!valid)
                return;
            m_state.clear();
            m_server.close();
            m_timeout.stop();
            if (query.hasQueryItem("error")) {
                emit failed(tr("Sign-in was declined: %1").arg(query.queryItemValue("error")));
                return;
            }
            const QString code = query.queryItemValue("code", QUrl::FullyDecoded);
            if (code.isEmpty()) {
                emit failed(tr("The sign-in response contained no authorization code."));
                return;
            }
            exchange(code);
        });
    }
}
void OAuth::exchange(const QString &code) {
    QUrlQuery form;
    form.addQueryItem("client_id", m_clientId);
    form.addQueryItem("code", code);
    form.addQueryItem("redirect_uri", m_redirect);
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code_verifier", m_verifier);
    if (m_provider == "google" && !m_secret.isEmpty())
        form.addQueryItem("client_secret", m_secret);
    m_http->request(
        "POST", tokenUrl(m_provider), form.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B"),
        {{"Content-Type", "application/x-www-form-urlencoded"}}, [this](const HttpResult &result) {
            if (!result.ok()) {
                emit failed(result.error);
                return;
            }
            auto tokens = result.object();
            if (tokens["access_token"].toString().isEmpty() ||
                tokens["refresh_token"].toString().isEmpty()) {
                emit failed(tr("The provider did not grant offline access. Reconnect "
                               "and approve offline access."));
                return;
            }
            tokens["client_id"] = m_clientId;
            tokens["provider"] = m_provider;
            if (!m_secret.isEmpty())
                tokens["client_secret"] = m_secret;
            m_secret.clear();
            tokens["expires_at"] =
                QDateTime::currentSecsSinceEpoch() + tokens["expires_in"].toInt(3600);
            identify(tokens);
        });
}
void OAuth::identify(QJsonObject credentials) {
    const QUrl url(m_provider == "microsoft"
                       ? "https://graph.microsoft.com/v1.0/"
                         "me?$select=id,displayName,mail,userPrincipalName"
                       : "https://gmail.googleapis.com/gmail/v1/users/me/profile");
    m_http->request(
        "GET", url, {},
        {{"Authorization", "Bearer " + credentials["access_token"].toString().toUtf8()}},
        [this, credentials](const HttpResult &result) {
            if (!result.ok()) {
                emit failed(result.error);
                return;
            }
            const auto user = result.object();
            const QString email = m_provider == "microsoft"
                                      ? user["mail"].toString(user["userPrincipalName"].toString())
                                      : user["emailAddress"].toString();
            if (email.isEmpty()) {
                emit failed(tr("The provider returned no account address."));
                return;
            }
            const QString id =
                m_provider + ":" +
                QString::fromLatin1(
                    QCryptographicHash::hash(email.toLower().toUtf8(), QCryptographicHash::Sha256)
                        .toHex()
                        .left(24));
            const QJsonObject account{{"id", id},
                                      {"provider", m_provider},
                                      {"profile_id", m_profile},
                                      {"email", email},
                                      {"name", user["displayName"].toString(email)}};
            m_vault->write(
                id, credentials, [this, account](const QJsonObject &, const QString &error) {
                    if (!error.isEmpty()) {
                        emit failed(tr("Secure credential storage failed: %1").arg(error));
                        return;
                    }
                    emit ready(account);
                });
        });
}
