#ifndef LAUNCHER_BUILDVERSION_HPP
#define LAUNCHER_BUILDVERSION_HPP

#include <QDate>
#include <QDateTime>
#include <QString>

QString BuildVersion();
QString BuildCommit();
QString BuildTag();
QDate BuildDate();
QDateTime BuildTimestamp();

#endif
