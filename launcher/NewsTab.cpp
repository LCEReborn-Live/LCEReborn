#include "NewsTab.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QXmlStreamReader>
#include <QUrl>
#include <QVBoxLayout>

#include "BlockIcons.hpp"
#include "BuildVersion.hpp"
#include "LauncherPaths.hpp"
#include "UpdateCheck.hpp"
#include "Widgets.hpp"

namespace
{
const char* const kLiveReleasesUrl = "https://github.com/LCEReborn-Live/LCEReborn/releases.atom";
const char* const kUpstreamReleasesUrl = "https://github.com/LCEReborn/LCEReborn/releases.atom";
const char* const kLiveRepositoryUrl = "https://github.com/LCEReborn-Live/LCEReborn";
const char* const kUpstreamRepositoryUrl = "https://github.com/LCEReborn/LCEReborn";
const int kTagRole = Qt::UserRole + 1;
const int kTitleRole = Qt::UserRole + 2;
const int kUrlRole = Qt::UserRole + 3;
const int kPublishedRole = Qt::UserRole + 4;
const int kBodyRole = Qt::UserRole + 5;
const int kPrereleaseRole = Qt::UserRole + 6;
const int kRepositoryRole = Qt::UserRole + 7;
const int kLiveFeed = 1;
const int kUpstreamFeed = 2;
const int kBlockSize = 40;

QString NewsCachePath(const QString& fileName)
{
    return QDir(LauncherPaths::DataDir()).filePath(fileName);
}

QString CommitOf(const QString& tag, const QString& title)
{
    static const QRegularExpression buildTag("^build-([0-9a-fA-F]{7,40})$");
    static const QRegularExpression leadingCommit("^([0-9a-fA-F]{8})\\b");
    QRegularExpressionMatch match = buildTag.match(tag);
    if (match.hasMatch())
        return match.captured(1).left(8).toLower();
    match = leadingCommit.match(title);
    if (match.hasMatch())
        return match.captured(1).toLower();
    return QString();
}

QString HowLongAgo(const QDateTime& published)
{
    const qint64 days = published.toLocalTime().date().daysTo(QDate::currentDate());
    if (days <= 0)
        return "Today";
    if (days == 1)
        return "Yesterday";
    if (days < 31)
        return QString("%1 days ago").arg(days);
    return QLocale().toString(published.toLocalTime().date(), "d MMMM yyyy");
}

void UseGameText(QTextDocument* document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
    {
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setFontWeight(QFont::Normal);
        format.setProperty(QTextFormat::FontSizeAdjustment, 0);
        format.setProperty(QTextFormat::FontPixelSize, block.blockFormat().headingLevel() > 0 ? 20 : 18);
        cursor.mergeCharFormat(format);
        if (block.blockFormat().headingLevel() > 0)
        {
            QTextBlockFormat spacing = block.blockFormat();
            spacing.setTopMargin(10);
            spacing.setBottomMargin(4);
            cursor.setBlockFormat(spacing);
        }
    }
}
}

