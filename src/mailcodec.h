#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

namespace MailCodec {
QString plainText(const QString &html);
QString utc(const QString &date, const QString &zone = {});
QJsonObject googleMessage(const QJsonObject &raw, const QString &account);
QJsonObject microsoftMessage(const QJsonObject &raw, const QString &account,
                             const QMap<QString, QString> &folders);
QJsonObject googleEvent(const QJsonObject &raw, const QString &calendar);
QJsonObject microsoftEvent(const QJsonObject &raw, const QString &calendar);
QStringList addresses(const QString &text);
QJsonObject replyDraft(const QJsonArray &thread);
QString validateDraft(const QJsonObject &draft);
QByteArray mime(const QJsonObject &draft);
QJsonObject microsoftDraft(const QJsonObject &draft);
QJsonObject eventPayload(const QJsonObject &event, const QString &provider);
} // namespace MailCodec
