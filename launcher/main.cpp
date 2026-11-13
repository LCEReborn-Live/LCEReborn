#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>
#include <QThread>

#include "BuildVersion.hpp"
#include "LauncherPaths.hpp"
#include "LiveJoin.hpp"
#include "MainWindow.hpp"
#include "Profiles.hpp"
#include "Theme.hpp"

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("LCEReborn Launcher");
    QApplication::setApplicationVersion(BuildVersion());
    QApplication::setWindowIcon(QIcon(":/launcher/icon.png"));
    ApplyTheme(application);

    QString error;
    if (!QDir().mkpath(LauncherPaths::DataDir()))
    {
        QMessageBox::critical(nullptr, "LCEReborn Launcher",
                              QString("The launcher's folder %1 could not be created.")
                                  .arg(QDir::toNativeSeparators(LauncherPaths::DataDir())));
        return 1;
    }
    const QStringList arguments = application.arguments();
    QString joinLink;
    if (arguments.size() > 1)
    {
        if (arguments.size() == 3 && arguments[1] == "--live-join") joinLink = arguments[2];
        else if (arguments.size() == 2) joinLink = arguments[1];
        if (LiveJoin::OwnerIdFromUrl(joinLink) == 0)
        {
            QMessageBox::warning(nullptr, "LCE Live", "This Live join link is invalid.");
            return 2;
        }
    }

    const QString serverName = "lcereborn-live-" + QString::fromLatin1(QCryptographicHash::hash(
        LauncherPaths::DataDir().toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    QLockFile instanceLock(LauncherPaths::DataDir() + "/launcher-instance.lock");
    if (!instanceLock.tryLock(0))
    {
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            QLocalSocket existing;
            existing.connectToServer(serverName);
            if (existing.waitForConnected(500))
            {
                const QByteArray message = (joinLink.isEmpty() ? QByteArray("activate") : joinLink.toUtf8()) + '\n';
                existing.write(message);
                if (existing.bytesToWrite() == 0 || existing.waitForBytesWritten(2000)) return 0;
            }
            QThread::msleep(100);
        }
        QMessageBox::information(nullptr, "LCE Live", "The launcher is already opening. Try the join link again in a moment.");
        return 1;
    }
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QLocalServer::removeServer(serverName); // Only the process holding the per-user lock may remove a stale socket.
    if (!server.listen(serverName))
    {
        QMessageBox::warning(nullptr, "LCE Live", "Could not start the launcher's local join-link listener.");
        return 1;
    }

    ProfileStore store(LauncherPaths::ProfilesFile());
    if (!store.Load(&error))
    {
        QMessageBox::critical(nullptr, "LCEReborn Launcher",
                              error + "\n\nFix or remove the file and start the launcher again.");
        return 1;
    }
    if (!store.Save(&error))
        QMessageBox::warning(nullptr, "LCEReborn Launcher", error);

    MainWindow window(store);
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        while (QLocalSocket* socket = server.nextPendingConnection())
        {
            socket->setReadBufferSize(256);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(3000, socket, [socket] { socket->disconnectFromServer(); });
            auto receive = [socket, &window] {
                if (socket->property("handled").toBool()) return;
                if (!socket->canReadLine()) { if (socket->bytesAvailable() > 128) socket->disconnectFromServer(); return; }
                socket->setProperty("handled", true);
                const QByteArray message = socket->readLine(129);
                const QString link = QString::fromUtf8(message.left(message.size() - 1));
                const qint64 ownerId = LiveJoin::OwnerIdFromUrl(link);
                if (message.endsWith('\n') && (ownerId > 0 || link == "activate"))
                {
                    window.showNormal(); window.raise(); window.activateWindow();
                    if (ownerId > 0) window.JoinLiveWorld(ownerId);
                }
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, &window, receive);
            QTimer::singleShot(0, socket, receive);
        }
    });
    window.show();
    if (!joinLink.isEmpty()) QTimer::singleShot(0, &window, [&window, joinLink] {
        window.JoinLiveWorld(LiveJoin::OwnerIdFromUrl(joinLink));
    });
    return application.exec();
}
