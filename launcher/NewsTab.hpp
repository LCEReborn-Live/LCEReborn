#ifndef LAUNCHER_NEWSTAB_HPP
#define LAUNCHER_NEWSTAB_HPP

#include <QByteArray>
#include <QJsonArray>
#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QTextBrowser;

class NewsTab : public QWidget
{
    Q_OBJECT

public:
    explicit NewsTab(QWidget* parent = nullptr);

    void Refresh();

signals:
    void UpdateChecked(const QString& newerName, const QString& installerUrl, const QString& releaseUrl);

private:
    void OnReply(QNetworkReply* reply);
    void RenderReleases();
    bool ParseReleases(const QByteArray& data, QJsonArray* releases, bool liveFeed) const;
    void ShowOffline(const QString& note);
    void ShowSelected();

    QNetworkAccessManager* mNetwork;
    QListWidget* mReleases;
    QLabel* mTitle;
    QLabel* mDate;
    QTextBrowser* mBody;
    QLabel* mNote;
    QJsonArray mLiveReleases;
    QJsonArray mUpstreamReleases;
    QStringList mWarnings;
};

#endif
