#include "appinstance.h"

#include <QLocalSocket>
#include <QQmlEngine>

namespace {
QString serverName()
{
    // Per user; QLocalServer puts it in the user's runtime directory
    return QStringLiteral("desktocean-editor-") + qEnvironmentVariable("USER", QStringLiteral("user"));
}
} // namespace

AppInstance::AppInstance(QObject *parent)
    : QObject(parent)
{
}

bool AppInstance::forwardToRunning(const QStringList &arguments)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(300))
        return false;
    socket.write(arguments.join(QLatin1Char('\n')).toUtf8());
    socket.flush();
    socket.waitForBytesWritten(300);
    socket.disconnectFromServer();
    return true;
}

AppInstance *AppInstance::instance(bool listen)
{
    static AppInstance *self = [listen] {
        auto *a = new AppInstance;
        if (listen)
            a->listen();
        return a;
    }();
    return self;
}

AppInstance *AppInstance::create(QQmlEngine *, QJSEngine *)
{
    AppInstance *a = instance();
    // Owned by us, not by the QML engine
    QQmlEngine::setObjectOwnership(a, QQmlEngine::CppOwnership);
    return a;
}

void AppInstance::listen()
{
    // A crashed editor can leave a stale socket behind
    QLocalServer::removeServer(serverName());
    m_server.listen(serverName());
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                const QStringList args = QString::fromUtf8(client->readAll()).split(QLatin1Char('\n'));
                Q_EMIT activated(args);
            });
        }
    });
}
