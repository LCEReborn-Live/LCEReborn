#include "GameDownload.hpp"
#include "LauncherPaths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

GameDownload::GameDownload(QObject* parent) : QObject(parent)
{
    connect(&mProcess, &QProcess::readyReadStandardOutput, this, &GameDownload::ReadOutput);
    connect(&mProcess, &QProcess::readyReadStandardError, this, [this] {
        // Node startup errors are useful, but keep diagnostics bounded.
        const QByteArray bytes = mProcess.readAllStandardError();
        if (mError.isEmpty()) mError = QString::fromUtf8(bytes.left(2048)).trimmed();
    });
    connect(&mProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            Complete(false, "The game downloader could not start: " + mProcess.errorString());
    });
    connect(&mProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
        ReadOutput();
        if (mCancelled) Complete(false, "Cancelled.");
        else if (status != QProcess::NormalExit || code != 0 || !mReady)
            Complete(false, mError.isEmpty() ? "The download stopped unexpectedly. Try again to resume." : mError);
        else QTimer::singleShot(0, this, &GameDownload::Next);
    });
}

GameDownload::~GameDownload()
{
    disconnect(&mProcess, nullptr, this, nullptr);
    if (mProcess.state() != QProcess::NotRunning)
    {
        mProcess.write("cancel\n");
        if (!mProcess.waitForFinished(3000))
        {
            mProcess.kill();
            mProcess.waitForFinished(1000);
        }
    }
}

void GameDownload::Start()
{
    mQueue = {"base", "update"};
    QTimer::singleShot(0, this, &GameDownload::Next);
}

void GameDownload::Cancel()
{
    mCancelled = true;
    if (mProcess.state() == QProcess::NotRunning)
        Complete(false, "Cancelled.");
    else
    {
        mProcess.write("cancel\n");
        QTimer::singleShot(5000, this, [this] {
            if (!mFinished && mProcess.state() != QProcess::NotRunning) mProcess.kill();
        });
    }
}

void GameDownload::Next()
{
    if (mFinished) return;
    if (mCancelled) { Complete(false, "Cancelled."); return; }
    if (mQueue.isEmpty()) { Complete(true, QString()); return; }
    mKey = mQueue.takeFirst();
    mReady = false;
    mError.clear();
    mOutput.clear();
    const QString install = LauncherPaths::InstallDir();
#if defined(Q_OS_WIN)
    const QString node = install + "/live/node.exe";
#else
    const QString node = install + "/live/node";
#endif
    const QString script = install + "/live/game-files.js";
    const QString manifest = LauncherPaths::ResourcesDir() + "/game-downloads.json";
    if (!QFileInfo(node).isFile() || !QFileInfo(script).isFile() || !QFileInfo(manifest).isFile())
    {
        Complete(false, "Game download files are missing. Reinstall the launcher or import local files.");
        return;
    }
    emit Progress(mKey == "base" ? "Connecting to the game download..." : "Connecting to Update 688...", 0, 0);
    mProcess.setProgram(node);
    mProcess.setArguments({script, "--manifest", manifest, "--key", mKey,
                           "--cache", LauncherPaths::DataDir() + "/downloads/game-files"});
    mProcess.start();
}

void GameDownload::ReadOutput()
{
    mOutput += mProcess.readAllStandardOutput();
    if (mOutput.size() > 1024 * 1024)
    {
        mError = "The downloader returned an invalid response.";
        mProcess.kill();
        return;
    }
    for (qsizetype end; (end = mOutput.indexOf('\n')) >= 0;)
    {
        const QByteArray line = mOutput.left(end);
        mOutput.remove(0, end + 1);
        const QJsonObject data = QJsonDocument::fromJson(line).object();
        const QString type = data.value("type").toString();
        if (type == "ready")
        {
            const QString root = data.value("path").toString();
            const QString cache = QDir(LauncherPaths::DataDir() + "/downloads/game-files").canonicalPath();
            const QString canonical = QFileInfo(root).canonicalFilePath();
            if (!cache.isEmpty() && canonical.startsWith(cache + "/") && QFileInfo(canonical).isDir())
            {
                mReady = true;
                emit Ready(mKey, canonical);
            }
            else mError = "The downloader returned an invalid game folder.";
        }
        else if (type == "error") mError = data.value("message").toString().left(2048);
        else if (type == "progress")
        {
            const QString phase = data.value("phase").toString();
            const QString label = mKey == "base" ? "Base game" : "Update 688";
            const qint64 done = data.value("done").toInteger();
            const qint64 total = data.value("total").toInteger();
            const QString action = phase == "verify" ? "Verifying" : phase == "extract" ? "Unpacking" : "Downloading";
            emit Progress(QString("%1 %2: %3 / %4 GB").arg(action, label)
                              .arg(done / 1e9, 0, 'f', 2).arg(total / 1e9, 0, 'f', 2), done, total);
        }
        else if (type == "info") emit Progress(data.value("message").toString().left(2048), 0, 0);
    }
}

void GameDownload::Complete(bool succeeded, const QString& error)
{
    if (mFinished) return;
    mFinished = true;
    emit Finished(succeeded, error);
}
