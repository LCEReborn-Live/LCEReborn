#ifndef LAUNCHER_GAMERUNNER_HPP
#define LAUNCHER_GAMERUNNER_HPP

#include <QObject>
#include <QProcess>
#include <QStringList>

#include "GameLog.hpp"
#include "Profiles.hpp"

class GameRunner : public QObject
{
    Q_OBJECT

public:
    explicit GameRunner(QObject* parent = nullptr);
    ~GameRunner() override;

    static QString CheckGameReady();
    static QStringList Arguments(const Profile& profile);

    bool Start(const Profile& profile, QString* error, const QString& liveDirectory = QString(),
               const QString& liveUserId = QString(), const QString& liveSkinPath = QString(),
               const QString& liveCapePath = QString());
    bool StartDetached(const Profile& profile, QString* error);
    bool IsRunning() const { return mProcess.state() != QProcess::NotRunning; }
    void Stop();

signals:
    void Output(const QVector<LogEntry>& entries);
    void Started();
    void Finished(int exitCode, bool crashed);

private:
    bool Prepare(const Profile& profile, QString* error);

    QProcess mProcess;
    LogParser mOutput;
    LogParser mErrors;
};

#endif
