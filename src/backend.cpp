#include "backend.h"
#include "mailcodec.h"
#include <QColor>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkInformation>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>

namespace {
QString json(const QJsonArray &o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString lockPath(const Store &s, const QString &id) {
    return s.directory() + "/send-" +
           QString::fromLatin1(
               QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           ".lock";
}
} // namespace
Backend::Backend(const QString &directory, const QString &configDirectory, bool demo,
                 QObject *parent, FilePicker *picker)
    : QObject(parent), m_config(configDirectory), m_store(directory),
      m_provider(&m_store, &m_config, &m_http, &m_vault), m_oauth(&m_config, &m_http, &m_vault),
      m_ai(&m_config, &m_store, &m_http), m_picker(picker ? picker : &m_portal), m_demo(demo) {
    if (demo)
        m_store.seedDemo();
    if (!m_store.good())
        m_error = m_store.error();
    m_status = demo ? tr("Preview · made-up mail") : tr("Ready");
    connect(&m_config, &Config::changed, this, [this] {
        emit settingsChanged();
        refresh();
        if (m_syncTimer.isActive())
            m_syncTimer.setInterval(m_config.get("syncMinutes").toInt() * 60000);
    });
    connect(&m_provider, &Provider::changed, this, &Backend::refresh);
    connect(&m_provider, &Provider::progress, this, [this](const QString &s) {
        m_status = s;
        emit statusChanged();
    });
    connect(&m_oauth, &OAuth::status, this, [this](const QString &s) {
        m_status = s;
        emit statusChanged();
    });
    connect(&m_oauth, &OAuth::failed, this, &Backend::fail);
    connect(&m_oauth, &OAuth::ready, this, [this](const QJsonObject &a) {
        if (!m_store.saveAccount(a)) {
            fail(m_store.error());
            return;
        }
        m_profile = a["profile_id"].toString();
        refresh();
        sync();
    });
    connect(&m_poll, &QTimer::timeout, this, [this] {
        const auto version = m_store.one("PRAGMA data_version")["data_version"].toInteger();
        if (version != m_version) {
            m_version = version;
            refresh();
        }
    });
    m_poll.start(1500);
    refresh();
}
void Backend::fail(const QString &error) {
    m_error = error;
    emit statusChanged();
}
void Backend::clearError() {
    m_error.clear();
    emit statusChanged();
}
void Backend::complete(const QString &error, const QString &success) {
    m_busy = false;
    if (!error.isEmpty())
        m_error = error;
    else
        m_status = success;
    refresh();
    emit statusChanged();
}
void Backend::refresh() {
    QJsonObject d;
    d["profiles"] = m_store.profiles();
    d["accounts"] = m_store.accounts();
    d["profile"] = m_profile;
    d["folder"] = m_folder;
    d["threads"] = m_store.conversations(m_profile, m_folder, m_search);
    d["messages"] = m_store.thread(m_account, m_thread);
    d["selectedAccount"] = m_account;
    d["selectedThread"] = m_thread;
    QJsonObject counts;
    for (const QString &folder :
         QStringList{"inbox", "priority", "flagged", "sent", "archive", "junk", "trash"})
        counts[folder] = m_store.conversations(m_profile, folder, {}, 2000).size();
    d["counts"] = counts;
    const bool all = m_profile == "all";
    const auto args = all ? QVariantList{} : QVariantList{m_profile};
    d["topics"] = m_store.query("SELECT DISTINCT m.topic FROM messages m JOIN accounts a ON "
                                "a.id=m.account_id WHERE m.topic<>''" +
                                    QString(all ? "" : " AND a.profile_id=?") + " ORDER BY m.topic",
                                args);
    auto drafts =
        m_store.query("SELECT d.*,a.email FROM drafts d JOIN accounts "
                      "a ON a.id=d.account_id WHERE d.status<>'sent'" +
                          QString(all ? "" : " AND a.profile_id=?") + " ORDER BY d.updated_at DESC",
                      args);
    for (int i = 0; i < drafts.size(); ++i) {
        auto row = drafts[i].toObject();
        row["content"] = QJsonDocument::fromJson(row["payload"].toString().toUtf8()).object();
        row.remove("payload");
        drafts[i] = row;
    }
    d["drafts"] = drafts;
    d["calendars"] =
        m_store.query("SELECT c.*,a.email FROM calendars c JOIN accounts a ON "
                      "a.id=c.account_id" +
                          QString(all ? "" : " WHERE a.profile_id=?") + " ORDER BY c.name",
                      args);
    const QString view = m_config.get("calendarView").toString();
    QDate first = view == "month" ? QDate(m_anchor.year(), m_anchor.month(), 1) : m_anchor;
    if (view != "agenda")
        first = first.addDays(1 - first.dayOfWeek());
    const int days = view == "month" ? 42 : view == "week" ? 7 : 30;
    QJsonArray dates;
    for (int i = 0; i < days; ++i)
        dates.append(first.addDays(i).toString(Qt::ISODate));
    d["dates"] = dates;
    d["anchor"] = m_anchor.toString(Qt::ISODate);
    d["events"] = m_store.events(
        m_profile, QDateTime(first, QTime(0, 0)).toUTC().toString(Qt::ISODateWithMs),
        QDateTime(first.addDays(days), QTime(0, 0)).toUTC().toString(Qt::ISODateWithMs));
    auto chats = m_store.query("SELECT * FROM (SELECT * FROM chats WHERE profile_id=? "
                               "ORDER BY id DESC LIMIT 50) ORDER BY id",
                               {m_profile});
    for (int i = 0; i < chats.size(); ++i) {
        auto c = chats[i].toObject();
        c["sources"] = QJsonDocument::fromJson(c["sources"].toString().toUtf8()).array();
        chats[i] = c;
    }
    d["chats"] = chats;
    d["pendingClassification"] =
        m_store.one("SELECT COUNT(*) AS n FROM messages m JOIN accounts a ON "
                    "a.id=m.account_id WHERE m.classified=0 AND m.folder='inbox'" +
                        QString(all ? "" : " AND a.profile_id=?"),
                    args)["n"];
    d["aiError"] =
        m_store.one("SELECT value FROM service_state WHERE key='ai_error'")["value"].toString();
    m_data = d.toVariantMap();
    emit changed();
}
void Backend::selectProfile(const QString &profile) {
    if (profile != "all" && m_store.one("SELECT id FROM profiles WHERE id=?", {profile}).isEmpty())
        return;
    m_profile = profile;
    m_account.clear();
    m_thread.clear();
    m_summary.clear();
    refresh();
    emit statusChanged();
}
void Backend::selectFolder(const QString &folder) {
    m_folder = folder;
    m_account.clear();
    m_thread.clear();
    m_summary.clear();
    refresh();
    emit statusChanged();
}
void Backend::selectThread(const QString &account, const QString &thread) {
    m_account = account;
    m_thread = thread;
    m_summary.clear();
    refresh();
    emit statusChanged();
}
void Backend::search(const QString &term) {
    m_search = term;
    refresh();
}
bool Backend::setSetting(const QString &key, const QVariant &v) {
    QString error;
    if (!m_config.set(key, QJsonValue::fromVariant(v), &error)) {
        fail(error);
        return false;
    }
    return true;
}
bool Backend::moveView(const QString &source, const QString &target) {
    auto views = m_config.get("viewOrder").toString().split(',');
    const int from = views.indexOf(source);
    const int to = views.indexOf(target);
    if (from < 0 || to < 0 || from == to)
        return false;
    views.move(from, to);
    return setSetting("viewOrder", views.join(','));
}
void Backend::saveProfile(const QString &id, const QString &name, const QString &color) {
    if (name.trimmed().isEmpty() || name.size() > 80 || !QColor(color).isValid()) {
        fail(tr("Choose a profile name and a valid colour."));
        return;
    }
    const QString key = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    if (!m_store.saveProfile(key, name.trimmed(), color))
        fail(m_store.error());
    else
        refresh();
}
void Backend::assignAccount(const QString &account, const QString &profile) {
    if (!m_store.exec("UPDATE accounts SET profile_id=? WHERE id=?", {profile, account}))
        fail(m_store.error());
    else
        refresh();
}
void Backend::connectAccount(const QString &provider, const QString &profile) {
    if (m_demo) {
        fail(tr("Close the preview to connect real accounts."));
        return;
    }
    if (profile == "all" ||
        m_store.one("SELECT id FROM profiles WHERE id=?", {profile}).isEmpty()) {
        fail(tr("Choose a profile for this account."));
        return;
    }
    if (provider == "google")
        m_vault.read("provider:google", [this, profile](const QJsonObject &v, const QString &e) {
            if (!e.isEmpty()) {
                fail(e);
                return;
            }
            if (v["client_secret"].toString().isEmpty()) {
                fail(tr("Import your Google Desktop OAuth JSON first."));
                return;
            }
            m_oauth.begin("google", profile, v["client_secret"].toString());
        });
    else
        m_oauth.begin(provider, profile);
}
void Backend::importGoogleClient() {
    m_picker->open(tr("Choose Google Desktop OAuth JSON"), false,
                   [this](const QStringList &files, const QString &e) {
                       if (!e.isEmpty()) {
                           fail(e);
                           return;
                       }
                       if (files.isEmpty())
                           return;
                       QFile file(files.first());
                       if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
                           fail(tr("Cannot read this OAuth file."));
                           return;
                       }
                       const auto client =
                           QJsonDocument::fromJson(file.readAll()).object()["installed"].toObject();
                       if (client["client_id"].toString().isEmpty() ||
                           client["client_secret"].toString().isEmpty()) {
                           fail(tr("Choose credentials for a Google Desktop application."));
                           return;
                       }
                       m_vault.write(
                           "provider:google", {{"client_secret", client["client_secret"]}},
                           [this, client](const QJsonObject &, const QString &error) {
                               if (!error.isEmpty()) {
                                   fail(error);
                                   return;
                               }
                               setSetting("googleClientId", client["client_id"].toString());
                               m_status = tr("Google OAuth configured. Connect your account next.");
                               emit statusChanged();
                           });
                   });
}
void Backend::openLink(const QString &text) {
    const QUrl url(text);
    if (!QStringList{"https", "http"}.contains(url.scheme()) || url.host().isEmpty()) {
        fail(tr("Only web links can be opened here."));
        return;
    }
    QDesktopServices::openUrl(url);
}
void Backend::openGuide() {
    QFile source(":/docs/accounts.md");
    if (!source.open(QIODevice::ReadOnly)) {
        fail(tr("The setup guide is missing."));
        return;
    }
    const QString path = m_store.directory() + "/account-setup.md";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(file.errorString());
        return;
    }
    file.write(source.readAll());
    if (!file.commit()) {
        fail(file.errorString());
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void Backend::sync() {
    if (m_busy)
        return;
    const auto accounts = m_store.accounts();
    if (accounts.isEmpty()) {
        m_status = tr("Connect an account in Settings to start syncing.");
        emit statusChanged();
        return;
    }
    m_busy = true;
    emit statusChanged();
    auto remaining = std::make_shared<int>(accounts.size() * 2);
    auto errors = std::make_shared<QStringList>();
    auto finish = [this, remaining, errors](const QString &error) {
        if (!error.isEmpty())
            errors->append(error);
        if (--*remaining == 0) {
            complete(errors->join('\n'), tr("Up to date"));
        }
    };
    const auto today = QDateTime::currentDateTimeUtc();
    for (const auto &a : accounts) {
        m_provider.syncMail(a.toObject(), finish);
        m_provider.syncCalendar(a.toObject(), today.addDays(-90), today.addDays(365), finish);
    }
}
void Backend::fetchOlder(const QString &account, const QString &query) {
    if (m_busy || query.trimmed().isEmpty())
        return;
    const auto a = m_store.one("SELECT * FROM accounts WHERE id=?", {account});
    if (a.isEmpty())
        return;
    m_busy = true;
    emit statusChanged();
    m_provider.syncMail(
        a,
        [this](const QString &e) {
            complete(e, tr("Requested messages are available in All mail."));
        },
        query);
}
void Backend::threadAction(const QString &action) {
    if (m_busy)
        return;
    auto rows = std::make_shared<QJsonArray>(m_store.thread(m_account, m_thread));
    if (rows->isEmpty())
        return;
    m_busy = true;
    emit statusChanged();
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, rows, action, next] {
        if (rows->isEmpty()) {
            QTimer::singleShot(0, [next] { *next = {}; });
            complete({}, tr("Conversation updated"));
            return;
        }
        const auto message = rows->takeAt(0).toObject();
        m_provider.mailAction(message, action, [this, next](const QString &e) {
            if (!e.isEmpty()) {
                QTimer::singleShot(0, [next] { *next = {}; });
                complete(e, {});
            } else
                (*next)();
        });
    };
    (*next)();
}
void Backend::correctPriority(const QString &id, bool priority) {
    const auto before = m_store.one(
        "SELECT account_id,thread_id,priority,override,reason,classified FROM messages WHERE id=?",
        {id});
    if (before.isEmpty())
        return;
    const QString reason =
        priority ? tr("Marked important by you") : tr("Marked not important by you");
    const auto dismissal =
        m_store.one("SELECT through_date FROM dismissals WHERE account_id=? AND thread_id=?",
                    {before["account_id"].toString(), before["thread_id"].toString()});
    if (!m_store.correctMessage(id, priority, reason)) {
        fail(m_store.error());
        return;
    }
    m_undo.append(
        {"UPDATE messages SET priority=?,override=?,reason=?,classified=? WHERE "
         "id=?",
         {before["priority"].toInt(), before["override"].toVariant(), before["reason"].toString(),
          before["classified"].toInt(), id},
         {priority, priority, reason, 1, id},
         "INSERT INTO dismissals(account_id,thread_id,through_date) VALUES(?,?,?) ON "
         "CONFLICT(account_id,thread_id) DO UPDATE SET through_date=excluded.through_date",
         {before["account_id"].toString(), before["thread_id"].toString(),
          dismissal["through_date"].toString("")},
         {before["account_id"].toString(), before["thread_id"].toString(),
          priority ? QString("") : dismissal["through_date"].toString("")}});
    m_redo.clear();
    refresh();
}
void Backend::setTopic(const QString &id, const QString &topic) {
    const auto before = m_store.one("SELECT topic FROM messages WHERE id=?", {id});
    if (before.isEmpty())
        return;
    LocalEdit e{"UPDATE messages SET topic=? WHERE id=?",
                {before["topic"].toString(), id},
                {topic.trimmed().left(80), id}};
    if (m_store.exec(e.sql, e.after)) {
        m_undo.append(e);
        m_redo.clear();
        refresh();
    } else
        fail(m_store.error());
}
void Backend::dismissPriority() {
    const auto before =
        m_store.one("SELECT through_date FROM dismissals WHERE account_id=? AND thread_id=?",
                    {m_account, m_thread});
    if (!m_store.dismissThread(m_account, m_thread)) {
        fail(m_store.error());
        return;
    }
    const auto after =
        m_store.one("SELECT through_date FROM dismissals WHERE account_id=? AND thread_id=?",
                    {m_account, m_thread});
    if (after.isEmpty())
        return;
    m_undo.append({"INSERT INTO dismissals(account_id,thread_id,through_date) "
                   "VALUES(?,?,?) ON CONFLICT(account_id,thread_id) DO UPDATE "
                   "SET through_date=excluded.through_date",
                   {m_account, m_thread, before["through_date"].toString("")},
                   {m_account, m_thread, after["through_date"].toString()}});
    m_redo.clear();
    refresh();
}
void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const auto e = m_undo.takeLast();
    if (m_store.exec(e.sql, e.before)) {
        if (!e.extraSql.isEmpty())
            m_store.exec(e.extraSql, e.extraBefore);
        m_redo.append(e);
        refresh();
    } else
        fail(m_store.error());
}
void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    const auto e = m_redo.takeLast();
    if (m_store.exec(e.sql, e.after)) {
        if (!e.extraSql.isEmpty())
            m_store.exec(e.extraSql, e.extraAfter);
        m_undo.append(e);
        refresh();
    } else
        fail(m_store.error());
}
void Backend::newDraft(const QString &mode) {
    const auto accounts = m_store.accounts(m_profile);
    if (accounts.isEmpty()) {
        fail(tr("Connect an account before composing."));
        return;
    }
    QJsonObject d{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                  {"account_id", accounts.first().toObject()["id"]},
                  {"mode", mode},
                  {"to", ""},
                  {"cc", ""},
                  {"bcc", ""},
                  {"subject", ""},
                  {"body", ""},
                  {"attachments", QJsonArray()}};
    const auto thread = m_store.thread(m_account, m_thread);
    if (mode == "reply" && !thread.isEmpty()) {
        emit draftReady(MailCodec::replyDraft(thread).toVariantMap());
        return;
    }
    if (mode != "new" && !thread.isEmpty()) {
        const auto m = thread.last().toObject();
        d["account_id"] = m["account_id"];
        d["thread_id"] = m["thread_id"];
        d["remote_id"] = m["remote_id"];
        d["in_reply_to"] = m["internet_id"];
        d["internet_id"] = m["internet_id"];
        d["subject"] = m["subject"].toString().startsWith("Fwd:", Qt::CaseInsensitive)
                           ? m["subject"]
                           : QJsonValue("Fwd: " + m["subject"].toString());
        d["body"] = "\n\n---------- Forwarded message ----------\n" + m["sender"].toString() +
                    " <" + m["sender_email"].toString() + ">\n" + m["received"].toString() +
                    "\n\n" + m["body"].toString();
    }
    emit draftReady(d.toVariantMap());
}
void Backend::editDraft(const QString &id) {
    const auto row = m_store.one("SELECT * FROM drafts WHERE id=? AND status IN "
                                 "('draft','scheduled','held')",
                                 {id});
    if (row.isEmpty())
        return;
    if (row["status"] == "scheduled")
        m_store.exec("UPDATE drafts SET status='draft',scheduled_at='' WHERE id=? "
                     "AND status='scheduled'",
                     {id});
    auto d = QJsonDocument::fromJson(row["payload"].toString().toUtf8()).object();
    d["previous_error"] = row["error"];
    emit draftReady(d.toVariantMap());
    refresh();
}
bool Backend::saveDraft(const QVariantMap &draft) {
    if (!m_store.saveDraft(QJsonObject::fromVariantMap(draft))) {
        fail(m_store.error());
        return false;
    }
    refresh();
    m_status = tr("Draft saved on this desktop");
    emit statusChanged();
    return true;
}
bool Backend::approveDraft(const QVariantMap &value, const QString &when) {
    const auto draft = QJsonObject::fromVariantMap(value);
    const QString error = MailCodec::validateDraft(draft);
    if (!error.isEmpty()) {
        fail(error);
        return false;
    }
    if (m_demo) {
        fail(tr("Preview messages cannot be sent or scheduled."));
        return false;
    }
    if (!saveDraft(value))
        return false;
    const QString id = draft["id"].toString();
    if (!when.isEmpty()) {
        const auto date = QDateTime::fromString(when, Qt::ISODate);
        if (!date.isValid() || !m_store.queueDraft(id, date)) {
            fail(tr("Choose a future send time in your local timezone."));
            return false;
        }
        m_status = tr("Scheduled. Missed times will be held for review.");
        refresh();
        emit statusChanged();
        emit draftSaved();
        return true;
    }
    auto lock = std::make_shared<QLockFile>(lockPath(m_store, id));
    if (!lock->tryLock(0)) {
        fail(tr("This message is already being sent."));
        return false;
    }
    const auto row = m_store.one("SELECT status FROM drafts WHERE id=?", {id});
    if (!QStringList{"draft", "held"}.contains(row["status"].toString())) {
        fail(tr("Cancel the schedule before sending this message now."));
        return false;
    }
    if (!m_store.exec("UPDATE drafts SET status='sending',error='' WHERE id=?", {id})) {
        fail(m_store.error());
        return false;
    }
    sendClaimed(draft, lock);
    emit draftSaved();
    return true;
}
void Backend::sendClaimed(const QJsonObject &draft, std::shared_ptr<QLockFile> lock) {
    if (!lock) {
        lock = std::make_shared<QLockFile>(lockPath(m_store, draft["id"].toString()));
        if (!lock->tryLock(0))
            return;
    }
    m_provider.send(draft, [this, draft, lock](const QString &error) {
        const QString message = error.isEmpty() ? QString()
                                                : tr("Delivery is not confirmed. Check Sent "
                                                     "before trying again. %1")
                                                      .arg(error);
        m_store.finishSend(draft["id"].toString(), error.isEmpty(), message);
        refresh();
        if (!error.isEmpty())
            fail(message);
        else {
            m_status = tr("Message accepted by the provider");
            emit statusChanged();
        }
    });
}
void Backend::cancelSchedule(const QString &id) {
    m_store.exec("UPDATE drafts SET status='draft',scheduled_at='',error='' "
                 "WHERE id=? AND status IN ('scheduled','held')",
                 {id});
    refresh();
}
void Backend::deleteDraft(const QString &id) {
    m_store.exec("DELETE FROM drafts WHERE id=? AND status IN "
                 "('draft','held','scheduled')",
                 {id});
    refresh();
}
void Backend::addAttachments(const QVariantMap &draft) {
    m_picker->open(
        tr("Attach files"), true, [this, draft](const QStringList &files, const QString &error) {
            if (!error.isEmpty()) {
                fail(error);
                return;
            }
            auto d = QJsonObject::fromVariantMap(draft);
            auto attachments = d["attachments"].toArray();
            qint64 total = 0;
            for (const auto &v : attachments)
                total += QByteArray::fromBase64(v.toObject()["data"].toString().toLatin1()).size();
            for (const auto &path : files) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    fail(file.errorString());
                    return;
                }
                if (file.size() + total > 2800000) {
                    fail(tr("Attachments can total up to 2.8 MB in v1."));
                    return;
                }
                const auto bytes = file.readAll();
                total += bytes.size();
                attachments.append(
                    QJsonObject{{"name", QFileInfo(path).fileName()},
                                {"mime", QMimeDatabase().mimeTypeForFile(path).name()},
                                {"data", QString::fromLatin1(bytes.toBase64())},
                                {"size", bytes.size()}});
            }
            d["attachments"] = attachments;
            emit draftReady(d.toVariantMap());
        });
}
void Backend::loadAttachments(const QString &id) {
    const auto message = m_store.one("SELECT * FROM messages WHERE id=?", {id});
    m_provider.attachments(message, [this, id](const QJsonArray &a, const QString &e) {
        if (!e.isEmpty()) {
            fail(e);
            return;
        }
        QJsonArray metadata;
        for (const auto &v : a) {
            auto item = v.toObject();
            item.remove("data");
            metadata.append(item);
        }
        m_store.exec("UPDATE messages SET attachments=? WHERE id=?", {json(metadata), id});
        refresh();
    });
}
void Backend::saveAttachment(const QString &id, const QVariantMap &attachment) {
    const auto message = m_store.one("SELECT * FROM messages WHERE id=?", {id});
    const auto a = QJsonObject::fromVariantMap(attachment);
    m_picker->save(QFileInfo(a["name"].toString()).fileName(),
                   [this, message, a](const QStringList &paths, const QString &e) {
                       if (!e.isEmpty()) {
                           fail(e);
                           return;
                       }
                       if (paths.isEmpty())
                           return;
                       m_provider.attachmentData(
                           message, a,
                           [this, paths](const QByteArray &bytes, const QString &error) {
                               if (!error.isEmpty()) {
                                   fail(error);
                                   return;
                               }
                               QSaveFile file(paths.first());
                               if (!file.open(QIODevice::WriteOnly) ||
                                   file.write(bytes) != bytes.size() || !file.commit()) {
                                   fail(file.errorString());
                                   return;
                               }
                               m_status = tr("Attachment saved");
                               emit statusChanged();
                           });
                   });
}
void Backend::analyzeAttachment(const QString &id, const QVariantMap &attachment) {
    if (m_aiBusy)
        return;
    const auto a = QJsonObject::fromVariantMap(attachment);
    if (a["size"].toDouble() > 20 * 1024 * 1024) {
        fail(tr("Choose an attachment smaller than 20 MB for analysis."));
        return;
    }
    m_aiBusy = true;
    emit statusChanged();
    m_provider.attachmentData(
        m_store.one("SELECT * FROM messages WHERE id=?", {id}), a,
        [this, a](const QByteArray &bytes, const QString &error) {
            if (!error.isEmpty()) {
                m_aiBusy = false;
                fail(error);
                return;
            }
            auto analyze = [this, a](const QString &text) {
                m_ai.attachment(a["name"].toString(), text,
                                tr("Summarize the document and any action items."),
                                [this](const QJsonObject &r, const QString &e) {
                                    m_aiBusy = false;
                                    if (!e.isEmpty())
                                        fail(e);
                                    else
                                        m_summary = r["text"].toString();
                                    emit statusChanged();
                                });
            };
            const QString name = a["name"].toString().toLower();
            if (name.endsWith(".pdf")) {
                auto dir = std::make_shared<QTemporaryDir>();
                QFile file(dir->filePath("attachment.pdf"));
                if (!file.open(QIODevice::WriteOnly)) {
                    m_aiBusy = false;
                    fail(file.errorString());
                    return;
                }
                file.write(bytes);
                file.close();
                auto process = new QProcess(this);
                auto timer = new QTimer(process);
                timer->setSingleShot(true);
                timer->start(20000);
                connect(timer, &QTimer::timeout, process, &QProcess::kill);
                connect(process, &QProcess::errorOccurred, this,
                        [this, process](QProcess::ProcessError e) {
                            if (e == QProcess::FailedToStart) {
                                m_aiBusy = false;
                                fail(tr("PDF analysis needs pdftotext from poppler."));
                                process->deleteLater();
                            }
                        });
                connect(process, &QProcess::finished, this,
                        [this, process, dir, analyze](int code, QProcess::ExitStatus) {
                            const auto text = QString::fromUtf8(process->readAllStandardOutput());
                            process->deleteLater();
                            if (code != 0 || text.trimmed().isEmpty()) {
                                m_aiBusy = false;
                                fail(tr("No readable text was found in this PDF."));
                                return;
                            }
                            analyze(text);
                        });
                process->start("pdftotext",
                               {"-f", "1", "-l", "30", dir->filePath("attachment.pdf"), "-"});
            } else if (a["type"].toString(a["mime"].toString()).startsWith("text/") ||
                       QStringList{"txt", "md", "csv", "json", "ics"}.contains(
                           QFileInfo(name).suffix()))
                analyze(QString::fromUtf8(bytes));
            else {
                m_aiBusy = false;
                fail(tr("Attachment analysis supports text files and text-based PDFs."));
            }
        });
}
void Backend::summarize(bool draft) {
    if (m_aiBusy)
        return;
    const auto thread = m_store.thread(m_account, m_thread);
    if (thread.isEmpty())
        return;
    m_aiBusy = true;
    emit statusChanged();
    m_ai.summarize(thread, draft,
                   [this, draft, thread, account = m_account,
                    threadId = m_thread](const QJsonObject &r, const QString &e) {
                       m_aiBusy = false;
                       if (!draft && (m_account != account || m_thread != threadId)) {
                           emit statusChanged();
                           return;
                       }
                       if (!e.isEmpty()) {
                           fail(e);
                           return;
                       }
                       if (draft) {
                           auto reply = MailCodec::replyDraft(thread);
                           reply["body"] = r["text"];
                           emit draftReady(reply.toVariantMap());
                       } else
                           m_summary = r["text"].toString();
                       emit statusChanged();
                   });
}
void Backend::ask(const QString &question) {
    if (m_aiBusy || question.trimmed().isEmpty())
        return;
    const QString profile = m_profile;
    if (profile == "all") {
        fail(tr("Choose one profile before asking AI."));
        return;
    }
    m_aiBusy = true;
    emit statusChanged();
    m_ai.ask(profile, question, [this, profile, question](const QJsonObject &r, const QString &e) {
        m_aiBusy = false;
        if (!e.isEmpty()) {
            fail(e);
            return;
        }
        m_store.exec("INSERT INTO chats(profile_id,role,body,created_at) "
                     "VALUES(?,'user',?,?)",
                     {profile, question, now()});
        m_store.exec("INSERT INTO chats(profile_id,role,body,sources,created_at) "
                     "VALUES(?,'assistant',?,?,?)",
                     {profile, r["text"].toString(), json(r["sources"].toArray()), now()});
        refresh();
        emit statusChanged();
    });
}
void Backend::checkModels() {
    m_ai.models([this](const QJsonArray &models, const QString &e) {
        if (!e.isEmpty()) {
            fail(e);
            return;
        }
        QStringList names;
        for (const auto &m : models)
            names << m.toObject()["name"].toString();
        m_status = names.isEmpty() ? tr("No local models installed. See Settings for the "
                                        "download command.")
                                   : tr("Installed models: %1").arg(names.join(", "));
        emit statusChanged();
    });
}
void Backend::classifySelected() {
    if (m_aiBusy)
        return;
    const auto thread = m_store.thread(m_account, m_thread);
    if (thread.isEmpty())
        return;
    const auto message = thread.last().toObject();
    m_aiBusy = true;
    emit statusChanged();
    m_ai.classify(message, [this, message](const QJsonObject &r, const QString &e) {
        m_aiBusy = false;
        if (!e.isEmpty())
            fail(e);
        else {
            m_store.applyClassification(message["id"].toString(), r);
            refresh();
        }
        emit statusChanged();
    });
}
void Backend::calendarNavigate(int n) {
    m_anchor = m_config.get("calendarView") == "month"
                   ? m_anchor.addMonths(n)
                   : m_anchor.addDays(n * (m_config.get("calendarView") == "week" ? 7 : 30));
    refresh();
}
void Backend::calendarToday() {
    m_anchor = QDate::currentDate();
    refresh();
}
void Backend::calendarDate(const QString &date) {
    const auto d = QDate::fromString(date, Qt::ISODate);
    if (d.isValid()) {
        m_anchor = d;
        refresh();
    }
}
QJsonObject Backend::findEvent(const QString &id) const {
    return m_store.one("SELECT e.*,c.account_id,c.remote_id AS "
                       "calendar_remote_id,c.writable,a.email,a.provider FROM "
                       "events e JOIN calendars c ON c.id=e.calendar_id JOIN "
                       "accounts a ON a.id=c.account_id WHERE e.id=?",
                       {id});
}
void Backend::newEvent(const QString &date) {
    const auto calendars = m_data["calendars"].toList();
    QString id;
    for (const auto &c : calendars)
        if (c.toMap()["writable"].toBool()) {
            id = c.toMap()["id"].toString();
            break;
        }
    if (id.isEmpty()) {
        fail(tr("Connect an account with a writable calendar first."));
        return;
    }
    const auto d = date.isEmpty() ? m_anchor : QDate::fromString(date, Qt::ISODate);
    const QDateTime start(d, QTime(10, 0));
    emit eventReady(QJsonObject{{"calendar_id", id},
                                {"title", ""},
                                {"start", start.toString(Qt::ISODate)},
                                {"end", start.addSecs(3600).toString(Qt::ISODate)},
                                {"all_day", 0},
                                {"location", ""},
                                {"body", ""},
                                {"attendee_emails", ""}}
                        .toVariantMap());
}
void Backend::editEvent(const QString &id) {
    auto e = findEvent(id);
    if (e.isEmpty())
        return;
    QStringList emails;
    for (const auto &a : QJsonDocument::fromJson(e["attendees"].toString().toUtf8()).array()) {
        const auto o = a.toObject();
        const auto email = o["email"].toString(o["emailAddress"].toObject()["address"].toString());
        if (!email.isEmpty())
            emails << email;
    }
    e["attendee_emails"] = emails.join(", ");
    emit eventReady(e.toVariantMap());
}
void Backend::saveEvent(const QVariantMap &value) {
    if (m_busy)
        return;
    auto e = QJsonObject::fromVariantMap(value);
    const auto start = QDateTime::fromString(e["start"].toString(), Qt::ISODate),
               end = QDateTime::fromString(e["end"].toString(), Qt::ISODate);
    if (e["title"].toString().trimmed().isEmpty() || !start.isValid() || !end.isValid() ||
        end <= start) {
        fail(tr("Enter a title and an end time after the start."));
        return;
    }
    e["start"] = start.toUTC().toString(Qt::ISODateWithMs);
    e["end"] = end.toUTC().toString(Qt::ISODateWithMs);
    m_busy = true;
    emit statusChanged();
    m_provider.saveEvent(e, [this](const QString &error) {
        complete(error, tr("Event saved"));
        if (error.isEmpty())
            emit eventSaved();
    });
}
void Backend::respondEvent(const QString &id, const QString &response) {
    if (m_busy)
        return;
    m_busy = true;
    emit statusChanged();
    m_provider.respondEvent(findEvent(id), response, [this](const QString &e) {
        complete(e, tr("Invitation response sent"));
        if (e.isEmpty())
            emit eventSaved();
    });
}
void Backend::deleteEvent(const QString &id) {
    if (m_busy)
        return;
    m_busy = true;
    emit statusChanged();
    m_provider.deleteEvent(findEvent(id), [this](const QString &e) {
        complete(e, tr("Event deleted"));
        if (e.isEmpty())
            emit eventSaved();
    });
}
void Backend::openSource(const QVariantMap &source) {
    if (source["type"] == "mail") {
        selectFolder("all");
        selectThread(source["account_id"].toString(), source["thread_id"].toString());
        emit sourceOpened("mail");
    } else {
        const auto e = findEvent(source["event_id"].toString());
        calendarDate(QDateTime::fromString(e["start"].toString(), Qt::ISODate)
                         .toLocalTime()
                         .date()
                         .toString(Qt::ISODate));
        emit sourceOpened("calendar");
        editEvent(source["event_id"].toString());
    }
}
bool Backend::startService() {
    m_serviceLock = std::make_unique<QLockFile>(m_store.directory() + "/service.lock");
    if (!m_serviceLock->tryLock(0))
        return false;
    m_store.recoverInterrupted();
    QNetworkInformation::loadDefaultBackend();
    if (auto network = QNetworkInformation::instance())
        connect(network, &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability state) {
                    if (state != QNetworkInformation::Reachability::Online)
                        m_onlineInterval = false;
                });
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1",
                                         "org.freedesktop.login1.Manager", "PrepareForSleep", this,
                                         SLOT(prepareForSleep(bool)));
    connect(&m_scheduler, &QTimer::timeout, this, &Backend::schedulerTick);
    m_scheduler.start(1000);
    connect(&m_syncTimer, &QTimer::timeout, this, &Backend::sync);
    m_syncTimer.start(m_config.get("syncMinutes").toInt() * 60000);
    connect(&m_classifier, &QTimer::timeout, this, &Backend::classifyNext);
    m_classifier.start(3000);
    schedulerTick();
    QTimer::singleShot(1500, this, &Backend::sync);
    return true;
}
void Backend::schedulerTick() {
    const auto current = QDateTime::currentDateTimeUtc();
    const auto network = QNetworkInformation::instance();
    const bool online =
        network && network->reachability() == QNetworkInformation::Reachability::Online;
    const auto due = m_store.takeDue(current, m_previousTick, online && m_onlineInterval);
    m_previousTick = current;
    m_onlineInterval = online;
    for (const auto &d : due)
        sendClaimed(QJsonDocument::fromJson(d.toObject()["payload"].toString().toUtf8()).object());
}
void Backend::prepareForSleep(bool) {
    // Even a short suspend makes a due appointment missed; there is no catch-up grace period.
    m_previousTick = {};
    m_onlineInterval = false;
}
void Backend::classifyNext() {
    if (m_classifying || !m_config.get("aiEnabled").toBool() ||
        m_retryAi > QDateTime::currentDateTimeUtc())
        return;
    const auto message =
        m_store.one("SELECT m.* FROM messages m JOIN accounts a ON a.id=m.account_id WHERE "
                    "m.classified=0 AND m.override IS NULL AND m.folder='inbox' AND "
                    "a.provider<>'demo' ORDER BY m.received DESC LIMIT 1");
    if (message.isEmpty())
        return;
    m_classifying = true;
    m_ai.classify(message, [this, message](const QJsonObject &r, const QString &e) {
        m_classifying = false;
        if (!e.isEmpty()) {
            m_retryAi = QDateTime::currentDateTimeUtc().addSecs(300);
            m_store.exec("INSERT OR REPLACE INTO service_state(key,value) "
                         "VALUES('ai_error',?)",
                         {e});
            return;
        }
        m_store.exec("DELETE FROM service_state WHERE key='ai_error'");
        m_store.applyClassification(message["id"].toString(), r);
        if (r["action_needed"].toBool() || r["important"].toBool())
            notify(tr("New Priority mail"), message["subject"].toString());
        refresh();
    });
}
void Backend::notify(const QString &title, const QString &body) {
    if (!m_config.get("notifications").toBool())
        return;
    auto message = QDBusMessage::createMethodCall("org.freedesktop.Notifications",
                                                  "/org/freedesktop/Notifications",
                                                  "org.freedesktop.Notifications", "Notify");
    message << QString("Omail") << uint(0) << QString("omail") << title << body << QStringList{}
            << QVariantMap{} << int(8000);
    QDBusConnection::sessionBus().asyncCall(message);
}
