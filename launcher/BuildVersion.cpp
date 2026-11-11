#include "BuildVersion.hpp"

#include <QDate>
#include <QLocale>

#include "BuildInfo.hpp"

QString BuildVersion()
{
    const QDate date = BuildDate();
    const QString built = date.isValid() ? QLocale().toString(date, "d MMM yyyy") : QString(LCE_BUILD_DATE);
    return QString("%1%2, %3").arg(LCE_BUILD_COMMIT, LCE_BUILD_DIRTY ? " (modified)" : "", built);
}

QString BuildCommit()
{
    return LCE_BUILD_COMMIT;
}

QString BuildTag()
{
    return LCE_BUILD_TAG;
}

QDate BuildDate()
{
    return QDate::fromString(LCE_BUILD_DATE, Qt::ISODate);
}

QDateTime BuildTimestamp()
{
    return QDateTime::fromString(LCE_BUILD_TIMESTAMP, Qt::ISODate);
}
