#include "store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

Store::Store(const QString &directory, bool readOnly)
    : m_directory(directory), m_connection(QUuid::createUuid().toString()) {
    if (!readOnly) {
        QDir().mkpath(directory);
        QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(directory + "/omail.sqlite3");
    m_db.setConnectOptions(readOnly ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"
                                    : "QSQLITE_BUSY_TIMEOUT=5000");
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return;
    }
    exec("PRAGMA foreign_keys=ON");
    if (!readOnly) {
        exec("PRAGMA journal_mode=WAL");
        migrate();
        QFile::setPermissions(m_db.databaseName(), QFile::ReadOwner | QFile::WriteOwner);
    }
}
Store::~Store() {
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}
bool Store::exec(const QString &sql, const QVariantList &args) {
    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        m_error = q.lastError().text();
        return false;
    }
    for (const auto &arg : args)
        q.addBindValue(arg.metaType().id() == QMetaType::QString && arg.toString().isNull()
                           ? QVariant(QString(""))
                           : arg);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return false;
    }
    return true;
}
QJsonArray Store::query(const QString &sql, const QVariantList &args) const {
    QJsonArray result;
    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        m_error = q.lastError().text();
        return result;
    }
    for (const auto &arg : args)
        q.addBindValue(arg);
    if (!q.exec()) {
        m_error = q.lastError().text();
        return result;
    }
    const QSqlRecord record = q.record();
    QStringList names;
    for (int i = 0; i < record.count(); ++i)
        names << record.fieldName(i);
    while (q.next()) {
        QJsonObject row;
        for (int i = 0; i < names.size(); ++i)
            row[names[i]] = QJsonValue::fromVariant(q.value(i));
        result.append(row);
    }
    return result;
}
QJsonObject Store::one(const QString &sql, const QVariantList &args) const {
    const auto rows = query(sql, args);
    return rows.isEmpty() ? QJsonObject() : rows.first().toObject();
}
bool Store::migrate() {
    const QStringList statements{
        "CREATE TABLE IF NOT EXISTS profiles(id TEXT PRIMARY KEY,name TEXT NOT "
        "NULL,color TEXT NOT NULL,cloud_opt_in INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS accounts(id TEXT PRIMARY KEY,provider TEXT "
        "NOT NULL,email TEXT NOT NULL,profile_id TEXT NOT NULL REFERENCES "
        "profiles(id),name TEXT NOT NULL DEFAULT '',status TEXT NOT NULL DEFAULT "
        "'Connected',last_sync TEXT NOT NULL DEFAULT '',sync_error TEXT NOT NULL "
        "DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS messages(id TEXT PRIMARY KEY,remote_id TEXT "
        "NOT NULL,account_id TEXT NOT NULL REFERENCES accounts(id),thread_id "
        "TEXT NOT NULL,subject TEXT NOT NULL DEFAULT '',sender TEXT NOT NULL "
        "DEFAULT '',sender_email TEXT NOT NULL DEFAULT '',reply_to TEXT NOT NULL DEFAULT "
        "'',recipients TEXT NOT "
        "NULL DEFAULT '',body TEXT NOT NULL DEFAULT '',html TEXT NOT NULL "
        "DEFAULT '',received TEXT NOT NULL,folder TEXT NOT NULL DEFAULT "
        "'inbox',unread INTEGER NOT NULL DEFAULT 1,flagged INTEGER NOT NULL "
        "DEFAULT 0,priority INTEGER NOT NULL DEFAULT 0,action_needed INTEGER NOT "
        "NULL DEFAULT 0,important INTEGER NOT NULL DEFAULT 0,reason TEXT NOT "
        "NULL DEFAULT '',topic TEXT NOT NULL DEFAULT '',classified INTEGER NOT "
        "NULL DEFAULT 0,override INTEGER,attachments TEXT NOT NULL DEFAULT "
        "'[]',internet_id TEXT NOT NULL DEFAULT '',web_url TEXT NOT NULL DEFAULT "
        "'')",
        "CREATE INDEX IF NOT EXISTS messages_thread ON "
        "messages(account_id,thread_id,received)",
        "CREATE INDEX IF NOT EXISTS messages_received ON messages(received)",
        "CREATE INDEX IF NOT EXISTS messages_folder ON messages(folder,received)",
        "CREATE TABLE IF NOT EXISTS dismissals(account_id TEXT NOT "
        "NULL,thread_id TEXT NOT NULL,through_date TEXT NOT NULL,PRIMARY "
        "KEY(account_id,thread_id))",
        "CREATE TABLE IF NOT EXISTS calendars(id TEXT PRIMARY KEY,remote_id TEXT "
        "NOT NULL,account_id TEXT NOT NULL REFERENCES accounts(id),name TEXT NOT "
        "NULL,color TEXT NOT NULL DEFAULT '#516dff',writable INTEGER NOT NULL "
        "DEFAULT 1)",
        "CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY,remote_id TEXT "
        "NOT NULL,calendar_id TEXT NOT NULL REFERENCES calendars(id),title TEXT "
        "NOT NULL,start TEXT NOT NULL,end TEXT NOT NULL,all_day INTEGER NOT NULL "
        "DEFAULT 0,location TEXT NOT NULL DEFAULT '',body TEXT NOT NULL DEFAULT "
        "'',attendees TEXT NOT NULL DEFAULT '[]',response TEXT NOT NULL DEFAULT "
        "'',organizer TEXT NOT NULL DEFAULT '',web_url TEXT NOT NULL DEFAULT "
        "'',recurrence TEXT NOT NULL DEFAULT '',etag TEXT NOT NULL DEFAULT "
        "'',raw TEXT NOT NULL DEFAULT '{}')",
        "CREATE TABLE IF NOT EXISTS drafts(id TEXT PRIMARY KEY,account_id TEXT "
        "NOT NULL REFERENCES accounts(id),payload TEXT NOT NULL,status TEXT NOT "
        "NULL DEFAULT 'draft',scheduled_at TEXT NOT NULL DEFAULT '',updated_at "
        "TEXT NOT NULL,error TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE IF NOT EXISTS chats(id INTEGER PRIMARY KEY "
        "AUTOINCREMENT,profile_id TEXT NOT NULL REFERENCES profiles(id),role "
        "TEXT NOT NULL,body TEXT NOT NULL,sources TEXT NOT NULL DEFAULT "
        "'[]',created_at TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS service_state(key TEXT PRIMARY KEY,value "
        "TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS retained_messages(id TEXT PRIMARY "
        "KEY,until_date TEXT NOT NULL)",
        "CREATE VIRTUAL TABLE IF NOT EXISTS message_search USING fts5(id "
        "UNINDEXED,subject,sender,body,tokenize='unicode61')",
        "CREATE TRIGGER IF NOT EXISTS message_search_insert AFTER INSERT ON "
        "messages BEGIN INSERT INTO message_search(id,subject,sender,body) "
        "VALUES(new.id,new.subject,new.sender || ' ' || "
        "new.sender_email,new.body); END",
        "CREATE TRIGGER IF NOT EXISTS message_search_delete AFTER DELETE ON "
        "messages BEGIN DELETE FROM message_search WHERE id=old.id; END"};
    if (!m_db.transaction())
        return false;
    for (const auto &sql : statements)
        if (!exec(sql)) {
            m_db.rollback();
            return false;
        }
    bool hasReplyTo = false;
    for (const auto &column : query("PRAGMA table_info(messages)"))
        hasReplyTo |= column.toObject()["name"] == "reply_to";
    if (!hasReplyTo && !exec("ALTER TABLE messages ADD COLUMN reply_to TEXT NOT NULL DEFAULT ''")) {
        m_db.rollback();
        return false;
    }
    if (one("PRAGMA user_version")["user_version"].toInt() < 1) {
        // Bodies cached by earlier builds kept image placeholders and invisible preview padding.
        // Sync rewrites recent mail with the new codec; this cleans everything else once.
        // The old search trigger fired on every sync, even for unchanged rows, and each run
        // scans the whole index. Replace it, and rebuild the index in one pass.
        if (!exec("DROP TRIGGER IF EXISTS message_search_update")) {
            m_db.rollback();
            return false;
        }
        QString body = "body";
        for (const int c : {0xFFFC, 0x034F, 0x00AD, 0x200B, 0x2060, 0xFEFF})
            body = QString("replace(%1,char(%2),'')").arg(body).arg(c);
        for (const char *edge : {"' '||char(8204)", "char(8204)||' '", "char(10)||char(8204)",
                                 "char(8204)||char(10)"})
            body = QString("replace(%1,%2,%3)")
                       .arg(body, edge, QString(edge).replace("char(8204)", "''"));
        if (!exec("UPDATE messages SET body=" + body + " WHERE body<>" + body) ||
            !exec("DELETE FROM message_search") ||
            !exec("INSERT INTO message_search(id,subject,sender,body) SELECT "
                  "id,subject,sender || ' ' || sender_email,body FROM messages") ||
            !exec("PRAGMA user_version=1")) {
            m_db.rollback();
            return false;
        }
    }
    if (!exec("CREATE TRIGGER IF NOT EXISTS message_search_update AFTER UPDATE OF "
              "subject,sender,sender_email,body ON messages WHEN old.subject IS NOT "
              "new.subject OR old.sender IS NOT new.sender OR old.sender_email IS NOT "
              "new.sender_email OR old.body IS NOT new.body BEGIN DELETE FROM "
              "message_search WHERE id=old.id; INSERT INTO "
              "message_search(id,subject,sender,body) VALUES(new.id,new.subject,new.sender "
              "|| ' ' || new.sender_email,new.body); END")) {
        m_db.rollback();
        return false;
    }
    if (!exec("INSERT OR IGNORE INTO profiles(id,name,color) "
              "VALUES('personal','Personal','#516dff')") ||
        !exec("INSERT OR IGNORE INTO profiles(id,name,color) "
              "VALUES('work','Work','#39bb9c')")) {
        m_db.rollback();
        return false;
    }
    return m_db.commit();
}
QJsonArray Store::profiles() const { return query("SELECT * FROM profiles ORDER BY rowid"); }
QJsonArray Store::accounts(const QString &profile) const {
    return profile.isEmpty() || profile == "all"
               ? query("SELECT * FROM accounts ORDER BY email")
               : query("SELECT * FROM accounts WHERE profile_id=? ORDER BY email", {profile});
}
bool Store::saveProfile(const QString &id, const QString &name, const QString &color) {
    return exec("INSERT INTO profiles(id,name,color) VALUES(?,?,?) ON CONFLICT(id) DO "
                "UPDATE SET name=excluded.name,color=excluded.color",
                {id, name, color});
}
bool Store::saveAccount(const QJsonObject &a) {
    return exec("INSERT INTO accounts(id,provider,email,profile_id,name) "
                "VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                "profile_id=excluded.profile_id,name=excluded.name,status='"
                "Connected',sync_error=''",
                {a["id"].toString(), a["provider"].toString(), a["email"].toString(),
                 a["profile_id"].toString(), a["name"].toString()});
}
bool Store::upsertMessage(const QJsonObject &m) {
    const QStringList fields{"id",          "remote_id",    "account_id", "thread_id",  "subject",
                             "sender",      "sender_email", "reply_to",   "recipients", "body",
                             "html",        "received",     "folder",     "unread",     "flagged",
                             "attachments", "internet_id",  "web_url"};
    QVariantList args;
    QStringList marks, updates, changed;
    for (const auto &f : fields) {
        args << (f == "unread" || f == "flagged" ? QVariant(m[f].toInt())
                                                 : QVariant(m[f].toString("")));
        marks << "?";
        if (f != "id") {
            updates << f + "=excluded." + f;
            changed << "messages." + f + " IS NOT excluded." + f;
        }
    }
    const bool oldInitial =
        one("SELECT last_sync FROM accounts WHERE id=?", {m["account_id"].toString()})["last_sync"]
            .toString()
            .isEmpty() &&
        m["received"].toString() <
            QDateTime::currentDateTimeUtc().addDays(-1).toString(Qt::ISODateWithMs);
    args << (oldInitial ? -1 : 0);
    return exec("INSERT INTO messages(" + fields.join(',') + ",classified) VALUES(" +
                    marks.join(',') + ",?) ON CONFLICT(id) DO UPDATE SET " + updates.join(',') +
                    // Sync re-reads every cached message. Leaving unchanged rows alone keeps
                    // the database quiet, so open windows have nothing to reload.
                    " WHERE " + changed.join(" OR "),
                args);
}
QJsonArray Store::conversations(const QString &profile, const QString &folder,
                                const QString &search, int limit) const {
    QVariantList args;
    QString where = "1=1";
    if (profile != "all" && !profile.isEmpty()) {
        where += " AND a.profile_id=?";
        args << profile;
    }
    if (folder == "priority")
        where += " AND m.priority=1 AND m.folder NOT IN "
                 "('junk','trash','sent','drafts') AND "
                 "m.received>COALESCE(d.through_date,'')";
    else if (folder == "flagged")
        where += " AND m.flagged=1";
    else if (folder.startsWith("topic:")) {
        where += " AND m.topic=?";
        args << folder.mid(6);
    } else if (folder != "all") {
        where += " AND m.folder=?";
        args << folder;
    }
    if (!search.trimmed().isEmpty()) {
        where += " AND (m.subject LIKE ? ESCAPE '\\' OR m.sender LIKE ? ESCAPE '\\' OR "
                 "m.sender_email LIKE ? ESCAPE '\\' OR m.body LIKE ? ESCAPE '\\')";
        QString term = search;
        term.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
        for (int i = 0; i < 4; ++i)
            args << "%" + term + "%";
    }
    args << qBound(1, limit, 2000);
    // Name the columns: m.* would drag every HTML body through the window functions.
    return query("WITH matching AS (SELECT m.id,m.account_id,m.thread_id,m.received,m.unread,"
                 "m.flagged,m.subject,m.sender,m.sender_email,substr(m.body,1,300) AS preview,"
                 "m.reason,m.topic,a.email,a.profile_id,p.color,"
                 "CASE WHEN m.received>COALESCE(d.through_date,'') THEN m.priority ELSE 0 "
                 "END AS active_priority "
                 "FROM messages m JOIN accounts a ON a.id=m.account_id "
                 "JOIN profiles p ON p.id=a.profile_id LEFT JOIN dismissals d ON "
                 "d.account_id=m.account_id AND d.thread_id=m.thread_id WHERE " +
                     where +
                     "), ranked AS (SELECT *,ROW_NUMBER() OVER (PARTITION BY "
                     "account_id,thread_id ORDER BY received DESC,id DESC) AS rn,"
                     "COUNT(*) OVER (PARTITION BY account_id,thread_id) AS count,"
                     "MAX(unread) OVER (PARTITION BY account_id,thread_id) AS any_unread,"
                     "MAX(flagged) OVER (PARTITION BY account_id,thread_id) AS "
                     "any_flagged,"
                     "MAX(active_priority) OVER (PARTITION BY account_id,thread_id) AS "
                     "any_priority FROM matching) "
                     "SELECT id,account_id,thread_id,received,count,any_unread AS "
                     "unread,any_flagged AS flagged,any_priority AS priority,"
                     "subject,sender,sender_email,preview,reason,topic,email,profile_id,"
                     "color FROM ranked WHERE rn=1 ORDER BY received DESC LIMIT ?",
                 args);
}
QJsonObject Store::counts(const QString &profile) const {
    const bool all = profile == "all" || profile.isEmpty();
    // Same rules as conversations(): a conversation counts once if any message matches.
    const QString key = "m.account_id||char(31)||m.thread_id";
    QStringList columns;
    for (const char *folder : {"inbox", "sent", "archive", "junk", "trash"})
        columns << QString("COUNT(DISTINCT CASE WHEN m.folder='%1' THEN %2 END) AS %1")
                       .arg(folder, key);
    columns << "COUNT(DISTINCT CASE WHEN m.flagged=1 THEN " + key + " END) AS flagged"
            << "COUNT(DISTINCT CASE WHEN m.priority=1 AND m.folder NOT IN "
               "('junk','trash','sent','drafts') AND m.received>COALESCE(d.through_date,'') "
               "THEN " +
                   key + " END) AS priority";
    return one("SELECT " + columns.join(',') +
                   " FROM messages m JOIN accounts a ON a.id=m.account_id LEFT JOIN dismissals d "
                   "ON d.account_id=m.account_id AND d.thread_id=m.thread_id" +
                   (all ? QString() : QString(" WHERE a.profile_id=?")),
               all ? QVariantList{} : QVariantList{profile});
}
QJsonArray Store::thread(const QString &account, const QString &conversation) const {
    return query("SELECT m.*,a.email AS account_email FROM messages m JOIN "
                 "accounts a ON a.id=m.account_id WHERE m.account_id=? AND "
                 "m.thread_id=? ORDER BY m.received",
                 {account, conversation});
}
bool Store::applyClassification(const QString &id, const QJsonObject &r) {
    if (!r["action_needed"].isBool() || !r["important"].isBool() || !r["reason"].isString())
        return false;
    return exec("UPDATE messages SET "
                "action_needed=?,important=?,priority=?,reason=?,topic=?,"
                "classified=1 WHERE id=? AND override IS NULL",
                {r["action_needed"].toBool(), r["important"].toBool(),
                 r["action_needed"].toBool() || r["important"].toBool(),
                 r["reason"].toString().left(300), r["topic"].toString().left(80), id});
}
bool Store::correctMessage(const QString &id, bool priority, const QString &reason) {
    if (!exec("UPDATE messages SET priority=?,override=?,reason=?,classified=1 "
              "WHERE id=?",
              {priority, priority, reason, id}))
        return false;
    if (priority)
        return exec("DELETE FROM dismissals WHERE (account_id,thread_id) IN "
                    "(SELECT account_id,thread_id FROM messages WHERE id=?)",
                    {id});
    return true;
}
bool Store::dismissThread(const QString &account, const QString &conversation) {
    return exec("INSERT INTO dismissals(account_id,thread_id,through_date) "
                "SELECT account_id,thread_id,MAX(received) FROM messages WHERE "
                "account_id=? AND thread_id=? GROUP BY account_id,thread_id ON "
                "CONFLICT(account_id,thread_id) DO UPDATE SET "
                "through_date=excluded.through_date",
                {account, conversation});
}
bool Store::upsertEvent(const QJsonObject &event) {
    const QStringList fields{"id",      "remote_id",  "calendar_id", "title",
                             "start",   "end",        "all_day",     "location",
                             "body",    "attendees",  "response",    "organizer",
                             "web_url", "recurrence", "etag",        "raw"};
    QStringList updates, marks;
    QVariantList values;
    for (const auto &f : fields) {
        values << (f == "all_day" ? QVariant(event[f].toInt()) : QVariant(event[f].toString("")));
        marks << "?";
        if (f != "id")
            updates << f + "=excluded." + f;
    }
    return exec("INSERT INTO events(" + fields.join(',') + ") VALUES(" + marks.join(',') +
                    ") ON CONFLICT(id) DO UPDATE SET " + updates.join(','),
                values);
}
QJsonArray Store::events(const QString &profile, const QString &from, const QString &to) const {
    QString where = "e.start<? AND e.end>?";
    QVariantList args{to, from};
    if (profile != "all" && !profile.isEmpty()) {
        where += " AND a.profile_id=?";
        args << profile;
    }
    return query("SELECT e.*,c.name AS calendar_name,c.color,c.account_id,c.remote_id AS "
                 "calendar_remote_id,a.email,a.provider,c.writable FROM events e JOIN "
                 "calendars c ON c.id=e.calendar_id JOIN accounts a ON a.id=c.account_id "
                 "WHERE " +
                     where + " ORDER BY e.start",
                 args);
}
QJsonArray Store::sources(const QString &profile, const QString &search, int limit) const {
    QStringList tokens;
    QString word;
    for (const auto &c : search) {
        if (c.isLetterOrNumber())
            word += c;
        else if (!word.isEmpty()) {
            if (word.size() > 2)
                tokens << "\"" + word + "\"";
            word.clear();
        }
    }
    if (word.size() > 2)
        tokens << "\"" + word + "\"";
    QJsonArray result;
    if (!tokens.isEmpty())
        result = query("SELECT "
                       "m.id,m.subject,m.sender,m.body,m.received,m.thread_id,m.account_id "
                       "FROM message_search f JOIN messages m ON m.id=f.id JOIN accounts a ON "
                       "a.id=m.account_id WHERE message_search MATCH ? AND a.profile_id=? AND "
                       "m.folder NOT IN ('junk','trash') ORDER BY rank LIMIT ?",
                       {tokens.join(" OR "), profile, limit});
    if (result.isEmpty())
        result = query("SELECT "
                       "m.id,m.subject,m.sender,m.body,m.received,m.thread_id,m.account_"
                       "id FROM messages m JOIN accounts a ON a.id=m.account_id WHERE "
                       "a.profile_id=? AND m.folder NOT IN ('junk','trash') ORDER BY "
                       "m.priority DESC,m.received DESC LIMIT ?",
                       {profile, limit});
    return result;
}
bool Store::saveDraft(QJsonObject draft) {
    QString id = draft["id"].toString();
    if (id.isEmpty())
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto existing = one("SELECT status FROM drafts WHERE id=?", {id});
    if (!existing.isEmpty() &&
        !QStringList{"draft", "held", "scheduled"}.contains(existing["status"].toString())) {
        m_error = "This message can no longer be edited.";
        return false;
    }
    draft["id"] = id;
    return exec("INSERT INTO drafts(id,account_id,payload,updated_at) VALUES(?,?,?,?) ON "
                "CONFLICT(id) DO UPDATE SET "
                "payload=excluded.payload,account_id=excluded.account_id,updated_at="
                "excluded.updated_at WHERE drafts.status IN ('draft','held','scheduled')",
                {id, draft["account_id"].toString(),
                 QString::fromUtf8(QJsonDocument(draft).toJson(QJsonDocument::Compact)),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)});
}
bool Store::queueDraft(const QString &id, const QDateTime &when) {
    if (!when.isValid() || when <= QDateTime::currentDateTimeUtc())
        return false;
    if (one("SELECT id FROM drafts WHERE id=? AND status IN "
            "('draft','held','scheduled')",
            {id})
            .isEmpty())
        return false;
    return exec("UPDATE drafts SET status='scheduled',scheduled_at=?,error='' "
                "WHERE id=? AND status IN ('draft','held','scheduled')",
                {when.toUTC().toString(Qt::ISODateWithMs), id});
}
QJsonArray Store::takeDue(const QDateTime &now, const QDateTime &previousTick, bool available) {
    QJsonArray send;
    if (!exec("BEGIN IMMEDIATE"))
        return send;
    const auto rows = query("SELECT * FROM drafts WHERE status='scheduled' AND scheduled_at<=?",
                            {now.toUTC().toString(Qt::ISODateWithMs)});
    const bool continuous = previousTick.isValid() && previousTick.msecsTo(now) >= 0 &&
                            previousTick.msecsTo(now) <= 10000;
    for (const auto &value : rows) {
        const auto row = value.toObject();
        const auto due = QDateTime::fromString(row["scheduled_at"].toString(), Qt::ISODateWithMs);
        // An unavailable interval is a missed appointment, even if it was brief.
        const bool onTime = available && continuous && due > previousTick;
        if (!exec("UPDATE drafts SET status=?,error=? WHERE id=? AND "
                  "status='scheduled'",
                  {onTime ? "sending" : "held",
                   onTime ? "" : "Scheduled time was missed. Review before sending.",
                   row["id"].toString()})) {
            m_db.rollback();
            return {};
        }
        if (onTime)
            send.append(row);
    }
    if (!m_db.commit())
        return {};
    return send;
}
bool Store::finishSend(const QString &id, bool accepted, const QString &error) {
    return exec("UPDATE drafts SET status=?,error=? WHERE id=?",
                {accepted ? "sent" : "held", error, id});
}
void Store::recoverInterrupted() {
    for (const auto &value : query("SELECT id FROM drafts WHERE status='sending'")) {
        const QString id = value.toObject()["id"].toString();
        QLockFile lock(
            m_directory + "/send-" +
            QString::fromLatin1(
                QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) +
            ".lock");
        if (lock.tryLock(0))
            exec("UPDATE drafts SET status='held',error='Delivery was interrupted. "
                 "Check Sent before sending again.' WHERE status='sending' AND id=?",
                 {id});
    }
}
void Store::seedDemo() {
    if (!accounts().isEmpty())
        return;
    saveAccount({{"id", "demo"},
                 {"provider", "demo"},
                 {"email", "alex@example.test"},
                 {"profile_id", "personal"},
                 {"name", "Alex"}});
    const auto now = QDateTime::currentDateTimeUtc();
    const QStringList subjects{"A few details before Friday", "Your workspace is ready",
                               "Notes from the design review", "Your monthly reading list",
                               "Weekend plans"};
    const QStringList senders{"Morgan Lee", "Studio North", "Sam Rivera", "The Weekly", "Casey"};
    const QStringList bodies{
        "Hi Alex,\n\nCould you confirm the revised outline before Friday? We "
        "need your approval before sharing it with the team.\n\nThe proposed "
        "timeline is in the notes below. I’ve kept the launch on Tuesday and "
        "left Thursday for a final review.\n\nThanks,\nMorgan",
        "Welcome back, Alex.\n\nYour shared workspace is ready. Everything from "
        "last week’s session is in place.\n\nHave a good week,\nStudio North",
        "Hi Alex,\n\nWe agreed to simplify navigation and keep the main actions "
        "close to the content. The next review is Tuesday at 10.\n\nSam",
        "This month: thoughtful tools, quieter inboxes, and a few good books. "
        "Enjoy your reading.",
        "Would Saturday morning work for coffee? Let me know what time suits "
        "you.\n\nCasey"};
    for (int i = 0; i < subjects.size(); ++i) {
        const QString id = "demo:" + QString::number(i);
        upsertMessage({{"id", id},
                       {"remote_id", QString::number(i)},
                       {"account_id", "demo"},
                       {"thread_id", QString::number(i)},
                       {"subject", subjects[i]},
                       {"sender", senders[i]},
                       {"sender_email", "sender@example.test"},
                       {"recipients", "alex@example.test"},
                       {"body", bodies[i]},
                       {"received", now.addSecs(-i * 4200).toString(Qt::ISODateWithMs)},
                       {"folder", "inbox"},
                       {"unread", i < 2 ? 1 : 0},
                       {"attachments", "[]"}});
        applyClassification(id, {{"action_needed", i == 0 || i == 4},
                                 {"important", i == 2},
                                 {"reason", i == 0   ? "Approval needed before Friday"
                                            : i == 2 ? "Decisions from your design review"
                                            : i == 4 ? "Reply requested"
                                                     : ""},
                                 {"topic", i == 3 ? "Newsletters" : ""}});
    }
    exec("INSERT INTO calendars(id,remote_id,account_id,name,color) "
         "VALUES('demo:calendar','primary','demo','Personal','#516dff')");
    const auto date = QDateTime(QDate::currentDate().addDays(1), QTime(10, 0)).toUTC();
    upsertEvent({{"id", "demo:event"},
                 {"remote_id", "event"},
                 {"calendar_id", "demo:calendar"},
                 {"title", "Design review"},
                 {"start", date.toString(Qt::ISODateWithMs)},
                 {"end", date.addSecs(3600).toString(Qt::ISODateWithMs)},
                 {"location", "Studio"},
                 {"attendees", "[]"}});
}