NewsTab::NewsTab(QWidget* parent)
    : QWidget(parent), mNetwork(new QNetworkAccessManager(this)), mReleases(new QListWidget(this)),
      mTitle(new QLabel(this)), mDate(new QLabel(this)), mBody(new QTextBrowser(this)), mNote(new QLabel(this))
{
    mReleases->setObjectName("GameRows");
    mReleases->setItemDelegate(new GameRowDelegate(mReleases));
    mReleases->setFrameShape(QFrame::NoFrame);
    mReleases->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    mReleases->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mReleases->setMouseTracking(true);
    mReleases->setUniformItemSizes(true);
    mNote->setObjectName("Muted");
    mNote->setWordWrap(true);
    mNote->hide();

    auto* refresh = new QPushButton("Refresh", this);
    auto* links = new QPushButton("Links", this);
    auto* linkMenu = new QMenu(links);
    const QString liveRepository = kLiveRepositoryUrl;
    const QString upstreamRepository = kUpstreamRepositoryUrl;
    const struct
    {
        const char* label;
        QString url;
    } entries[] = {
        {"LCEReborn Live GitHub", liveRepository},
        {"LCEReborn Live releases", liveRepository + "/releases"},
        {"Original LCEReborn GitHub", upstreamRepository},
        {"Original LCEReborn releases", upstreamRepository + "/releases"},
        {"Report a bug", liveRepository + "/issues"},
        {"Documentation", liveRepository + "/tree/main/docs"},
    };
    for (const auto& entry : entries)
    {
        const QUrl url(entry.url);
        linkMenu->addAction(entry.label, this, [url] { QDesktopServices::openUrl(url); });
    }
    links->setMenu(linkMenu);
    connect(refresh, &QPushButton::clicked, this, &NewsTab::Refresh);

    auto* listWell = new GameWell(this);
    listWell->setFixedWidth(400);
    auto* listLayout = new QVBoxLayout(listWell);
    listLayout->setContentsMargins(10, 10, 10, 10);
    listLayout->setSpacing(10);
    listLayout->addWidget(mReleases, 1);
    listLayout->addWidget(mNote);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(refresh, 1);
    buttons->addWidget(links, 1);
    listLayout->addLayout(buttons);

    mTitle->setObjectName("NewsTitle");
    mTitle->setTextFormat(Qt::RichText);
    mTitle->setOpenExternalLinks(true);
    mTitle->setWordWrap(true);
    mDate->setObjectName("NewsDate");
    mBody->setObjectName("NewsBody");
    mBody->setOpenExternalLinks(true);
    mBody->setFrameShape(QFrame::NoFrame);
    mBody->document()->setDocumentMargin(0);

    auto* notesWell = new GameWell(this);
    auto* notesLayout = new QVBoxLayout(notesWell);
    notesLayout->setContentsMargins(22, 18, 14, 14);
    notesLayout->setSpacing(6);
    notesLayout->addWidget(mTitle);
    notesLayout->addWidget(mDate);
    notesLayout->addSpacing(8);
    notesLayout->addWidget(mBody, 1);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(listWell);
    layout->addWidget(notesWell, 1);

    connect(mReleases, &QListWidget::currentRowChanged, this, &NewsTab::ShowSelected);
    connect(mNetwork, &QNetworkAccessManager::finished, this, &NewsTab::OnReply);
    QMetaObject::invokeMethod(this, &NewsTab::Refresh, Qt::QueuedConnection);
}

void NewsTab::Refresh()
{
    mWarnings.clear();
    QFile liveCache(NewsCachePath("launcher_news_live.json"));
    if (liveCache.open(QIODevice::ReadOnly)) ParseReleases(liveCache.readAll(), &mLiveReleases, true);
    QFile upstreamCache(NewsCachePath("launcher_news_upstream.json"));
    if (upstreamCache.open(QIODevice::ReadOnly)) ParseReleases(upstreamCache.readAll(), &mUpstreamReleases, false);
    RenderReleases();

    const auto requestFeed = [this](const char* url, int feed) {
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("Accept", "application/atom+xml, application/xml;q=0.9");
        request.setRawHeader("User-Agent", "LCEReborn-Launcher");
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::User, feed);
        mNetwork->get(request);
    };
    requestFeed(kLiveReleasesUrl, kLiveFeed);
    requestFeed(kUpstreamReleasesUrl, kUpstreamFeed);
}

