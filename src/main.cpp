#include "backend.h"
#include "cli.h"
#include "maildocument.h"
#include "theme.h"
#include "translator.h"
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <sys/stat.h>

int main(int argc, char **argv) {
    umask(0077);
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    const bool demo = arguments.contains("--demo"), capture = arguments.contains("--screenshot");
    const bool service = !arguments.isEmpty() && arguments.first() == "service";
    const bool gui = arguments.isEmpty() || arguments.first() == "open" || demo || capture;
    if (!gui || capture) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QT_QPA_PLATFORMTHEME", "generic");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("omail");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("Omail");
    app.setDesktopFileName("omail");
    if (!gui && !service)
        return runCli(arguments);
    auto take = [&arguments](const QString &key, const QString &fallback = QString()) {
        const int i = arguments.indexOf(key);
        return i >= 0 && i + 1 < arguments.size() ? arguments[i + 1] : fallback;
    };
    QTemporaryDir demoDirectory;
    const QString data = demo ? demoDirectory.path() + "/data" : Config::dataDirectory();
    const QString config = demo ? demoDirectory.path() + "/config" : Config::configDirectory();
    Backend backend(data, config, demo);
    if (service) {
        if (!backend.startService())
            return 0;
        return app.exec();
    }
    if (!demo && !capture) {
        // The daemon owns sync and scheduling independently of window lifetime.
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {"service"});
    }
    QQuickStyle::setStyle("Material");
    Theme theme(take("--theme-dir"));
    Translator translator(take("--language-file"));
    app.installTranslator(&translator);
    qmlRegisterType<MailDocument>("Omail", 1, 0, "MailDocument");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("initialWidth", take("--width", "1360").toInt());
    engine.rootContext()->setContextProperty("initialHeight", take("--height", "860").toInt());
    QObject::connect(&translator, &Translator::changed, &engine,
                     &QQmlApplicationEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &backend, &Backend::statusChanged);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    if (!take("--tab").isEmpty())
        engine.rootObjects().first()->setProperty("tab", take("--tab"));
    if (capture) {
        const QString path = take("--screenshot");
        QTimer::singleShot(1200, &app, [&app, &engine, path] {
            auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window || !window->grabWindow().save(path)) {
                QTextStream(stderr) << "Cannot save screenshot.\n";
                app.exit(1);
            } else
                app.quit();
        });
    }
    return app.exec();
}
