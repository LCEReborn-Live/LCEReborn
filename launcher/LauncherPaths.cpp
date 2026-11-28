#include "LauncherPaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace LauncherPaths
{
QString InstallDir()
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

QString DataDir()
{
#if defined(Q_OS_WIN)
    return InstallDir();
#else
    // The Linux install folder can be read-only (an AppImage), so the data lives in the user's data folder.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/LCEReborn").absolutePath();
#endif
}

QString AssetsDir()
{
    const QString bundled = InstallDir() + "/assets";
    if (QFileInfo(bundled + "/Common/Media/MediaWiiU.arc").isFile())
        return bundled;
    return DataDir() + "/assets";
}

QString ResourcesDir()
{
    return InstallDir() + "/resources";
}

QString GameExecutable()
{
#if defined(Q_OS_WIN)
    return InstallDir() + "/LCEReborn.exe";
#else
    return InstallDir() + "/LCEReborn";
#endif
}

QString ProfilesFile()
{
    return DataDir() + "/launcher_profiles.json";
}

QString SettingsFile()
{
    return DataDir() + "/launcher.ini";
}

QString NewsCacheFile()
{
    return DataDir() + "/launcher_news.json";
}

QString PreparedManifest()
{
    return AssetsDir() + "/.prepared.json";
}

QString ContentCheckFile()
{
    return AssetsDir() + "/Common/Media/MediaWiiU.arc";
}

bool RunsFromAppImage()
{
    return qEnvironmentVariableIsSet("APPIMAGE");
}
}
