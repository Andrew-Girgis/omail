#include "provider.h"
#include "mailcodec.h"
#include "oauth.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QLockFile>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

namespace {
QString enc(const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }
QString compact(const QJsonArray &a) {
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}
template <class T> void release(const std::shared_ptr<T> &callback) {
    // Recursion owns its continuation until the last asynchronous callback
    // returns.
    QTimer::singleShot(0, [callback] { *callback = {}; });
}
} // namespace
Provider::Provider(Store *store, Config *config, HttpClient *http, CredentialVault *vault,
                   QObject *parent)
    : QObject(parent), m_store(store), m_config(config), m_http(http), m_vault(vault) {}
QUrl Provider::mailUrl(const QJsonObject &a, const QString &path) {
    return QUrl((a["provider"] == "microsoft" ? "https://graph.microsoft.com/v1.0/me/"
                                              : "https://gmail.googleapis.com/gmail/v1/users/me/") +
                path);
}
QUrl Provider::calendarUrl(const QJsonObject &a, const QString &path) {
    return QUrl((a["provider"] == "microsoft" ? "https://graph.microsoft.com/v1.0/me/"
                                              : "https://www.googleapis.com/calendar/v3/") +
                path);
}
void Provider::token(const QJsonObject &account,
                     std::function<void(const QString &, const QString &)> done, int retry) {
    const QString id = account["id"].toString();
    auto lock = std::make_shared<QLockFile>(
        m_store->directory() + "/auth-" +
        QString::fromLatin1(
            QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        ".lock");
    if (!lock->tryLock(0)) {
        if (retry >= 100) {
            done({}, tr("Account authorization is busy. Try again shortly."));
            return;
        }
        QTimer::singleShot(200, this,
                           [this, account, done, retry] { token(account, done, retry + 1); });
        return;
    }
    m_vault->read(
        id, [this, account, done, lock, id](const QJsonObject &credentials, const QString &error) {
            if (!error.isEmpty()) {
                done({}, tr("Unlock the desktop keyring: %1").arg(error));
                return;
            }
            if (credentials.isEmpty()) {
                done({}, tr("Reconnect %1 in Settings.").arg(account["email"].toString()));
                return;
            }
            if (credentials["expires_at"].toDouble() > QDateTime::currentSecsSinceEpoch() + 120) {
                done(credentials["access_token"].toString(), {});
                return;
            }
            QUrlQuery form;
            form.addQueryItem("client_id", credentials["client_id"].toString());
            form.addQueryItem("grant_type", "refresh_token");
            form.addQueryItem("refresh_token", credentials["refresh_token"].toString());
            if (credentials.contains("client_secret"))
                form.addQueryItem("client_secret", credentials["client_secret"].toString());
            const QString provider = account["provider"].toString();
            if (provider == "microsoft")
                form.addQueryItem("scope", OAuth::scopes(provider));
            m_http->request(
                "POST", OAuth::tokenUrl(provider),
                form.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B"),
                {{"Content-Type", "application/x-www-form-urlencoded"}},
                [this, id, credentials, done, lock](const HttpResult &result) {
                    if (!result.ok()) {
                        done({}, tr("Account authorization needs attention: %1").arg(result.error));
                        return;
                    }
                    auto next = credentials;
                    const auto response = result.object();
                    if (response["access_token"].toString().isEmpty()) {
                        done({}, tr("The provider returned no access token."));
                        return;
                    }
                    next["access_token"] = response["access_token"];
                    if (!response["refresh_token"].toString().isEmpty())
                        next["refresh_token"] = response["refresh_token"];
                    next["expires_at"] =
                        QDateTime::currentSecsSinceEpoch() + response["expires_in"].toInt(3600);
                    m_vault->write(id, next,
                                   [done, lock](const QJsonObject &stored, const QString &error) {
                                       done(stored["access_token"].toString(), error);
                                   });
                });
        });
}
void Provider::api(const QJsonObject &account, const QByteArray &method, const QUrl &url,
                   const QJsonObject &body, HttpCallback callback) {
    const bool microsoft = account["provider"] == "microsoft";
    const QStringList hosts = microsoft ? QStringList{"graph.microsoft.com"}
                                        : QStringList{"gmail.googleapis.com", "www.googleapis.com"};
    if (url.scheme() != "https" || !hosts.contains(url.host()) || !url.userInfo().isEmpty()) {
        HttpResult r;
        r.error = tr("Rejected an unexpected provider endpoint.");
        callback(r);
        return;
    }
    token(account, [this, method, url, body, callback, microsoft](const QString &access,
                                                                  const QString &error) {
        if (!error.isEmpty()) {
            HttpResult r;
            r.error = error;
            callback(r);
            return;
        }
        QMap<QByteArray, QByteArray> headers{{"Authorization", "Bearer " + access.toUtf8()},
                                             {"Content-Type", "application/json"}};
        if (microsoft)
            headers["Prefer"] = "IdType=\"ImmutableId\", outlook.body-content-type=\"html\", "
                                "outlook.timezone=\"UTC\"";
        m_http->request(method, url,
                        body.isEmpty() ? QByteArray()
                                       : QJsonDocument(body).toJson(QJsonDocument::Compact),
                        headers, callback);
    });
}
void Provider::syncMail(const QJsonObject &account, Done done, const QString &search) {
    const QString accountId = account["id"].toString();
    if (account["provider"] == "demo") {
        done({});
        return;
    }
    if (m_syncing.contains(accountId)) {
        done(tr("This account is already syncing."));
        return;
    }
    auto lock = std::make_shared<QLockFile>(
        m_store->directory() + "/sync-" +
        QString::fromLatin1(
            QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256).toHex()) +
        ".lock");
    if (!lock->tryLock(0)) {
        done(tr("This account is syncing in the background."));
        return;
    }
    m_syncing.insert(accountId);
    const QString cutoff = QDateTime::currentDateTimeUtc()
                               .addDays(-m_config->get("cacheDays").toInt())
                               .toString(Qt::ISODateWithMs);
    auto seen = std::make_shared<QSet<QString>>();
    auto finish = [this, accountId, done, lock, seen, cutoff, search](const QString &error) {
        m_syncing.remove(accountId);
        if (error.isEmpty()) {
            if (search.isEmpty()) {
                if (m_store
                        ->one("SELECT last_sync FROM accounts WHERE id=?", {accountId})["last_sync"]
                        .toString()
                        .isEmpty())
                    m_store->exec("UPDATE messages SET classified=-1 WHERE account_id=? AND "
                                  "classified=0 AND received<?",
                                  {accountId, QDateTime::currentDateTimeUtc().addDays(-1).toString(
                                                  Qt::ISODateWithMs)});
                const auto cached =
                    m_store->query("SELECT id FROM messages WHERE account_id=? AND received>=?",
                                   {accountId, cutoff});
                m_store->exec("BEGIN IMMEDIATE");
                for (const auto &row : cached)
                    if (!seen->contains(row.toObject()["id"].toString()))
                        m_store->exec("DELETE FROM messages WHERE id=?",
                                      {row.toObject()["id"].toString()});
                m_store->exec("COMMIT");
                m_store->exec("DELETE FROM messages WHERE account_id=? AND received<? AND "
                              "flagged=0 AND (priority=0 OR EXISTS(SELECT 1 FROM dismissals d "
                              "WHERE d.account_id=messages.account_id AND "
                              "d.thread_id=messages.thread_id AND "
                              "d.through_date>=messages.received)) AND id NOT IN (SELECT id FROM "
                              "retained_messages WHERE until_date>?)",
                              {accountId, cutoff,
                               QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)});
            } else {
                for (const auto &id : *seen)
                    m_store->exec("INSERT OR REPLACE INTO "
                                  "retained_messages(id,until_date) VALUES(?,?)",
                                  {id, QDateTime::currentDateTimeUtc()
                                           .addDays(m_config->get("cacheDays").toInt())
                                           .toString(Qt::ISODateWithMs)});
            }
            m_store->exec("UPDATE accounts SET status='Connected',last_sync=?,sync_error='' "
                          "WHERE id=?",
                          {QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), accountId});
        } else
            m_store->exec("UPDATE accounts SET status='Needs attention',sync_error=? "
                          "WHERE id=?",
                          {error, accountId});
        emit changed();
        done(error);
    };
    emit progress(tr("Syncing %1…").arg(account["email"].toString()));
    if (account["provider"] == "microsoft") {
        auto folders = std::make_shared<QMap<QString, QString>>();
        auto names = std::make_shared<QStringList>(
            QStringList{"inbox", "sentitems", "drafts", "junkemail", "deleteditems", "archive"});
        auto loadPage = std::make_shared<std::function<void(QUrl)>>();
        *loadPage = [this, account, accountId, folders, loadPage, seen, finish](QUrl url) {
            api(account, "GET", url, {},
                [this, accountId, folders, loadPage, seen, finish](const HttpResult &result) {
                    if (!result.ok()) {
                        auto keep = loadPage;
                        release(keep);
                        finish(result.error);
                        return;
                    }
                    for (const auto &value : result.object()["value"].toArray()) {
                        const auto message =
                            MailCodec::microsoftMessage(value.toObject(), accountId, *folders);
                        if (!m_store->upsertMessage(message)) {
                            auto keep = loadPage;
                            release(keep);
                            finish(m_store->error());
                            return;
                        }
                        seen->insert(message["id"].toString());
                    }
                    emit changed();
                    const QString next = result.object()["@odata.nextLink"].toString();
                    if (!next.isEmpty())
                        (*loadPage)(QUrl(next));
                    else {
                        auto keep = loadPage;
                        release(keep);
                        finish({});
                    }
                });
        };
        auto loadFolder = std::make_shared<std::function<void()>>();
        *loadFolder = [this, account, names, folders, loadFolder, loadPage, cutoff, search,
                       finish] {
            if (names->isEmpty()) {
                QUrl url = mailUrl(account, "messages");
                QUrlQuery q;
                q.addQueryItem("$top", "100");
                if (search.isEmpty()) {
                    q.addQueryItem("$filter", "receivedDateTime ge " + cutoff);
                    q.addQueryItem("$orderby", "receivedDateTime desc");
                } else
                    q.addQueryItem("$search", "\"" + QString(search).replace('"', "\\\"") + "\"");
                q.addQueryItem("$select",
                               "id,conversationId,subject,from,replyTo,toRecipients,body,"
                               "receivedDateTime,parentFolderId,isRead,flag,"
                               "hasAttachments,internetMessageId,webLink");
                url.setQuery(q);
                auto keep = loadFolder;
                release(keep);
                (*loadPage)(url);
                return;
            }
            const QString name = names->takeFirst();
            api(account, "GET", mailUrl(account, "mailFolders/" + name), {},
                [folders, loadFolder, loadPage, finish, name](const HttpResult &result) {
                    if (result.status == 404) {
                        (*loadFolder)();
                        return;
                    }
                    if (!result.ok()) {
                        auto keep = loadFolder;
                        release(keep);
                        release(loadPage);
                        finish(result.error);
                        return;
                    }
                    const QMap<QString, QString> mapping{
                        {"inbox", "inbox"},    {"sentitems", "sent"},     {"drafts", "drafts"},
                        {"junkemail", "junk"}, {"deleteditems", "trash"}, {"archive", "archive"}};
                    folders->insert(result.object()["id"].toString(), mapping[name]);
                    (*loadFolder)();
                });
        };
        (*loadFolder)();
    } else {
        auto page = std::make_shared<std::function<void(QString)>>();
        *page = [this, account, accountId, search, page, seen, finish](QString pageToken) {
            QUrl url = mailUrl(account, "messages");
            QUrlQuery q;
            q.addQueryItem("maxResults", "100");
            q.addQueryItem("includeSpamTrash", "true");
            q.addQueryItem(
                "q", search.isEmpty()
                         ? "newer_than:" + QString::number(m_config->get("cacheDays").toInt()) + "d"
                         : search);
            if (!pageToken.isEmpty())
                q.addQueryItem("pageToken", pageToken);
            url.setQuery(q);
            api(account, "GET", url, {},
                [this, account, accountId, page, seen, finish](const HttpResult &result) {
                    if (!result.ok()) {
                        auto keep = page;
                        release(keep);
                        finish(result.error);
                        return;
                    }
                    auto messages =
                        std::make_shared<QJsonArray>(result.object()["messages"].toArray());
                    const QString next = result.object()["nextPageToken"].toString();
                    auto fetch = std::make_shared<std::function<void()>>();
                    *fetch = [this, account, accountId, page, messages, next, fetch, seen, finish] {
                        if (messages->isEmpty()) {
                            auto keep = fetch;
                            release(keep);
                            emit changed();
                            if (!next.isEmpty())
                                (*page)(next);
                            else {
                                auto hold = page;
                                release(hold);
                                finish({});
                            }
                            return;
                        }
                        const auto message = messages->takeAt(0).toObject();
                        api(account, "GET",
                            mailUrl(account,
                                    "messages/" + enc(message["id"].toString()) + "?format=full"),
                            {}, [this, accountId, fetch, page, seen, finish](const HttpResult &r) {
                                if (!r.ok()) {
                                    auto keep = fetch;
                                    release(keep);
                                    release(page);
                                    finish(r.error);
                                    return;
                                }
                                const auto value = MailCodec::googleMessage(r.object(), accountId);
                                if (!m_store->upsertMessage(value)) {
                                    auto keep = fetch;
                                    release(keep);
                                    release(page);
                                    finish(m_store->error());
                                    return;
                                }
                                seen->insert(value["id"].toString());
                                (*fetch)();
                            });
                    };
                    (*fetch)();
                });
        };
        (*page)({});
    }
}
void Provider::syncCalendar(const QJsonObject &account, const QDateTime &from, const QDateTime &to,
                            Done done) {
    if (account["provider"] == "demo") {
        done({});
        return;
    }
    auto lock = std::make_shared<QLockFile>(
        m_store->directory() + "/calendar-" +
        QString::fromLatin1(
            QCryptographicHash::hash(account["id"].toString().toUtf8(), QCryptographicHash::Sha256)
                .toHex()) +
        ".lock");
    if (!lock->tryLock(0)) {
        done(tr("This calendar is syncing in the background."));
        return;
    }
    const auto originalDone = done;
    done = [lock, originalDone](const QString &e) { originalDone(e); };
    const bool google = account["provider"] == "google";
    auto calendars = std::make_shared<QJsonArray>();
    auto list = std::make_shared<std::function<void(QUrl)>>();
    *list = [this, account, google, from, to, done, list, calendars](QUrl url) {
        api(account, "GET", url, {},
            [this, account, google, from, to, done, list, calendars](const HttpResult &result) {
                if (!result.ok()) {
                    auto keep = list;
                    release(keep);
                    done(result.error);
                    return;
                }
                for (const auto &c : result.object()[google ? "items" : "value"].toArray())
                    calendars->append(c);
                const QString next =
                    result.object()[google ? "nextPageToken" : "@odata.nextLink"].toString();
                if (!next.isEmpty()) {
                    (*list)(google ? calendarUrl(account,
                                                 "users/me/calendarList?maxResults=250&pageToken=" +
                                                     enc(next))
                                   : QUrl(next));
                    return;
                }
                auto keep = list;
                release(keep);
                auto each = std::make_shared<std::function<void()>>();
                *each = [this, account, google, from, to, done, calendars, each] {
                    if (calendars->isEmpty()) {
                        auto keep = each;
                        release(keep);
                        emit changed();
                        done({});
                        return;
                    }
                    const auto c = calendars->takeAt(0).toObject();
                    const QString remote = c["id"].toString(),
                                  id = account["id"].toString() + ":" + remote;
                    const QString role = c["accessRole"].toString();
                    if (!m_store->exec(
                            "INSERT INTO "
                            "calendars(id,remote_id,account_id,name,color,writable) "
                            "VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                            "name=excluded.name,color=excluded.color,writable=excluded."
                            "writable",
                            {id, remote, account["id"].toString(),
                             c[google ? "summary" : "name"].toString(),
                             google ? c["backgroundColor"].toString("#516dff") : "#516dff",
                             google ? (role == "owner" || role == "writer")
                                    : c["canEdit"].toBool()})) {
                        auto keep = each;
                        release(keep);
                        done(m_store->error());
                        return;
                    }
                    QUrl url = calendarUrl(account, "calendars/" + enc(remote) +
                                                        (google ? "/events" : "/calendarView"));
                    QUrlQuery q;
                    q.addQueryItem(google ? "timeMin" : "startDateTime",
                                   from.toUTC().toString(Qt::ISODate));
                    q.addQueryItem(google ? "timeMax" : "endDateTime",
                                   to.toUTC().toString(Qt::ISODate));
                    if (google) {
                        q.addQueryItem("singleEvents", "true");
                        q.addQueryItem("maxResults", "250");
                        q.addQueryItem("showDeleted", "true");
                    } else
                        q.addQueryItem("$top", "250");
                    url.setQuery(q);
                    const QUrl first = url;
                    auto seen = std::make_shared<QSet<QString>>();
                    auto page = std::make_shared<std::function<void(QUrl)>>();
                    *page = [this, account, google, id, from, to, first, seen, each, page,
                             done](QUrl current) {
                        api(account, "GET", current, {},
                            [this, google, id, from, to, first, seen, each, page,
                             done](const HttpResult &r) {
                                if (!r.ok()) {
                                    auto keep = page;
                                    release(keep);
                                    release(each);
                                    done(r.error);
                                    return;
                                }
                                for (const auto &raw :
                                     r.object()[google ? "items" : "value"].toArray()) {
                                    const auto object = raw.toObject();
                                    if (object["status"] == "cancelled" ||
                                        object["isCancelled"].toBool())
                                        continue;
                                    const auto event = google
                                                           ? MailCodec::googleEvent(object, id)
                                                           : MailCodec::microsoftEvent(object, id);
                                    if (event["start"].toString().isEmpty() ||
                                        event["end"].toString().isEmpty())
                                        continue;
                                    if (!m_store->upsertEvent(event)) {
                                        auto keep = page;
                                        release(keep);
                                        release(each);
                                        done(m_store->error());
                                        return;
                                    }
                                    seen->insert(event["id"].toString());
                                }
                                const QString next =
                                    r.object()[google ? "nextPageToken" : "@odata.nextLink"]
                                        .toString();
                                if (!next.isEmpty()) {
                                    QUrl u = google ? first : QUrl(next);
                                    if (google) {
                                        QUrlQuery q(u);
                                        q.addQueryItem("pageToken", next);
                                        u.setQuery(q);
                                    }
                                    (*page)(u);
                                    return;
                                }
                                const auto cached = m_store->query(
                                    "SELECT id FROM events WHERE calendar_id=? AND start<? "
                                    "AND end>?",
                                    {id, to.toUTC().toString(Qt::ISODateWithMs),
                                     from.toUTC().toString(Qt::ISODateWithMs)});
                                for (const auto &row : cached)
                                    if (!seen->contains(row.toObject()["id"].toString()))
                                        m_store->exec("DELETE FROM events WHERE id=?",
                                                      {row.toObject()["id"].toString()});
                                auto keep = page;
                                release(keep);
                                emit changed();
                                (*each)();
                            });
                    };
                    (*page)(url);
                };
                (*each)();
            });
    };
    (*list)(calendarUrl(account,
                        google ? "users/me/calendarList?maxResults=250" : "calendars?$top=100"));
}
void Provider::mailAction(const QJsonObject &message, const QString &action, Done done) {
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {message["account_id"].toString()});
    const QString id = message["id"].toString(), remote = enc(message["remote_id"].toString());
    const bool google = account["provider"] == "google";
    if (google && action == "inbox" && message["folder"] == "trash") {
        api(account, "POST", mailUrl(account, "messages/" + remote + "/untrash"), {},
            [this, message, action, done](const HttpResult &r) {
                if (!r.ok()) {
                    done(r.error);
                    return;
                }
                auto untrashed = message;
                untrashed["folder"] = "archive";
                mailAction(untrashed, action, done);
            });
        return;
    }
    QString folder;
    QJsonObject body;
    QByteArray method = "POST";
    QString path = "messages/" + remote;
    QString localSql;
    QVariantList localArgs;
    if (action == "read" || action == "unread") {
        const bool unread = action == "unread";
        method = google ? "POST" : "PATCH";
        path += google ? "/modify" : "";
        body = google
                   ? QJsonObject{{unread ? "addLabelIds" : "removeLabelIds", QJsonArray{"UNREAD"}}}
                   : QJsonObject{{"isRead", !unread}};
        localSql = "UPDATE messages SET unread=? WHERE id=?";
        localArgs = {unread, id};
    } else if (action == "flag" || action == "unflag") {
        const bool flag = action == "flag";
        method = google ? "POST" : "PATCH";
        path += google ? "/modify" : "";
        body = google
                   ? QJsonObject{{flag ? "addLabelIds" : "removeLabelIds", QJsonArray{"STARRED"}}}
                   : QJsonObject{
                         {"flag", QJsonObject{{"flagStatus", flag ? "flagged" : "notFlagged"}}}};
        localSql = "UPDATE messages SET flagged=? WHERE id=?";
        localArgs = {flag, id};
    } else if (QStringList{"archive", "trash", "junk", "inbox"}.contains(action)) {
        folder = action;
        if (google) {
            if (action == "trash")
                path += "/trash";
            else {
                path += "/modify";
                QJsonArray add, remove{"INBOX"};
                if (action == "junk")
                    add.append("SPAM");
                if (action == "inbox") {
                    add.append("INBOX");
                    remove = QJsonArray{"SPAM", "TRASH"};
                }
                body = {{"addLabelIds", add}, {"removeLabelIds", remove}};
            }
        } else {
            path += "/move";
            body = {{"destinationId", action == "trash"  ? "deleteditems"
                                      : action == "junk" ? "junkemail"
                                                         : action}};
        }
        localSql = "UPDATE messages SET folder=? WHERE id=?";
        localArgs = {folder, id};
    } else {
        done(tr("Unknown mail action."));
        return;
    }
    if (account["provider"] == "demo") {
        m_store->exec(localSql, localArgs);
        emit changed();
        done({});
        return;
    }
    api(account, method, mailUrl(account, path), body,
        [this, localSql, localArgs, done](const HttpResult &r) {
            if (!r.ok()) {
                done(r.error);
                return;
            }
            if (!m_store->exec(localSql, localArgs)) {
                done(m_store->error());
                return;
            }
            emit changed();
            done({});
        });
}
void Provider::send(const QJsonObject &draft, Done done) {
    const QString error = MailCodec::validateDraft(draft);
    if (!error.isEmpty()) {
        done(error);
        return;
    }
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {draft["account_id"].toString()});
    if (account["provider"] == "demo") {
        done(tr("Demo accounts cannot send mail."));
        return;
    }
    if (account.isEmpty()) {
        done(tr("Choose a connected sending account."));
        return;
    }
    QJsonObject payload = draft;
    payload["from"] = account["email"];
    if (account["provider"] == "google") {
        QJsonObject body{
            {"raw", QString::fromLatin1(MailCodec::mime(payload).toBase64(
                        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))}};
        if (payload["mode"] == "reply")
            body["threadId"] = payload["thread_id"];
        api(account, "POST", mailUrl(account, "messages/send"), body,
            [done](const HttpResult &r) { done(r.ok() ? QString() : r.error); });
        return;
    }
    const QString mode = payload["mode"].toString();
    const bool reply = mode == "reply" && !payload["remote_id"].toString().isEmpty();
    const QString path = reply ? "messages/" + enc(payload["remote_id"].toString()) +
                                     (mode == "reply" ? "/createReply" : "/createForward")
                               : "messages";
    const auto message = MailCodec::microsoftDraft(payload);
    api(account, "POST", mailUrl(account, path), reply ? QJsonObject() : message,
        [this, account, reply, message, done](const HttpResult &r) {
            if (!r.ok()) {
                done(r.error);
                return;
            }
            const QString id = r.object()["id"].toString();
            if (id.isEmpty()) {
                done(tr("The provider returned no draft ID."));
                return;
            }
            auto send = [this, account, id, done] {
                api(account, "POST", mailUrl(account, "messages/" + enc(id) + "/send"), {},
                    [done](const HttpResult &result) {
                        done(result.ok() ? QString() : result.error);
                    });
            };
            if (reply) {
                auto content = message;
                content.remove("attachments");
                api(account, "PATCH", mailUrl(account, "messages/" + enc(id)), content,
                    [this, account, id, message, send, done](const HttpResult &p) {
                        if (!p.ok()) {
                            done(p.error);
                            return;
                        }
                        auto attachments =
                            std::make_shared<QJsonArray>(message["attachments"].toArray());
                        auto next = std::make_shared<std::function<void()>>();
                        *next = [this, account, id, attachments, next, send, done] {
                            if (attachments->isEmpty()) {
                                release(next);
                                send();
                                return;
                            }
                            const auto attachment = attachments->takeAt(0).toObject();
                            api(account, "POST",
                                mailUrl(account, "messages/" + enc(id) + "/attachments"),
                                attachment, [next, done](const HttpResult &r) {
                                    if (!r.ok()) {
                                        release(next);
                                        done(r.error);
                                    } else
                                        (*next)();
                                });
                        };
                        (*next)();
                    });
            } else
                send();
        });
}
void Provider::saveEvent(const QJsonObject &event, Done done) {
    const auto calendar =
        m_store->one("SELECT * FROM calendars WHERE id=?", {event["calendar_id"].toString()});
    if (calendar.isEmpty() || calendar["writable"].toInt() == 0) {
        done(tr("Choose a writable calendar."));
        return;
    }
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {calendar["account_id"].toString()});
    if (account["provider"] == "demo") {
        auto e = event;
        if (e["id"].toString().isEmpty()) {
            e["id"] = "demo:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            e["remote_id"] = e["id"];
        }
        m_store->upsertEvent(e);
        emit changed();
        done({});
        return;
    }
    const bool google = account["provider"] == "google";
    const bool editing = !event["remote_id"].toString().isEmpty();
    QString path = "calendars/" + enc(calendar["remote_id"].toString()) + "/events";
    if (editing)
        path += "/" + enc(event["remote_id"].toString());
    if (google)
        path += "?sendUpdates=all";
    api(account, editing ? "PATCH" : "POST", calendarUrl(account, path),
        MailCodec::eventPayload(event, account["provider"].toString()),
        [this, google, calendar, done](const HttpResult &r) {
            if (!r.ok()) {
                done(r.error);
                return;
            }
            if (!m_store->upsertEvent(
                    google ? MailCodec::googleEvent(r.object(), calendar["id"].toString())
                           : MailCodec::microsoftEvent(r.object(), calendar["id"].toString()))) {
                done(m_store->error());
                return;
            }
            emit changed();
            done({});
        });
}
void Provider::respondEvent(const QJsonObject &event, const QString &response, Done done) {
    if (!QStringList{"accepted", "declined", "tentative"}.contains(response)) {
        done(tr("Unknown invitation response."));
        return;
    }
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {event["account_id"].toString()});
    if (account["provider"] == "demo") {
        m_store->exec("UPDATE events SET response=? WHERE id=?",
                      {response, event["id"].toString()});
        emit changed();
        done({});
        return;
    }
    const bool google = account["provider"] == "google";
    QJsonObject body;
    QString path;
    QByteArray method;
    if (google) {
        auto attendees = QJsonDocument::fromJson(event["attendees"].toString().toUtf8()).array();
        bool found = false;
        for (int i = 0; i < attendees.size(); ++i) {
            auto a = attendees[i].toObject();
            if (a["email"].toString().compare(account["email"].toString(), Qt::CaseInsensitive) ==
                0) {
                a["responseStatus"] = response;
                attendees[i] = a;
                found = true;
            }
        }
        if (!found) {
            done(tr("This event has no invitation for the selected account."));
            return;
        }
        body = {{"attendees", attendees}};
        method = "PATCH";
        path = "calendars/" + enc(event["calendar_remote_id"].toString()) + "/events/" +
               enc(event["remote_id"].toString()) + "?sendUpdates=all";
    } else {
        method = "POST";
        path = "events/" + enc(event["remote_id"].toString()) + "/" +
               (response == "accepted"   ? "accept"
                : response == "declined" ? "decline"
                                         : "tentativelyAccept");
        body = {{"sendResponse", true}};
    }
    api(account, method, calendarUrl(account, path), body,
        [this, event, response, done](const HttpResult &r) {
            if (!r.ok()) {
                done(r.error);
                return;
            }
            m_store->exec("UPDATE events SET response=? WHERE id=?",
                          {response, event["id"].toString()});
            emit changed();
            done({});
        });
}
void Provider::deleteEvent(const QJsonObject &event, Done done) {
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {event["account_id"].toString()});
    auto remove = [this, event, done] {
        m_store->exec("DELETE FROM events WHERE id=?", {event["id"].toString()});
        emit changed();
        done({});
    };
    if (account["provider"] == "demo") {
        remove();
        return;
    }
    const bool google = account["provider"] == "google";
    const QString path = google ? "calendars/" + enc(event["calendar_remote_id"].toString()) +
                                      "/events/" + enc(event["remote_id"].toString()) +
                                      "?sendUpdates=all"
                                : "events/" + enc(event["remote_id"].toString());
    api(account, "DELETE", calendarUrl(account, path), {}, [remove, done](const HttpResult &r) {
        if (r.ok())
            remove();
        else
            done(r.error);
    });
}
void Provider::attachments(const QJsonObject &message,
                           std::function<void(const QJsonArray &, const QString &)> done) {
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {message["account_id"].toString()});
    if (account["provider"] != "microsoft") {
        done(QJsonDocument::fromJson(message["attachments"].toString().toUtf8()).array(), {});
        return;
    }
    api(account, "GET",
        mailUrl(account, "messages/" + enc(message["remote_id"].toString()) + "/attachments"), {},
        [this, message, done](const HttpResult &r) {
            if (!r.ok()) {
                done({}, r.error);
                return;
            }
            QJsonArray array;
            for (const auto &value : r.object()["value"].toArray()) {
                const auto a = value.toObject();
                array.append(QJsonObject{
                    {"id", a["id"]},
                    {"name", a["name"]},
                    {"size", a["size"]},
                    {"type", a["contentType"]},
                    {"data", a["contentBytes"]},
                    {"supported", a["@odata.type"] == "#microsoft.graph.fileAttachment"}});
            }
            // Attachment bytes remain transient until the user explicitly saves
            // them.
            QJsonArray metadata;
            for (const auto &v : array) {
                auto a = v.toObject();
                a.remove("data");
                metadata.append(a);
            }
            m_store->exec("UPDATE messages SET attachments=? WHERE id=?",
                          {compact(metadata), message["id"].toString()});
            emit changed();
            done(array, {});
        });
}
void Provider::attachmentData(const QJsonObject &message, const QJsonObject &attachment,
                              std::function<void(const QByteArray &, const QString &)> done) {
    if (attachment.contains("data")) {
        done(QByteArray::fromBase64(attachment["data"].toString().toLatin1()), {});
        return;
    }
    if (attachment.contains("inline_data") && !attachment["inline_data"].toString().isEmpty()) {
        done(QByteArray::fromBase64(attachment["inline_data"].toString().toLatin1(),
                                    QByteArray::Base64UrlEncoding),
             {});
        return;
    }
    const auto account =
        m_store->one("SELECT * FROM accounts WHERE id=?", {message["account_id"].toString()});
    const bool google = account["provider"] == "google";
    api(account, "GET",
        mailUrl(account, "messages/" + enc(message["remote_id"].toString()) + "/attachments/" +
                             enc(attachment["id"].toString())),
        {}, [done, google](const HttpResult &r) {
            if (!r.ok()) {
                done({}, r.error);
                return;
            }
            const auto object = r.object();
            const QString key = google ? "data" : "contentBytes";
            if (!object.contains(key)) {
                done({}, QObject::tr("This attachment type cannot be downloaded by Omail."));
                return;
            }
            done(QByteArray::fromBase64(object[key].toString().toLatin1(),
                                        google ? QByteArray::Base64UrlEncoding
                                               : QByteArray::Base64Encoding),
                 {});
        });
}
