#pragma once
#include "backend.h"
#include <QCoreApplication>
#include <QProcess>
#include <QWindow>

// Session-bus entry point that brings the one Omail window forward, optionally on a
// conversation. Notification clicks and repeated `omail open` calls arrive here.
class WindowService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "app.omail.Omail")
  public:
    WindowService(Backend *backend, QObject *parent = nullptr)
        : QObject(parent), m_backend(backend) {}
    void setWindow(QWindow *window) { m_window = window; }
  public slots:
    Q_SCRIPTABLE void Show(const QString &account, const QString &thread) {
        if (!account.isEmpty() && !thread.isEmpty())
            m_backend->openThread(account, thread);
        if (!m_window)
            return;
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
        // Hyprland ignores activation requests without a token from user input, and
        // notification servers do not always pass one along.
        if (qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE"))
            QProcess::startDetached(
                "hyprctl", {"dispatch", "focuswindow",
                            "pid:" + QString::number(QCoreApplication::applicationPid())});
    }

  private:
    Backend *m_backend;
    QWindow *m_window = nullptr;
};
