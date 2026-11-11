#include "UpdateCheck.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <vector>

QJsonArray NewestFirst(const QJsonArray& releases)
{
    std::vector<QJsonValue> sorted(releases.begin(), releases.end());
    const auto published = [](const QJsonValue& release) {
        return QDateTime::fromString(release.toObject().value("published_at").toString(), Qt::ISODate);
    };
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const QJsonValue& a, const QJsonValue& b) { return published(a) > published(b); });
    QJsonArray result;
    for (const QJsonValue& release : sorted)
        result.append(release);
    return result;
}

NewerRelease FindNewerRelease(const QJsonArray& releases, const QString& tag, const QDateTime& built)
{
    QJsonObject latest;
    bool ownIsRelease = false;
    for (const QJsonValue& value : NewestFirst(releases))
    {
        const QJsonObject release = value.toObject();
        if (release.value("draft").toBool())
            continue;
        if (!tag.isEmpty() && release.value("tag_name").toString() == tag)
            ownIsRelease = true;
        if (latest.isEmpty() && !release.value("prerelease").toBool())
            latest = release;
    }
    if (latest.isEmpty() || (!tag.isEmpty() && latest.value("tag_name").toString() == tag))
        return NewerRelease();

    if (!ownIsRelease)
    {
        const QDateTime published = QDateTime::fromString(latest.value("published_at").toString(), Qt::ISODate).toUTC();
        if (!built.isValid() || !published.isValid() || published <= built.toUTC())
            return NewerRelease();
    }

    NewerRelease newer;
    newer.name = latest.value("name").toString().trimmed();
    if (newer.name.isEmpty())
        newer.name = latest.value("tag_name").toString();
    newer.url = latest.value("html_url").toString();
    for (const QJsonValue& assetValue : latest.value("assets").toArray())
    {
        const QJsonObject asset = assetValue.toObject();
        const QString name = asset.value("name").toString();
        if (name.startsWith("LCEReborn-Live-Installer-") && name.endsWith(".exe", Qt::CaseInsensitive))
        {
            newer.installerUrl = asset.value("browser_download_url").toString();
            break;
        }
    }
    return newer;
}
