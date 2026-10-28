#include <QApplication>
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
