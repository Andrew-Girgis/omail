#include <libsecret/secret.h>
#include "vault.h"
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QtConcurrent>

namespace {
const SecretSchema schema = {
    "org.omarchy.omail",
    SECRET_SCHEMA_NONE,
    {{"account", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr};
using Result = QPair<QJsonObject, QString>;
} // namespace
void SecretVault::read(const QString &key, VaultCallback callback) {
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [watcher, callback] {
        const auto r = watcher->result();
        watcher->deleteLater();
        callback(r.first, r.second);
    });
    watcher->setFuture(QtConcurrent::run([key]() -> Result {
        GError *error = nullptr;
        gchar *secret = secret_password_lookup_sync(&schema, nullptr, &error, "account",
                                                    key.toUtf8().constData(), nullptr);
        if (error) {
            const QString text = QString::fromUtf8(error->message);
            g_error_free(error);
            return {{}, text};
        }
        QJsonObject value;
        if (secret) {
            value = QJsonDocument::fromJson(QByteArray(secret)).object();
            secret_password_free(secret);
        }
        return {value, {}};
    }));
}
void SecretVault::write(const QString &key, const QJsonObject &value, VaultCallback callback) {
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [watcher, callback] {
        const auto r = watcher->result();
        watcher->deleteLater();
        callback(r.first, r.second);
    });
    watcher->setFuture(QtConcurrent::run([key, value]() -> Result {
        GError *error = nullptr;
        const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
        const bool ok = secret_password_store_sync(
            &schema, SECRET_COLLECTION_DEFAULT, "Omail account credentials", bytes.constData(),
            nullptr, &error, "account", key.toUtf8().constData(), nullptr);
        if (error) {
            const QString text = QString::fromUtf8(error->message);
            g_error_free(error);
            return {{}, text};
        }
        return ok ? Result{value, {}}
                  : Result{{}, QObject::tr("The desktop keyring could not store credentials.")};
    }));
}
