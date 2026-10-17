#include "config.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

Config::Config(const QString &directory, QObject *parent)
    : QObject(parent), m_path(directory + "/settings.json") {
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Config::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Config::reload);
    reload();
}

QString Config::dataDirectory() {
    const auto override = qEnvironmentVariable("OMAIL_DATA_DIR");
    return override.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/omail"
               : override;
}
QString Config::configDirectory() {
    const auto override = qEnvironmentVariable("OMAIL_CONFIG_DIR");
    return override.isEmpty()
               ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omail"
               : override;
}
QJsonObject Config::defaults() {
    return {{"fontSize", 14},
            {"fontFamily", ""},
            {"density", "comfortable"},
            {"sidebarWidth", 224},
            {"listWidth", 360},
            {"cacheDays", 90},
            {"syncMinutes", 5},
            {"ollamaUrl", "http://127.0.0.1:11434"},
            {"ollamaModel", "qwen3.5:4b"},
            {"aiEnabled", true},
            {"microsoftClientId", ""},
            {"googleClientId", ""},
            {"shortcutCompose", "Ctrl+N"},
            {"shortcutSearch", "Ctrl+K"},
            {"shortcutSync", "Ctrl+R"},
            {"shortcutMail", "Alt+1"},
            {"shortcutCalendar", "Alt+2"},
            {"shortcutAi", "Alt+3"},
            {"notifications", true},
            {"viewOrder", "inbox,priority,flagged,drafts,sent,archive,junk,trash,all"},
            {"showTopics", true},
            {"calendarView", "week"}};
}
QJsonObject Config::schema() {
    QJsonObject result;
    const auto d = defaults();
    for (auto it = d.begin(); it != d.end(); ++it)
        result[it.key()] = QJsonObject{{"type", it.value().isBool()     ? "boolean"
                                                : it.value().isDouble() ? "integer"
                                                                        : "string"},
                                       {"default", it.value()}};
    return result;
}
void Config::reload() {
    auto values = defaults();
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (validate(it.key(), it.value()))
                values[it.key()] = it.value();
    }
    const auto old = m_watcher.files() + m_watcher.directories();
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    const QString directory = QFileInfo(m_path).absolutePath();
    QString ancestor = directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    m_watcher.addPath(ancestor);
    if (QFileInfo::exists(m_path))
        m_watcher.addPath(m_path);
    if (values != m_values) {
        m_values = values;
        emit changed();
    }
}
bool Config::validate(const QString &key, const QJsonValue &value, QString *error) {
    const auto base = defaults();
    auto fail = [&](const QString &s) {
        if (error)
            *error = s;
        return false;
    };
    if (!base.contains(key))
        return fail(tr("Unknown setting: %1").arg(key));
    if (base[key].type() != value.type())
        return fail(tr("Wrong type for setting: %1").arg(key));
    const QHash<QString, QPair<int, int>> ranges{{"fontSize", {11, 28}},
                                                 {"sidebarWidth", {180, 400}},
                                                 {"listWidth", {280, 700}},
                                                 {"cacheDays", {1, 3650}},
                                                 {"syncMinutes", {1, 60}}};
    if (ranges.contains(key) &&
        (value.toDouble() != value.toInt() || value.toInt() < ranges[key].first ||
         value.toInt() > ranges[key].second))
        return fail(tr("Setting %1 is outside its supported range.").arg(key));
    if (key == "density" && value != "comfortable" && value != "compact")
        return fail(tr("Choose comfortable or compact density."));
    if (key == "calendarView" && value != "week" && value != "month" && value != "agenda")
        return fail(tr("Choose week, month, or agenda."));
    if (key == "viewOrder") {
        const QStringList allowed{"inbox",   "priority", "flagged", "drafts", "sent",
                                  "archive", "junk",     "trash",   "all"};
        auto views = value.toString().split(',', Qt::SkipEmptyParts);
        const int size = views.size();
        views.removeDuplicates();
        if (size == 0 || size != views.size())
            return fail(tr("Choose a nonempty list of unique views."));
        for (const auto &view : views)
            if (!allowed.contains(view))
                return fail(tr("Unknown view: %1").arg(view));
    }
    if (key == "ollamaUrl") {
        const QUrl url(value.toString());
        if (url.scheme() != "http" ||
            !QStringList{"localhost", "127.0.0.1", "::1"}.contains(url.host()) ||
            !url.userInfo().isEmpty() || !url.query().isEmpty())
            return fail(tr("Omail v1 uses a local Ollama endpoint only."));
    }
    if (value.isString() && value.toString().size() > 512)
        return fail(tr("Setting value is too long."));
    if (key == "ollamaModel" && (value.toString().contains("cloud", Qt::CaseInsensitive) ||
                                 value.toString().trimmed().isEmpty()))
        return fail(tr("Choose an installed local model."));
    return true;
}
bool Config::set(const QString &key, const QJsonValue &value, QString *error) {
    if (!validate(key, value, error))
        return false;
    auto fail = [&](const QString &s) {
        if (error)
            *error = s;
        return false;
    };
    const QString directory = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(directory))
        return fail(tr("Cannot create the settings directory."));
    QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QLockFile lock(m_path + ".lock");
    if (!lock.tryLock(1000))
        return fail(tr("Settings are being edited by another process."));
    reload();
    auto next = m_values;
    next[key] = value;
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray bytes = QJsonDocument(next).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(file.errorString());
    reload();
    return true;
}
