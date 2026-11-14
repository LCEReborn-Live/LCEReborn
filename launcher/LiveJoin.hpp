#ifndef LAUNCHER_LIVEJOIN_HPP
#define LAUNCHER_LIVEJOIN_HPP

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace LiveJoin
{
// Links contain an account ID only. Credentials and connection addresses come from the API.
inline qint64 OwnerIdFromUrl(const QString& text)
{
    if (text.size() > 64) return 0;
    static const QRegularExpression pattern("\\Alcereborn://join/([1-9][0-9]{0,15})/?\\z",
                                            QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return 0;
    bool ok = false;
    const qint64 id = match.captured(1).toLongLong(&ok);
    return ok && id <= 9007199254740991LL ? id : 0;
}

inline bool RegisterBrowserHandler()
{
#ifdef Q_OS_WIN
    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    QSettings settings("HKEY_CURRENT_USER\\Software\\Classes\\lcereborn", QSettings::NativeFormat);
    settings.setValue("Default", "URL:LCEReborn Live");
    settings.setValue("URL Protocol", "");
    settings.setValue("DefaultIcon/Default", "\"" + executable + "\",0");
    settings.setValue("shell/open/command/Default", "\"" + executable + "\" --live-join \"%1\"");
    settings.sync();
    return settings.status() == QSettings::NoError;
#elif defined(Q_OS_LINUX)
    QString executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty())
        executable = QCoreApplication::applicationFilePath();
    if (executable.isEmpty())
        return false;

    // Desktop Exec quoting rules differ from shell quoting. Escape reserved
    // characters while keeping the URL field code as a separate argument.
    executable.replace("\\", "\\\\");
    executable.replace("\"", "\\\"");
    executable.replace("`", "\\`");
    executable.replace("$", "\\$");
    executable.replace("%", "%%");

    const QString applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                 + "/applications";
    if (!QDir().mkpath(applications))
        return false;
    const QString desktopName = "lcereborn-live.desktop";
    QSaveFile desktop(QDir(applications).filePath(desktopName));
    if (!desktop.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray entry = ("[Desktop Entry]\nType=Application\nName=LCEReborn Live\nExec=\"" + executable
                              + "\" --live-join %u\nTryExec=\"" + executable
                              + "\"\nNoDisplay=true\nTerminal=false\nMimeType=x-scheme-handler/lcereborn;\n").toUtf8();
    if (desktop.write(entry) != entry.size() || !desktop.commit())
        return false;

    return QProcess::execute("xdg-mime", {"default", desktopName, "x-scheme-handler/lcereborn"}) == 0;
#else
    return false;
#endif
}
}

#endif
