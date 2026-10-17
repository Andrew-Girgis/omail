#pragma once
#include "config.h"
#include "http.h"
#include "vault.h"
#include <QObject>
#include <QTcpServer>
#include <QTimer>

class OAuth : public QObject {
    Q_OBJECT
  public:
    OAuth(Config *config, HttpClient *http, CredentialVault *vault, QObject *parent = nullptr);
    void begin(const QString &provider, const QString &profile, const QString &googleSecret = {});
    static QString scopes(const QString &provider);
    static QUrl tokenUrl(const QString &provider);
  signals:
    void ready(const QJsonObject &account);
    void failed(const QString &error);
    void status(const QString &text);

  private:
    void receive();
    void exchange(const QString &code);
    void identify(QJsonObject credentials);
    Config *m_config;
    HttpClient *m_http;
    CredentialVault *m_vault;
    QTcpServer m_server;
    QTimer m_timeout;
    QString m_provider, m_profile, m_state, m_verifier, m_redirect, m_clientId, m_secret;
    bool m_busy = false;
};
