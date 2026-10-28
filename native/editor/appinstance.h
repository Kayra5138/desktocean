#pragma once

#include <QLocalServer>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

// Keeps the editor to one window. The first instance listens on a local
// socket; later launches hand their arguments over and quit, and the running
// window comes to the front instead.
class AppInstance : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    // True if another editor took over `arguments`; the caller should exit.
    static bool forwardToRunning(const QStringList &arguments);
    // The first call decides whether this process listens for later launches;
    // call it once from main() before loading QML.
    static AppInstance *instance(bool listen = true);

    static AppInstance *create(QQmlEngine *, QJSEngine *);

Q_SIGNALS:
    // Another launch asked for the window (with its command-line arguments)
    void activated(const QStringList &arguments);

private:
    explicit AppInstance(QObject *parent = nullptr);
    void listen();

    QLocalServer m_server;
};
