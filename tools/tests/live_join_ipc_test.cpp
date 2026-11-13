#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <cstdio>

// Run with a launcher copied into an isolated test folder, with Qt DLLs beside it.
// The test peer holds the instance lock, so the child never starts a game or browser.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    const QString executable = QFileInfo(app.arguments()[1]).absoluteFilePath();
    const QString directory = QFileInfo(executable).absolutePath();
    QLockFile lock(directory + "/launcher-instance.lock");
    if (!lock.tryLock(0)) return 3;
    const QString name = "lcereborn-live-" + QString::fromLatin1(QCryptographicHash::hash(
        directory.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(name);
    if (!server.listen(name)) return 4;
    for (const QString& link : {QString(), QString("lcereborn://join/123")}) {
        QProcess child;
        QStringList arguments;
        if (!link.isEmpty()) arguments << "--live-join" << link;
        child.start(executable, arguments);
        if (!child.waitForStarted(5000) || !server.waitForNewConnection(5000)) return 5;
        QLocalSocket* socket = server.nextPendingConnection();
        if (!socket) return 6;
        if (!socket->canReadLine() && !socket->waitForReadyRead(5000)) return 7;
        const QByteArray message = socket->readLine();
        socket->disconnectFromServer();
        delete socket;
        if (!child.waitForFinished(5000) || child.exitCode() != 0) return 8;
        const QByteArray expected = (link.isEmpty() ? QByteArray("activate") : link.toUtf8()) + '\n';
        if (message != expected) return 9;
    }
    std::puts("Live launcher IPC passed: activation and valid browser join forwarded without a second instance.");
    return 0;
}
