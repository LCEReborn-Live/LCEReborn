#include "GameFiles.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>

#include "LauncherPaths.hpp"
#include "platform/BuiltInPacks.hpp"

namespace
{
const char* const kTitleId = "00050000101D9D00";
const int kUpdateVersion = 688;
const char* const kContentFolders[] = {"Common", "music", "WiiU"};
const qint64 kCopyBlock = 1 << 20;
const qint64 kSpaceMargin = 256ll << 20;

QString Gigabytes(qint64 bytes)
{
    return QLocale().toString(static_cast<double>(bytes) / 1e9, 'f', 2) + " GB";
}

QMap<QString, QString> ContentFiles(const QString& dump)
{
    QMap<QString, QString> files;
    const QDir content(dump + "/content");
    for (const char* folder : kContentFolders)
    {
        const QString top = content.filePath(folder);
        if (!QFileInfo(top).isDir())
            continue;
        QDirIterator it(top, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString full = it.next();
            const QString relative = content.relativeFilePath(full);
            if (!IsOriginalCandyPackPath(relative.toStdString()))
                files.insert(relative, full);
        }
    }
    return files;
}
}

AssetPreparer::AssetPreparer(const QString& baseDump, const QString& updateDump, const QString& assetsDir)
    : mBaseDump(baseDump), mUpdateDump(updateDump), mAssetsDir(assetsDir), mCancelled(false)
{
}

bool AssetPreparer::CheckDump(const QString& path, bool isUpdate, int* version, QString* error)
{
    if (path.isEmpty() || !QFileInfo(path).isDir())
    {
        *error = "This folder doesn't exist.";
        return false;
    }
    for (const char* folder : {"code", "content", "meta"})
    {
        if (!QFileInfo(path + "/" + folder).isDir())
        {
            *error = QString("There's no %1 folder here. Choose the extracted title folder itself.").arg(folder);
            return false;
        }
    }
    QFile meta(path + "/meta/meta.xml");
    if (!meta.open(QIODevice::ReadOnly))
    {
        *error = "There's no meta/meta.xml here. Choose the extracted title folder itself.";
        return false;
    }
    const QString text = QString::fromUtf8(meta.readAll());
    const QRegularExpressionMatch titleId =
        QRegularExpression("<title_id[^>]*>\\s*([0-9A-Fa-f]{16})\\s*<").match(text);
    const QRegularExpressionMatch titleVersion = QRegularExpression("<title_version[^>]*>\\s*(\\d+)\\s*<").match(text);
    if (!titleId.hasMatch() || !titleVersion.hasMatch())
    {
        *error = "meta/meta.xml has no title ID or version.";
        return false;
    }
    if (titleId.captured(1).toUpper() != kTitleId)
    {
        *error = QString("This is title %1, not Minecraft: Wii U Edition.").arg(titleId.captured(1).toUpper());
        return false;
    }
    *version = titleVersion.captured(1).toInt();
    if (isUpdate && *version != kUpdateVersion)
    {
        *error = QString("This is version %1. The update must be version %2.").arg(*version).arg(kUpdateVersion);
        return false;
    }
    if (!isUpdate && *version == kUpdateVersion)
    {
        *error = QString("This is the update (version %1), not the base game.").arg(kUpdateVersion);
        return false;
    }
    if (!isUpdate)
    {
        QStringList missing;
        for (const char* folder : kContentFolders)
        {
            if (!QFileInfo(path + "/content/" + folder).isDir())
                missing.append(folder);
        }
        if (!missing.isEmpty())
        {
            *error = QString("content is missing %1.").arg(missing.join(", "));
            return false;
        }
    }
    return true;
}

void AssetPreparer::Run()
{
    QString error;
    const bool succeeded = Prepare(&error);
    if (!succeeded)
        QDir(mAssetsDir + ".partial").removeRecursively();
    emit Finished(succeeded, error);
}

