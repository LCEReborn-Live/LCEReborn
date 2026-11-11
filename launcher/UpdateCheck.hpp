#ifndef LAUNCHER_UPDATECHECK_HPP
#define LAUNCHER_UPDATECHECK_HPP

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QString>

struct NewerRelease
{
    QString name;
    QString url;
    QString installerUrl;
};

QJsonArray NewestFirst(const QJsonArray& releases);
NewerRelease FindNewerRelease(const QJsonArray& releases, const QString& tag, const QDateTime& built);

#endif
