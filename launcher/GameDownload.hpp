#ifndef LAUNCHER_GAMEDOWNLOAD_HPP
#define LAUNCHER_GAMEDOWNLOAD_HPP

#include <QObject>
#include <QProcess>
#include <QStringList>

// Runs the bundled installer without blocking the launcher's UI thread.
class GameDownload : public QObject
{
    Q_OBJECT
public:
    explicit GameDownload(QObject* parent = nullptr);
    ~GameDownload() override;
    void Start();
    void Cancel();

signals:
    void Progress(const QString& text, qint64 done, qint64 total);
    void Ready(const QString& key, const QString& path);
    void Finished(bool succeeded, const QString& error);

private:
    void Next();
    void ReadOutput();
    void Complete(bool succeeded, const QString& error);

    QProcess mProcess;
    QStringList mQueue;
    QString mKey;
    QString mError;
    QByteArray mOutput;
    bool mReady = false;
    bool mCancelled = false;
    bool mFinished = false;
};

#endif
