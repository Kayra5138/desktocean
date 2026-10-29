#include "appinstance.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("desktocean-editor"));
    app.setApplicationDisplayName(QStringLiteral("Desktocean"));
    app.setDesktopFileName(QStringLiteral("desktocean-editor"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("draw-brush")));

    // One editor window: a second launch brings the first one forward. Test runs
    // (--screenshot) stay separate so they never drive the user's open editor.
    const bool testRun = app.arguments().contains(QStringLiteral("--screenshot"));
    // Shared fish files are passed on as absolute paths: the running editor has another working directory
    QStringList arguments = app.arguments().mid(1);
    for (QString &arg : arguments) {
        if (arg.endsWith(QLatin1String(".desktocean")) && !arg.startsWith(QLatin1Char('-')) && QFileInfo::exists(arg))
            arg = QFileInfo(arg).absoluteFilePath();
    }
    if (!testRun && AppInstance::forwardToRunning(arguments))
        return 0;
    AppInstance::instance(!testRun);

    // Match the Plasma look unless the user asked for something else
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Desktocean.Editor", "Main");
    return app.exec();
}