bool AssetPreparer::Prepare(QString* error)
{
    QElapsedTimer clock;
    clock.start();

    int updateVersion = 0;
    int baseVersion = 0;
    if (!CheckDump(mUpdateDump, true, &updateVersion, error) || !CheckDump(mBaseDump, false, &baseVersion, error))
        return false;
    emit Message(LogLevel::Info, QString("Base game version %1 and update version %2 found.").arg(baseVersion).arg(
                                     updateVersion));

    QMap<QString, QString> merged = ContentFiles(mBaseDump);
    const qint64 baseCount = merged.size();
    const QMap<QString, QString> updateFiles = ContentFiles(mUpdateDump);
    qint64 overridden = 0;
    for (auto it = updateFiles.constBegin(); it != updateFiles.constEnd(); ++it)
    {
        if (merged.contains(it.key()))
            ++overridden;
        merged.insert(it.key(), it.value());
    }

    qint64 bytesTotal = 0;
    for (const QString& source : merged)
        bytesTotal += QFileInfo(source).size();
    const QStorageInfo storage(QFileInfo(mAssetsDir).absolutePath());
    if (storage.isValid() && storage.bytesAvailable() < bytesTotal + kSpaceMargin)
    {
        *error = QString("Not enough free space: the game files need %1 and %2 is free.")
                     .arg(Gigabytes(bytesTotal + kSpaceMargin), Gigabytes(storage.bytesAvailable()));
        return false;
    }
    emit Message(LogLevel::Info, QString("Copying %1 files (%2): the base game's content, then the update's over it.")
                                     .arg(merged.size())
                                     .arg(Gigabytes(bytesTotal)));

    const QString staging = mAssetsDir + ".partial";
    if (QFileInfo::exists(staging) && !QDir(staging).removeRecursively())
    {
        *error = QString("%1 could not be cleared.").arg(QDir::toNativeSeparators(staging));
        return false;
    }

    qint64 bytesDone = 0;
    qint64 filesDone = 0;
    const qint64 filesTotal = merged.size();
    emit Progress(0, filesTotal, 0, bytesTotal);
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it)
    {
        const QString target = staging + "/" + it.key();
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
        {
            *error = QString("%1 could not be created.").arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath()));
            return false;
        }
        if (!CopyFile(it.value(), target, &bytesDone, bytesTotal, filesDone, filesTotal, error))
            return false;
        ++filesDone;
    }
    emit Progress(filesTotal, filesTotal, bytesDone, bytesTotal);

    QJsonObject assets;
    assets.insert("files", filesTotal);
    assets.insert("bytes", bytesDone);
    assets.insert("from_base", baseCount - overridden);
    assets.insert("from_update", static_cast<qint64>(updateFiles.size()));
    assets.insert("update_overrides", overridden);
    assets.insert("seconds", qRound(clock.elapsed() / 100.0) / 10.0);
    QJsonObject manifest;
    manifest.insert("prepared_at", QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ssZ"));
    manifest.insert("prepared_by", "LCEReborn launcher");
    manifest.insert("title_id", kTitleId);
    manifest.insert("base_version", baseVersion);
    manifest.insert("update_version", updateVersion);
    manifest.insert("assets", assets);
    QSaveFile manifestFile(staging + "/.prepared.json");
    if (!manifestFile.open(QIODevice::WriteOnly) ||
        manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0 || !manifestFile.commit())
    {
        *error = QString("The manifest could not be written: %1").arg(manifestFile.errorString());
        return false;
    }

    if (QFileInfo::exists(mAssetsDir) && !QDir(mAssetsDir).removeRecursively())
    {
        *error = QString("The old game files in %1 could not be removed; close the game and try again.")
                     .arg(QDir::toNativeSeparators(mAssetsDir));
        return false;
    }
    if (!QDir().rename(staging, mAssetsDir))
    {
        *error = QString("%1 could not be renamed to %2.")
                     .arg(QDir::toNativeSeparators(staging), QDir::toNativeSeparators(mAssetsDir));
        return false;
    }
    emit Message(LogLevel::Info, QString("Game files ready: %1 files, %2 (%3 from the base game, %4 from the update, "
                                         "%5 of them replacing the base game's).")
                                     .arg(filesTotal)
                                     .arg(Gigabytes(bytesDone))
                                     .arg(baseCount - overridden)
                                     .arg(updateFiles.size())
                                     .arg(overridden));
    return true;
}

bool AssetPreparer::CopyFile(const QString& source, const QString& target, qint64* bytesDone, qint64 bytesTotal,
                             qint64 filesDone, qint64 filesTotal, QString* error)
{
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly))
    {
        *error = QString("%1 could not be read: %2").arg(QDir::toNativeSeparators(source), input.errorString());
        return false;
    }
    QFile output(target);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        *error = QString("%1 could not be written: %2").arg(QDir::toNativeSeparators(target), output.errorString());
        return false;
    }
    QByteArray block;
    for (;;)
    {
        if (mCancelled)
        {
            *error = "Cancelled.";
            return false;
        }
        block = input.read(kCopyBlock);
        if (block.isEmpty())
        {
            if (input.error() != QFileDevice::NoError)
            {
                *error = QString("%1 could not be read: %2").arg(QDir::toNativeSeparators(source), input.errorString());
                return false;
            }
            break;
        }
        if (output.write(block) != block.size())
        {
            *error = QString("%1 could not be written: %2").arg(QDir::toNativeSeparators(target), output.errorString());
            return false;
        }
        *bytesDone += block.size();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - mLastProgress >= 100)
        {
            mLastProgress = now;
            emit Progress(filesDone, filesTotal, *bytesDone, bytesTotal);
        }
    }
    if (!output.flush())
    {
        *error = QString("%1 could not be written: %2").arg(QDir::toNativeSeparators(target), output.errorString());
        return false;
    }
    return true;
}