void NewsTab::OnReply(QNetworkReply* reply)
{
    const int feed = reply->request().attribute(QNetworkRequest::User).toInt();
    QJsonArray* releases = feed == kLiveFeed ? &mLiveReleases : &mUpstreamReleases;
    const QString cacheName = feed == kLiveFeed ? "launcher_news_live.json" : "launcher_news_upstream.json";
    const QString feedName = feed == kLiveFeed ? "LCEReborn Live" : "original LCEReborn";
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
    {
        mWarnings.append(QString("Couldn't reach %1 releases (%2).").arg(feedName, reply->errorString()));
        RenderReleases();
        return;
    }
    const QByteArray data = reply->readAll();
    if (!ParseReleases(data, releases, feed == kLiveFeed))
    {
        mWarnings.append(QString("Couldn't read %1 releases.").arg(feedName));
        RenderReleases();
        return;
    }
    const QByteArray normalizedJson = QJsonDocument(*releases).toJson(QJsonDocument::Compact);
    QSaveFile cache(NewsCachePath(cacheName));
    if (cache.open(QIODevice::WriteOnly))
    {
        cache.write(normalizedJson);
        cache.commit();
    }
    RenderReleases();
}

bool NewsTab::ParseReleases(const QByteArray& data, QJsonArray* releases, bool liveFeed) const
{
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &jsonError);
    if (jsonError.error == QJsonParseError::NoError && document.isArray())
    {
        *releases = document.array();
        return true;
    }

    QXmlStreamReader xml(data);
    QJsonArray parsed;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("entry"))
            continue;

        QString id;
        QString title;
        QString updated;
        QString url;
        QString body;
        while (!(xml.isEndElement() && xml.name() == QLatin1String("entry")) && !xml.atEnd())
        {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            const auto name = xml.name();
            if (name == QLatin1String("id"))
                id = xml.readElementText();
            else if (name == QLatin1String("title"))
                title = xml.readElementText();
            else if (name == QLatin1String("updated"))
                updated = xml.readElementText();
            else if (name == QLatin1String("link")
                     && xml.attributes().value(QLatin1String("rel")) == QLatin1String("alternate"))
                url = xml.attributes().value(QLatin1String("href")).toString();
            else if (name == QLatin1String("content"))
                body = xml.readElementText();
        }

        const QString tag = id.section('/', -1);
        if (tag.isEmpty() || title.isEmpty() || url.isEmpty())
            continue;
        QJsonObject release;
        release.insert("tag_name", tag);
        release.insert("name", title);
        release.insert("html_url", url);
        release.insert("published_at", updated);
        release.insert("body", body);
        release.insert("draft", false);
        release.insert("prerelease", tag.contains('-'));
        if (liveFeed && QRegularExpression("^v\\d+\\.\\d+\\.\\d+$").match(tag).hasMatch())
        {
            const QString version = tag.mid(1);
            QJsonObject installer;
            installer.insert("name", "LCEReborn-Live-Installer-" + version + ".exe");
            installer.insert("browser_download_url", "https://github.com/LCEReborn-Live/LCEReborn/releases/download/"
                                                        + tag + "/LCEReborn-Live-Installer-" + version + ".exe");
            release.insert("assets", QJsonArray{installer});
        }
        parsed.append(release);
    }
    if (xml.hasError() || parsed.isEmpty())
        return false;
    *releases = parsed;
    return true;
}

