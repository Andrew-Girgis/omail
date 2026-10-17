#include "portalfilepicker.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QUuid>

void PortalFilePicker::open(const QString &title, bool multiple, Picked done) {
    request("OpenFile", title, {{"multiple", multiple}}, done);
}
void PortalFilePicker::save(const QString &name, Picked done) {
    request("SaveFile", tr("Save attachment"), {{"current_name", name}}, done);
}
void PortalFilePicker::request(const QString &method, const QString &title, QVariantMap options,
                               Picked done) {
    if (m_done) {
        done({}, tr("A file chooser is already open."));
        return;
    }
    const QString token = "omail" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    m_path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    options["handle_token"] = token;
    options["modal"] = true;
    m_done = done;
    // Subscribe before opening the portal because a fast response can precede the
    // method reply.
    auto bus = QDBusConnection::sessionBus();
    bus.connect("org.freedesktop.portal.Desktop", m_path, "org.freedesktop.portal.Request",
                "Response", this, SLOT(response(uint, QVariantMap)));
    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop",
                                               "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.FileChooser", method);
    call << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError() && m_done) {
            auto done = std::move(m_done);
            m_done = {};
            done({}, reply.error().message());
        }
    });
}
void PortalFilePicker::response(uint code, const QVariantMap &results) {
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", m_path,
                                             "org.freedesktop.portal.Request", "Response", this,
                                             SLOT(response(uint, QVariantMap)));
    if (!m_done)
        return;
    auto done = std::move(m_done);
    m_done = {};
    QStringList files;
    if (code == 0) {
        const QStringList uris = qdbus_cast<QStringList>(results.value("uris"));
        for (const auto &uri : uris) {
            const QUrl url(uri);
            if (url.isLocalFile())
                files << url.toLocalFile();
        }
        if (files.isEmpty()) {
            done({}, tr("Choose a local file."));
            return;
        }
    }
    done(files, {});
}
