#pragma once
#include <QJsonObject>
#include <QObject>
#include <functional>

using VaultCallback = std::function<void(const QJsonObject &, const QString &)>;
class CredentialVault : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void read(const QString &key, VaultCallback callback) = 0;
    virtual void write(const QString &key, const QJsonObject &value, VaultCallback callback) = 0;
};
class SecretVault : public CredentialVault {
    Q_OBJECT
  public:
    using CredentialVault::CredentialVault;
    void read(const QString &key, VaultCallback callback) override;
    void write(const QString &key, const QJsonObject &value, VaultCallback callback) override;
};
