#ifndef LAUNCHER_LIVEJOIN_HPP
#define LAUNCHER_LIVEJOIN_HPP

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
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
#else
    return false;
#endif
}
}

#endif