void NewsTab::RenderReleases()
{
    if (mLiveReleases.isEmpty() && mUpstreamReleases.isEmpty())
    {
        ShowOffline(mWarnings.join(" "));
        return;
    }

    const QString selectedTag = mReleases->currentItem() != nullptr
                                    ? mReleases->currentItem()->data(kTagRole).toString()
                                    : QString();
    const QString installed = BuildCommit().left(8).toLower();
    mReleases->blockSignals(true);
    mReleases->clear();
    int selectRow = 0;
    QJsonArray combined = mLiveReleases;
    for (const QJsonValue& value : mUpstreamReleases)
    {
        QJsonObject release = value.toObject();
        release.insert("_lce_feed", "upstream");
        combined.append(release);
    }
    for (const QJsonValue& value : NewestFirst(combined))
    {
        const QJsonObject release = value.toObject();
        if (release.value("draft").toBool())
            continue;
        const bool isLive = release.value("_lce_feed").toString() != "upstream";
        const QString tag = release.value("tag_name").toString();
        QString title = release.value("name").toString().trimmed();
        if (title.isEmpty())
            title = tag;
        const QDateTime published = QDateTime::fromString(release.value("published_at").toString(), Qt::ISODate);
        const QString commit = CommitOf(tag, title);
        const bool prerelease = release.value("prerelease").toBool();

        const QString source = isLive ? "Live" : "Original";
        const QString label = QString("%1  |  %2%3")
                                  .arg(source, title,
                                       published.isValid()
                                           ? QString("  -  %1").arg(QLocale().toString(published.toLocalTime().date(), "d MMM"))
                                           : QString());
        auto* item = new QListWidgetItem(label, mReleases);
        item->setData(Qt::DecorationRole, BlockIcons::Render(commit.isEmpty() ? tag : commit, kBlockSize));
        QStringList notes;
        if (prerelease)
            notes.append("Pre-release");
        if (isLive && !commit.isEmpty() && commit == installed)
            notes.append("This build");
        item->setData(GameRowDelegate::kSubtitleRole, notes.join(", "));
        item->setData(kTagRole, tag);
        item->setData(kTitleRole, title);
        item->setData(kUrlRole, release.value("html_url").toString());
        item->setData(kPublishedRole, published);
        item->setData(kBodyRole, release.value("body").toString());
        item->setData(kPrereleaseRole, prerelease);
        item->setData(kRepositoryRole, isLive ? kLiveRepositoryUrl : kUpstreamRepositoryUrl);
        item->setToolTip(title);
        if (tag == selectedTag)
            selectRow = mReleases->count() - 1;
    }
    mReleases->blockSignals(false);
    if (mReleases->count() == 0)
        return;
    mReleases->setCurrentRow(selectRow);
    ShowSelected();

    mNote->setText(mWarnings.join(" "));
    mNote->setVisible(!mWarnings.isEmpty());
    const NewerRelease newer = FindNewerRelease(mLiveReleases, BuildTag(), BuildTimestamp());
    emit UpdateChecked(newer.name, newer.installerUrl, newer.url);
}

void NewsTab::ShowSelected()
{
    const QListWidgetItem* item = mReleases->currentItem();
    if (item == nullptr)
        return;
    static const QRegularExpression images("!\\[[^\\]]*\\]\\([^)]*\\)");
    const QString url = item->data(kUrlRole).toString();
    const QString title = item->data(kTitleRole).toString().toHtmlEscaped();
    mTitle->setText(url.isEmpty() ? title
                                  : QString("<a href='%1' style='color:#404040; text-decoration:none'>%2</a>")
                                        .arg(url.toHtmlEscaped(), title));
    mTitle->setToolTip(url.isEmpty() ? QString() : "Open the release on GitHub");

    const QDateTime published = item->data(kPublishedRole).toDateTime();
    QString when = published.isValid() ? HowLongAgo(published) : QString();
    if (item->data(kPrereleaseRole).toBool())
        when += when.isEmpty() ? "Pre-release" : ", pre-release";
    const QString repository = item->data(kRepositoryRole).toString();
    const QString source = repository.contains("LCEReborn-Live") ? "LCEReborn Live" : "Original LCEReborn";
    mDate->setText(when.isEmpty() ? source : source + " - " + when);
    mDate->setVisible(true);

    QString body = item->data(kBodyRole).toString();
    body.remove(images);
    body = body.trimmed();
    if (body.isEmpty())
        mBody->setPlainText("No notes for this release.");
    else if (body.trimmed().startsWith('<'))
        mBody->setHtml(body);
    else
        mBody->setMarkdown(body);
    UseGameText(mBody->document());
}

void NewsTab::ShowOffline(const QString& note)
{
    mReleases->clear();
    mTitle->setText("No news yet");
    mDate->setText("LCEReborn Live and original LCEReborn releases will appear here.");
    mDate->show();
    mBody->clear();
    mNote->setText(note);
    mNote->setVisible(!note.isEmpty());
    emit UpdateChecked(QString(), QString(), QString());
}
