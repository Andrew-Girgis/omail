#include "mailcodec.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>
#include <QObject>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>

namespace {
QString compact(const QJsonValue &value) {
    return QString::fromUtf8(value.isArray()
                                 ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                 : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}
QString b64decode(const QString &value) {
    return QString::fromUtf8(
        QByteArray::fromBase64(value.toLatin1(), QByteArray::Base64UrlEncoding));
}
QString header(const QJsonArray &headers, const QString &name) {
    for (const auto &h : headers)
        if (h.toObject()["name"].toString().compare(name, Qt::CaseInsensitive) == 0)
            return h.toObject()["value"].toString();
    return {};
}
void parts(const QJsonObject &part, QString &text, QString &html, QJsonArray &attachments) {
    const auto body = part["body"].toObject();
    const auto name = part["filename"].toString();
    if (!name.isEmpty()) {
        attachments.append(QJsonObject{{"id", body["attachmentId"]},
                                       {"name", name},
                                       {"size", body["size"]},
                                       {"type", part["mimeType"]},
                                       {"inline_data", body["data"]}});
        return;
    }
    if (part["mimeType"] == "text/plain")
        text += b64decode(body["data"].toString());
    else if (part["mimeType"] == "text/html")
        html += b64decode(body["data"].toString());
    for (const auto &child : part["parts"].toArray())
        parts(child.toObject(), text, html, attachments);
}
QJsonArray msRecipients(const QString &text) {
    QJsonArray result;
    for (const auto &address : MailCodec::addresses(text))
        result.append(QJsonObject{{"emailAddress", QJsonObject{{"address", address}}}});
    return result;
}
QByteArray folded64(const QByteArray &bytes) {
    const auto base = bytes.toBase64();
    QByteArray result;
    for (int i = 0; i < base.size(); i += 76)
        result += base.mid(i, 76) + "\r\n";
    return result;
}
} // namespace
QString MailCodec::plainText(const QString &html) {
    return QTextDocumentFragment::fromHtml(html).toPlainText();
}
QString MailCodec::utc(const QString &date, const QString &zone) {
    QDateTime time = QDateTime::fromString(date, Qt::ISODateWithMs);
    if (!time.isValid())
        time = QDateTime::fromString(date, Qt::ISODate);
    if (!time.isValid() && QDate::fromString(date, Qt::ISODate).isValid())
        time = QDateTime(QDate::fromString(date, Qt::ISODate), QTime(0, 0),
                         QTimeZone::systemTimeZone());
    if (!zone.isEmpty() && !date.endsWith('Z') &&
        !QRegularExpression("[+-]\\d\\d:\\d\\d$").match(date).hasMatch()) {
        QTimeZone timezone(zone.toUtf8());
        if (!timezone.isValid())
            timezone = QTimeZone(QTimeZone::windowsIdToDefaultIanaId(zone.toUtf8()));
        if (timezone.isValid())
            time.setTimeZone(timezone);
    }
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString();
}
QJsonObject MailCodec::googleMessage(const QJsonObject &raw, const QString &account) {
    const auto payload = raw["payload"].toObject();
    const auto headers = payload["headers"].toArray();
    QString text, html;
    QJsonArray attachments;
    parts(payload, text, html, attachments);
    if (text.isEmpty())
        text = plainText(html);
    const auto labels = raw["labelIds"].toArray();
    QString folder = "archive";
    if (labels.contains("INBOX"))
        folder = "inbox";
    if (labels.contains("SENT"))
        folder = "sent";
    if (labels.contains("DRAFT"))
        folder = "drafts";
    if (labels.contains("SPAM"))
        folder = "junk";
    if (labels.contains("TRASH"))
        folder = "trash";
    QString sender = header(headers, "From"), email = sender;
    const auto match = QRegularExpression("^(.*?)\\s*<([^>]+)>$").match(sender);
    if (match.hasMatch()) {
        sender = match.captured(1).trimmed();
        email = match.captured(2);
        if (sender.startsWith('"') && sender.endsWith('"'))
            sender = sender.mid(1, sender.size() - 2);
    }
    const QString id = raw["id"].toString();
    return {{"id", account + ":" + id},
            {"remote_id", id},
            {"account_id", account},
            {"thread_id", raw["threadId"]},
            {"subject", header(headers, "Subject")},
            {"sender", sender},
            {"sender_email", email},
            {"reply_to", header(headers, "Reply-To")},
            {"recipients", header(headers, "To")},
            {"body", text},
            {"html", html},
            {"received", QDateTime::fromMSecsSinceEpoch(raw["internalDate"].toString().toLongLong(),
                                                        QTimeZone::UTC)
                             .toString(Qt::ISODateWithMs)},
            {"folder", folder},
            {"unread", labels.contains("UNREAD") ? 1 : 0},
            {"flagged", labels.contains("STARRED") ? 1 : 0},
            {"attachments", compact(attachments)},
            {"internet_id", header(headers, "Message-ID")},
            {"web_url", "https://mail.google.com/mail/u/0/#all/" + raw["threadId"].toString()}};
}
QJsonObject MailCodec::microsoftMessage(const QJsonObject &raw, const QString &account,
                                        const QMap<QString, QString> &folders) {
    const auto sender = raw["from"].toObject()["emailAddress"].toObject();
    const auto body = raw["body"].toObject();
    const QString html = body["contentType"].toString().compare("html", Qt::CaseInsensitive) == 0
                             ? body["content"].toString()
                             : QString();
    QStringList recipients;
    for (const auto &value : raw["toRecipients"].toArray())
        recipients << value.toObject()["emailAddress"].toObject()["address"].toString();
    QStringList replyTo;
    for (const auto &value : raw["replyTo"].toArray())
        replyTo << value.toObject()["emailAddress"].toObject()["address"].toString();
    const QString id = raw["id"].toString();
    QJsonArray attachments;
    if (raw["hasAttachments"].toBool())
        attachments.append(
            QJsonObject{{"pending", true}, {"name", QObject::tr("Load attachments")}});
    return {{"id", account + ":" + id},
            {"remote_id", id},
            {"account_id", account},
            {"thread_id", raw["conversationId"].toString(id)},
            {"subject", raw["subject"]},
            {"sender", sender["name"]},
            {"sender_email", sender["address"]},
            {"reply_to", replyTo.join(", ")},
            {"recipients", recipients.join(", ")},
            {"body", html.isEmpty() ? body["content"].toString() : plainText(html)},
            {"html", html},
            {"received", utc(raw["receivedDateTime"].toString())},
            {"folder", folders.value(raw["parentFolderId"].toString(), "archive")},
            {"unread", raw["isRead"].toBool() ? 0 : 1},
            {"flagged", raw["flag"].toObject()["flagStatus"] == "flagged" ? 1 : 0},
            {"attachments", compact(attachments)},
            {"internet_id", raw["internetMessageId"]},
            {"web_url", raw["webLink"]}};
}
QJsonObject MailCodec::googleEvent(const QJsonObject &raw, const QString &calendar) {
    const auto start = raw["start"].toObject(), end = raw["end"].toObject();
    QString response;
    for (const auto &v : raw["attendees"].toArray())
        if (v.toObject()["self"].toBool())
            response = v.toObject()["responseStatus"].toString();
    return {
        {"id", calendar + ":" + raw["id"].toString()},
        {"remote_id", raw["id"]},
        {"calendar_id", calendar},
        {"title", raw["summary"].toString(QObject::tr("Untitled event"))},
        {"start",
         utc(start["dateTime"].toString(start["date"].toString()), start["timeZone"].toString())},
        {"end", utc(end["dateTime"].toString(end["date"].toString()), end["timeZone"].toString())},
        {"all_day", start.contains("date") ? 1 : 0},
        {"location", raw["location"].toString()},
        {"body", plainText(raw["description"].toString())},
        {"attendees", compact(raw["attendees"].toArray())},
        {"response", response},
        {"organizer", raw["organizer"].toObject()["email"].toString()},
        {"web_url", raw["htmlLink"].toString()},
        {"recurrence", compact(raw["recurrence"].toArray())},
        {"etag", raw["etag"].toString()},
        {"raw", compact(raw)}};
}
QJsonObject MailCodec::microsoftEvent(const QJsonObject &raw, const QString &calendar) {
    const auto start = raw["start"].toObject(), end = raw["end"].toObject();
    QJsonArray attendees;
    for (const auto &a : raw["attendees"].toArray()) {
        const auto v = a.toObject();
        attendees.append(QJsonObject{{"email", v["emailAddress"].toObject()["address"]},
                                     {"responseStatus", v["status"].toObject()["response"]}});
    }
    return {
        {"id", calendar + ":" + raw["id"].toString()},
        {"remote_id", raw["id"]},
        {"calendar_id", calendar},
        {"title", raw["subject"].toString(QObject::tr("Untitled event"))},
        {"start", utc(start["dateTime"].toString(), start["timeZone"].toString())},
        {"end", utc(end["dateTime"].toString(), end["timeZone"].toString())},
        {"all_day", raw["isAllDay"].toBool() ? 1 : 0},
        {"location", raw["location"].toObject()["displayName"].toString()},
        {"body", plainText(raw["body"].toObject()["content"].toString())},
        {"attendees", compact(attendees)},
        {"response", raw["responseStatus"].toObject()["response"].toString()},
        {"organizer", raw["organizer"].toObject()["emailAddress"].toObject()["address"].toString()},
        {"web_url", raw["webLink"].toString()},
        {"recurrence", compact(raw["recurrence"].toObject())},
        {"etag", raw["@odata.etag"].toString()},
        {"raw", compact(raw)}};
}
QStringList MailCodec::addresses(const QString &text) {
    QStringList result;
    QString current;
    bool quoted = false, escaped = false;
    int angleDepth = 0;
    const auto append = [&] {
        auto address = current.trimmed();
        const auto match = QRegularExpression("^[^<>]*<([^<>]+)>$").match(address);
        if (match.hasMatch())
            address = match.captured(1).trimmed();
        if (!address.isEmpty())
            result << address;
        current.clear();
    };
    for (const auto c : text) {
        if (escaped) {
            current += c;
            escaped = false;
            continue;
        }
        if (c == '\\' && quoted) {
            current += c;
            escaped = true;
            continue;
        }
        if (c == '"')
            quoted = !quoted;
        if (!quoted && c == '<')
            ++angleDepth;
        if (!quoted && c == '>')
            --angleDepth;
        if (!quoted && angleDepth == 0 && (c == ',' || c == ';'))
            append();
        else
            current += c;
    }
    append();
    return result;
}
QJsonObject MailCodec::replyDraft(const QJsonArray &thread) {
    if (thread.isEmpty())
        return {};
    auto message = thread.last().toObject();
    // A conversation can end with our own reply; address the latest incoming message.
    for (qsizetype i = thread.size(); i > 0; --i) {
        const auto candidate = thread.at(i - 1).toObject();
        if (candidate["folder"] != "sent" &&
            candidate["sender_email"].toString().compare(candidate["account_email"].toString(),
                                                         Qt::CaseInsensitive) != 0) {
            message = candidate;
            break;
        }
    }
    const auto own = message["account_email"].toString();
    const bool outgoing = message["folder"] == "sent" ||
                          message["sender_email"].toString().compare(own, Qt::CaseInsensitive) == 0;
    auto recipients = addresses(outgoing ? message["recipients"].toString()
                                : message["reply_to"].toString().trimmed().isEmpty()
                                    ? message["sender_email"].toString()
                                    : message["reply_to"].toString());
    if (outgoing)
        recipients.removeIf(
            [&](const QString &value) { return value.compare(own, Qt::CaseInsensitive) == 0; });
    auto subject = message["subject"].toString();
    if (!subject.startsWith("Re:", Qt::CaseInsensitive))
        subject.prepend("Re: ");
    return {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"account_id", message["account_id"]},
            {"mode", "reply"},
            {"remote_id", message["remote_id"]},
            {"thread_id", message["thread_id"]},
            {"internet_id", message["internet_id"]},
            {"in_reply_to", message["internet_id"]},
            {"to", recipients.join(", ")},
            {"cc", ""},
            {"bcc", ""},
            {"subject", subject},
            {"body", ""},
            {"attachments", QJsonArray()}};
}
QString MailCodec::validateDraft(const QJsonObject &draft) {
    for (const QString key : {"to", "cc", "bcc", "subject", "from", "internet_id", "id"})
        if (draft[key].toString().contains('\n') || draft[key].toString().contains('\r'))
            return QObject::tr("Message headers cannot contain newlines.");
    if (addresses(draft["to"].toString()).isEmpty())
        return QObject::tr("Add at least one recipient.");
    static const QRegularExpression address("^[^\\s<>@,;]+@[^\\s<>@,;]+\\.[^\\s<>@,;]+$");
    for (const QString key : {"to", "cc", "bcc"})
        for (const auto &v : addresses(draft[key].toString()))
            if (!address.match(v).hasMatch())
                return QObject::tr("Enter a valid email address: %1").arg(v);
    qint64 total = 0;
    for (const auto &v : draft["attachments"].toArray())
        total += QByteArray::fromBase64(v.toObject()["data"].toString().toLatin1()).size();
    if (total > 2800000)
        return QObject::tr("Attachments can total up to 2.8 MB in v1.");
    return {};
}
QByteArray MailCodec::mime(const QJsonObject &draft) {
    const QByteArray boundary = "omail-" + QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    QByteArray bytes = "MIME-Version: 1.0\r\nFrom: " + draft["from"].toString().toUtf8() +
                       "\r\nTo: " + addresses(draft["to"].toString()).join(", ").toUtf8() + "\r\n";
    if (!draft["cc"].toString().isEmpty())
        bytes += "Cc: " + addresses(draft["cc"].toString()).join(", ").toUtf8() + "\r\n";
    if (!draft["bcc"].toString().isEmpty())
        bytes += "Bcc: " + addresses(draft["bcc"].toString()).join(", ").toUtf8() + "\r\n";
    bytes += "Subject: =?UTF-8?B?" + draft["subject"].toString().toUtf8().toBase64() + "?=\r\n";
    bytes += "Message-ID: <" + draft["id"].toString().toUtf8() + "@omail.local>\r\n";
    bytes += "Date: " +
             QLocale::c()
                 .toString(QDateTime::currentDateTimeUtc(), "ddd, dd MMM yyyy HH:mm:ss +0000")
                 .toLatin1() +
             "\r\n";
    if (draft["mode"] == "reply" && !draft["internet_id"].toString().isEmpty())
        bytes += "In-Reply-To: " + draft["internet_id"].toString().toUtf8() +
                 "\r\nReferences: " + draft["internet_id"].toString().toUtf8() + "\r\n";
    bytes += "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"\r\n\r\n--" + boundary +
             "\r\nContent-Type: text/plain; "
             "charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
             folded64(draft["body"].toString().toUtf8());
    for (const auto &v : draft["attachments"].toArray()) {
        const auto a = v.toObject();
        QString name = a["name"].toString();
        name.remove('\r').remove('\n').remove('"');
        bytes += "\r\n--" + boundary +
                 "\r\nContent-Type: application/octet-stream\r\nContent-Disposition: "
                 "attachment; filename*=UTF-8''" +
                 QUrl::toPercentEncoding(name) + "\r\nContent-Transfer-Encoding: base64\r\n\r\n" +
                 folded64(QByteArray::fromBase64(a["data"].toString().toLatin1()));
    }
    return bytes + "\r\n--" + boundary + "--\r\n";
}
QJsonObject MailCodec::microsoftDraft(const QJsonObject &draft) {
    QJsonArray attachments;
    for (const auto &value : draft["attachments"].toArray()) {
        const auto a = value.toObject();
        attachments.append(
            QJsonObject{{"@odata.type", "#microsoft.graph.fileAttachment"},
                        {"name", a["name"]},
                        {"contentType", a["type"].toString("application/octet-stream")},
                        {"contentBytes", a["data"]}});
    }
    return {{"subject", draft["subject"]},
            {"body", QJsonObject{{"contentType", "Text"}, {"content", draft["body"]}}},
            {"toRecipients", msRecipients(draft["to"].toString())},
            {"ccRecipients", msRecipients(draft["cc"].toString())},
            {"bccRecipients", msRecipients(draft["bcc"].toString())},
            {"attachments", attachments}};
}
QJsonObject MailCodec::eventPayload(const QJsonObject &event, const QString &provider) {
    const bool google = provider == "google";
    const auto start =
        QDateTime::fromString(event["start"].toString(), Qt::ISODateWithMs).toLocalTime();
    const auto end =
        QDateTime::fromString(event["end"].toString(), Qt::ISODateWithMs).toLocalTime();
    const bool allDay = event["all_day"].toVariant().toBool();
    const QString timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    QJsonObject result;
    if (google) {
        result = {{"summary", event["title"]},
                  {"description", event["body"]},
                  {"location", event["location"]},
                  {"start", allDay ? QJsonObject{{"date", start.date().toString(Qt::ISODate)}}
                                   : QJsonObject{{"dateTime", start.toString(Qt::ISODate)},
                                                 {"timeZone", timezone}}},
                  {"end", allDay ? QJsonObject{{"date", end.date().toString(Qt::ISODate)}}
                                 : QJsonObject{{"dateTime", end.toString(Qt::ISODate)},
                                               {"timeZone", timezone}}}};
        QJsonArray attendees;
        for (const auto &email : addresses(event["attendee_emails"].toString()))
            attendees.append(QJsonObject{{"email", email}});
        result["attendees"] = attendees;
        if (event["repeat"].toString() != "none" && !event["repeat"].toString().isEmpty())
            result["recurrence"] = QJsonArray{"RRULE:FREQ=" + event["repeat"].toString().toUpper()};
    } else {
        QString zone = QString::fromUtf8(QTimeZone::ianaIdToWindowsId(timezone.toUtf8()));
        if (zone.isEmpty())
            zone = "UTC";
        result = {{"subject", event["title"]},
                  {"body", QJsonObject{{"contentType", "Text"}, {"content", event["body"]}}},
                  {"location", QJsonObject{{"displayName", event["location"]}}},
                  {"start", QJsonObject{{"dateTime", start.toString("yyyy-MM-ddTHH:mm:ss")},
                                        {"timeZone", zone}}},
                  {"end", QJsonObject{{"dateTime", end.toString("yyyy-MM-ddTHH:mm:ss")},
                                      {"timeZone", zone}}},
                  {"isAllDay", allDay}};
        QJsonArray attendees;
        for (const auto &email : addresses(event["attendee_emails"].toString()))
            attendees.append(QJsonObject{{"emailAddress", QJsonObject{{"address", email}}},
                                         {"type", "required"}});
        result["attendees"] = attendees;
        const QString repeat = event["repeat"].toString();
        if (repeat == "daily" || repeat == "weekly" || repeat == "monthly") {
            QJsonObject pattern{{"type", repeat == "monthly" ? "absoluteMonthly" : repeat},
                                {"interval", 1}};
            if (repeat == "weekly") {
                const QStringList days{"monday", "tuesday",  "wednesday", "thursday",
                                       "friday", "saturday", "sunday"};
                pattern["daysOfWeek"] = QJsonArray{days[start.date().dayOfWeek() - 1]};
                pattern["firstDayOfWeek"] = "monday";
            }
            if (repeat == "monthly")
                pattern["dayOfMonth"] = start.date().day();
            result["recurrence"] =
                QJsonObject{{"pattern", pattern},
                            {"range", QJsonObject{{"type", "noEnd"},
                                                  {"startDate", start.date().toString(Qt::ISODate)},
                                                  {"recurrenceTimeZone", zone}}}};
        }
    }
    return result;
}
