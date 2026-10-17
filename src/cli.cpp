#include "cli.h"
#include "config.h"
#include "store.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

int runCli(QStringList args) {
    const bool compact = args.removeAll("--json") > 0;
    auto output = [compact](const QJsonValue &v) {
        QTextStream(stdout) << (v.isArray() ? QJsonDocument(v.toArray())
                                            : QJsonDocument(v.toObject()))
                                   .toJson(compact ? QJsonDocument::Compact
                                                   : QJsonDocument::Indented)
                            << '\n';
        return 0;
    };
    auto error = [](const QString &e) {
        QTextStream(stderr)
            << QJsonDocument(QJsonObject{{"error", e}}).toJson(QJsonDocument::Compact) << '\n';
        return 1;
    };
    auto option = [&args](const QString &key, const QString &fallback = QString()) {
        const int i = args.indexOf(key);
        if (i < 0)
            return fallback;
        if (i + 1 >= args.size())
            return QString();
        const auto v = args.takeAt(i + 1);
        args.removeAt(i);
        return v;
    };
    const QString profile = option("--profile", "personal"), folder = option("--view", "inbox"),
                  query = option("--query"), account = option("--account"), from = option("--from"),
                  to = option("--to"), path = option("--path");
    bool validLimit = false;
    const int limit = option("--limit", "100").toInt(&validLimit);
    if (!validLimit || limit < 1 || limit > 2000)
        return error("--limit must be between 1 and 2000.");
    if (args.isEmpty() || args == QStringList{"help"} || args == QStringList{"--help"})
        return output(QJsonObject{
            {"app", "Omail"},
            {"version", "0.1.0"},
            {"mail_and_calendar", "read-only"},
            {"commands",
             QJsonArray{"omail open", "omail status --json", "omail profiles list --json",
                        "omail accounts list --profile personal --json",
                        "omail mail list --profile personal --view priority --json",
                        "omail mail search --query words --profile personal --json",
                        "omail mail get MESSAGE_ID --json",
                        "omail mail thread THREAD_ID --account ACCOUNT_ID --json",
                        "omail calendar list --profile personal --json",
                        "omail calendar events --profile personal --from ISO_DATE --to "
                        "ISO_DATE --json",
                        "omail calendar get EVENT_ID --json", "omail config get [KEY] --json",
                        "omail config set KEY VALUE --json", "omail config schema --json",
                        "omail skill install [--path DIRECTORY] --json", "omail service"}},
            {"docs", "/usr/share/doc/omail"},
            {"settings", Config::configDirectory() + "/settings.json"},
            {"data", Config::dataDirectory() + "/omail.sqlite3"}});
    if (args.first() == "config") {
        Config config(Config::configDirectory());
        if (args.size() == 2 && args[1] == "schema")
            return output(config.schema());
        if (args.size() >= 2 && args[1] == "get" && args.size() <= 3) {
            if (args.size() == 2)
                return output(config.values());
            if (!config.values().contains(args[2]))
                return error("Unknown setting.");
            return output(QJsonObject{{args[2], config.get(args[2])}});
        }
        if (args.size() == 4 && args[1] == "set") {
            QJsonValue value = args[3];
            const auto defaults = Config::defaults();
            if (defaults[args[2]].isBool()) {
                if (args[3] != "true" && args[3] != "false")
                    return error("Use true or false.");
                value = args[3] == "true";
            } else if (defaults[args[2]].isDouble()) {
                bool ok = false;
                const int number = args[3].toInt(&ok);
                if (!ok)
                    return error("Use a whole number.");
                value = number;
            }
            QString reason;
            if (!config.set(args[2], value, &reason))
                return error(reason);
            return output(QJsonObject{{"ok", true}, {"key", args[2]}, {"value", value}});
        }
        return error("Use config get [KEY], config set KEY VALUE, or config schema.");
    }
    if (args == QStringList{"skill", "install"}) {
        const QString directory = path.isEmpty() ? QDir::homePath() + "/.codex/skills/omail" : path;
        if (!QDir().mkpath(directory))
            return error("Cannot create the skill directory.");
        QFile source(":/agent/SKILL.md");
        QSaveFile target(directory + "/SKILL.md");
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
            return error("Cannot install the skill.");
        const auto bytes = source.readAll();
        if (target.write(bytes) != bytes.size() || !target.commit())
            return error(target.errorString());
        return output(QJsonObject{{"ok", true}, {"path", directory + "/SKILL.md"}});
    }
    if (args == QStringList{"status"} &&
        !QFileInfo::exists(Config::dataDirectory() + "/omail.sqlite3"))
        return output(QJsonObject{{"version", "0.1.0"}, {"accounts", 0}, {"setup_required", true}});
    Store store(Config::dataDirectory(), true);
    if (!store.good())
        return error("Open Omail once to initialize its local store. " + store.error());
    if (args == QStringList{"status"})
        return output(QJsonObject{
            {"version", "0.1.0"},
            {"accounts", store.accounts().size()},
            {"profiles", store.profiles().size()},
            {"mail_and_calendar_cli", "read-only"},
            {"scheduled",
             store.one("SELECT COUNT(*) AS n FROM drafts WHERE status='scheduled'")["n"]},
            {"held", store.one("SELECT COUNT(*) AS n FROM drafts WHERE status='held'")["n"]},
            {"ai_error", store.one("SELECT value FROM service_state WHERE key='ai_error'")["value"]
                             .toString()}});
    if (args == QStringList{"profiles", "list"})
        return output(store.profiles());
    if (args == QStringList{"accounts", "list"})
        return output(store.accounts(profile));
    if (args == QStringList{"mail", "list"})
        return output(store.conversations(profile, folder, query, limit));
    if (args == QStringList{"mail", "search"})
        return output(store.conversations(profile, "all", query, limit));
    if (args.size() == 3 && args[0] == "mail" && args[1] == "get") {
        const auto message = store.one("SELECT * FROM messages WHERE id=?", {args[2]});
        if (message.isEmpty())
            return error("Message not found.");
        return output(message);
    }
    if (args.size() == 3 && args[0] == "mail" && args[1] == "thread") {
        if (account.isEmpty())
            return error("--account is required.");
        return output(store.thread(account, args[2]));
    }
    if (args == QStringList{"calendar", "list"})
        return output(store.query("SELECT c.*,a.email FROM calendars c JOIN accounts a ON "
                                  "a.id=c.account_id" +
                                      QString(profile == "all" ? "" : " WHERE a.profile_id=?"),
                                  profile == "all" ? QVariantList{} : QVariantList{profile}));
    if (args == QStringList{"calendar", "events"}) {
        auto parse = [](const QString &s, QDateTime fallback) {
            if (s.isEmpty())
                return fallback;
            auto d = QDateTime::fromString(s, Qt::ISODate);
            if (!d.isValid())
                d = QDateTime(QDate::fromString(s, Qt::ISODate), QTime(0, 0));
            return d;
        };
        const auto start = parse(from, QDateTime::currentDateTimeUtc()),
                   end = parse(to, start.addDays(30));
        if (!start.isValid() || !end.isValid() || end <= start)
            return error("Use valid ISO dates with --to after --from.");
        return output(store.events(profile, start.toUTC().toString(Qt::ISODateWithMs),
                                   end.toUTC().toString(Qt::ISODateWithMs)));
    }
    if (args.size() == 3 && args[0] == "calendar" && args[1] == "get") {
        const auto event = store.one("SELECT * FROM events WHERE id=?", {args[2]});
        if (event.isEmpty())
            return error("Event not found.");
        return output(event);
    }
    return error("Unknown command. Mail and calendar commands are read-only in "
                 "v1. Run omail help.");
}
