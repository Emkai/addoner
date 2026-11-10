// addoner: install World of Warcraft addons from zips in Downloads, plus your
// own addons from a local folder.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "manager.h"
#include "theme.h"
#include "translator.h"

#ifdef Q_OS_WIN
#include <cstdio>
#include <windows.h>
#endif

static const char *Version = "0.1.0";

int main(int argc, char *argv[]) {
    // Commands run without a display, so they work over ssh and from scripts.
    if (argc > 1 && Cli::isCommand(QString::fromLocal8Bit(argv[1]))) {
#ifdef Q_OS_WIN
        // A GUI-subsystem exe has no console of its own; borrow the terminal
        // it was started from so commands can print.
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
#endif
        QCoreApplication app(argc, argv);
        app.setApplicationName("addoner");
        app.setApplicationVersion(Version);
        AddonManager manager;
        return Cli::run(manager, app.arguments().mid(1));
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("addoner");
    app.setApplicationVersion(Version);

    // Matches the window to addoner.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("addoner");
    app.setWindowIcon(QIcon::fromTheme("addoner"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    AddonManager manager;
    Backend backend(&manager);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
