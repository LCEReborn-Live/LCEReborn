#include "MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QFrame>
#include <QDateTime>
#include <QDataStream>
#include <QGuiApplication>
#include <QScreen>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QInputDialog>
#include <QImage>
#include <QLineEdit>
#include <QScrollArea>
#include <QMessageBox>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>
#include <string>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <mmsystem.h>
#pragma comment(lib, "Crypt32.lib")
#endif

#include "BuildVersion.hpp"
#include "GameArt.hpp"
#include "GameRunner.hpp"
#include "LauncherPaths.hpp"
#include "LiveJoin.hpp"
#include "NewsTab.hpp"
#include "ProfileDialog.hpp"
#include "ProfilePopup.hpp"
#include "Profiles.hpp"
#include "ProfilesTab.hpp"
#include "SetupPage.hpp"
#include "Skins.hpp"
#include "Widgets.hpp"

namespace
{
#ifdef Q_OS_WIN
unsigned int gLiveInviteSoundGeneration = 0;
#endif

void PlayLiveInviteSound(QObject* context)
{
#ifdef Q_OS_WIN
    const unsigned int generation = ++gLiveInviteSoundGeneration;
    mciSendStringW(L"close LCEInviteSound", nullptr, 0, nullptr);
    const QString soundPath = QDir::toNativeSeparators(
        QDir(QCoreApplication::applicationDirPath()).filePath("resources/ps4-trophy.mp3"));
    const QString open = QString("open \"%1\" type mpegvideo alias LCEInviteSound").arg(soundPath);
    if (mciSendStringW(reinterpret_cast<LPCWSTR>(open.utf16()), nullptr, 0, nullptr) == 0
        && mciSendStringW(L"play LCEInviteSound", nullptr, 0, nullptr) == 0)
    {
        QTimer::singleShot(10000, context, [generation] {
            if (generation == gLiveInviteSoundGeneration)
            {
                mciSendStringW(L"stop LCEInviteSound", nullptr, 0, nullptr);
                mciSendStringW(L"close LCEInviteSound", nullptr, 0, nullptr);
            }
        });
        return;
    }
    mciSendStringW(L"close LCEInviteSound", nullptr, 0, nullptr);
#else
    Q_UNUSED(context);
#endif
    QApplication::beep();
}
}

namespace
{
enum Page
{
    NewsPage,
    LogPage,
    ProfilesPage,
    FriendsPage,
    PageCount
};

const char* const kPageNames[PageCount] = {"News", "Game Log", "Profiles", "Friends"};
const int kBottomRowHeight = 62;

bool SaveLiveToken(const QString& token)
{
    QSettings settings(LauncherPaths::SettingsFile(), QSettings::IniFormat);
    if (token.isEmpty()) { settings.remove("live/protectedSession"); settings.sync(); return settings.status() == QSettings::NoError; }
#ifdef Q_OS_WIN
    QByteArray bytes = token.toUtf8();
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(bytes.data())};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"LCE Live", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return false;
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    settings.setValue("live/protectedSession", QString::fromLatin1(encrypted.toBase64()));
    settings.sync();
    return settings.status() == QSettings::NoError;
#else
    return false;
#endif
}

static void WritePckString(QDataStream& stream, const QString& value)
{
    stream << static_cast<quint32>(value.size());
    for (const QChar character : value)
        stream << static_cast<quint16>(character.unicode());
    stream << static_cast<quint32>(0);
}

static void WritePckParameter(QDataStream& stream, quint32 index, const QString& value)
{
    stream << index;
    WritePckString(stream, value);
}

static quint32 LiveSkinModelFlag(const QByteArray& bytes)
{
    const QImage skin = QImage::fromData(bytes).convertToFormat(QImage::Format_ARGB32);
    if (skin.size() != QSize(64, 64)) return 0x00040000u;
    const QRect armRegions[] = {QRect(50, 16, 2, 4), QRect(54, 20, 2, 12), QRect(42, 48, 2, 4),
                                QRect(46, 52, 2, 12)};
    for (const QRect& region : armRegions)
    {
        for (int y = region.top(); y <= region.bottom(); ++y)
        {
            for (int x = region.left(); x <= region.right(); ++x)
            {
                if (qAlpha(skin.pixel(x, y)) < 255) return 0x00080000u;
            }
        }
    }
    return 0x00040000u;
}

static QByteArray BuildLiveCosmeticPck(qint64 userId, const QString& username, const QByteArray& skin,
                                       const QByteArray& cape)
{
    if (userId <= 0 || (skin.isEmpty() && cape.isEmpty()))
        return {};

    const quint32 skinId = static_cast<quint32>((static_cast<quint64>(userId) * 2654435761ULL) & 0x7fffffffULL);
    const QString id = QString::number(skinId == 0 ? 1 : skinId).rightJustified(8, QChar('0'));
    const QString skinPath = "dlcskin" + id + ".png";
    const QString capePath = "dlccape" + id + ".png";
    QStringList properties{"DISPLAYNAME", "GAME_FLAGS", "FREE"};
    if (!skin.isEmpty()) properties.append("ANIM");
    if (!cape.isEmpty()) properties.append("CAPEPATH");
    const QHash<QString, quint32> propertyIndex{{"DISPLAYNAME", 0}, {"GAME_FLAGS", 1}, {"FREE", 2},
                                                {"ANIM", 3}, {"CAPEPATH", 4}};

    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint32(3) << static_cast<quint32>(properties.size());
    for (quint32 index = 0; index < static_cast<quint32>(properties.size()); ++index)
    {
        stream << index;
        WritePckString(stream, properties.at(static_cast<int>(index)));
    }
    stream << static_cast<quint32>((!skin.isEmpty() ? 1 : 0) + (!cape.isEmpty() ? 1 : 0));
    if (!skin.isEmpty())
    {
        stream << static_cast<quint32>(skin.size()) << quint32(0);
        WritePckString(stream, skinPath);
    }
    if (!cape.isEmpty())
    {
        stream << static_cast<quint32>(cape.size()) << quint32(1);
        WritePckString(stream, capePath);
    }
    if (!skin.isEmpty())
    {
        stream << static_cast<quint32>(cape.isEmpty() ? 4 : 5);
        WritePckParameter(stream, propertyIndex.value("DISPLAYNAME"), username);
        WritePckParameter(stream, propertyIndex.value("GAME_FLAGS"), "0x00");
        WritePckParameter(stream, propertyIndex.value("FREE"), "1");
        WritePckParameter(stream, propertyIndex.value("ANIM"),
                          QString("0x%1").arg(LiveSkinModelFlag(skin), 8, 16, QChar('0')));
        if (!cape.isEmpty()) WritePckParameter(stream, propertyIndex.value("CAPEPATH"), capePath);
        stream.writeRawData(skin.constData(), skin.size());
    }
    if (!cape.isEmpty())
    {
        stream << quint32(0);
        stream.writeRawData(cape.constData(), cape.size());
    }
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

QString LoadLiveToken()
{
#ifdef Q_OS_WIN
    QSettings settings(LauncherPaths::SettingsFile(), QSettings::IniFormat);
    QByteArray encrypted = QByteArray::fromBase64(settings.value("live/protectedSession").toByteArray());
    if (encrypted.isEmpty()) return QString();
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE*>(encrypted.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return QString();
    const QString token = QString::fromUtf8(reinterpret_cast<const char*>(output.pbData), static_cast<int>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return token;
#else
    return QString();
#endif
}
}

MainWindow::MainWindow(ProfileStore& store, QWidget* parent)
    : QMainWindow(parent), mStore(store), mRunner(new GameRunner(this)), mLiveNetwork(new QNetworkAccessManager(this)),
      mLiveButton(new GameButton("Live Sign In", this)), mPanorama(new Panorama(this)),
      mRoot(new QStackedWidget(mPanorama)), mShellLayout(nullptr), mTabs(new TabbedPanel(this)),
      mPages(new QStackedWidget(this)), mSetup(new SetupPage(this)), mNews(new NewsTab(this)),
      mLog(new GameLogTab(this)), mProfileButton(new GameButton(QString(), this)), mPlay(new PlayButton(this)),
      mStatus(new ShadowLabel(QString(), 16, this))
{
    setWindowTitle(QString("LCEReborn Launcher - %1").arg(BuildVersion()));
    resize(1280, 720);
    setMinimumSize(960, 600);

    mPages->addWidget(mNews);
    mPages->addWidget(mLog);
    auto* profiles = new ProfilesTab(mStore, this);
    mPages->addWidget(profiles);
    mPages->addWidget(CreateLiveFriendsPage());
    for (const char* name : kPageNames)
        mTabs->AddTab(name);
    mTabs->SetContent(mPages);
    connect(mTabs, &TabbedPanel::CurrentChanged, mPages, &QStackedWidget::setCurrentIndex);

    auto* corner = new QWidget(this);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(12);
    auto* mark = new QLabel(corner);
    mark->setPixmap(QPixmap(":/launcher/mark.png").scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mark->setFixedSize(32, 32);
    cornerLayout->addStretch(1);
    mLiveButton->SetPixelSize(14);
    mLiveButton->SetReadableFont(true);
    mLiveButton->setFixedSize(150, 40);
    cornerLayout->addWidget(mLiveButton);
    cornerLayout->addWidget(mark);
    cornerLayout->addWidget(new ShadowLabel("LCE Reborn", 24, corner));
    mTabs->SetCorner(corner);

    mProfileButton->SetPixelSize(20);
    mProfileButton->setFixedSize(360, kBottomRowHeight);
    mPlay->setFixedSize(280, kBottomRowHeight);
    auto* update = new GameButton("Update", this);
    mUpdateButton = update;
    update->SetPixelSize(20);
    update->setFixedSize(150, kBottomRowHeight);
    update->hide();
    auto* build = new ShadowLabel(QString("Build %1").arg(BuildVersion()), 16, this);
    auto* statusColumn = new QVBoxLayout;
    statusColumn->setSpacing(6);
    statusColumn->addStretch(1);
    statusColumn->addWidget(mStatus);
    statusColumn->addWidget(build);
    statusColumn->addStretch(1);

    auto* bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(20);
    bottomRow->addWidget(mProfileButton);
    bottomRow->addLayout(statusColumn, 1);
    bottomRow->addWidget(update);
    bottomRow->addWidget(mPlay);

    auto* shell = new QWidget(this);
    mShellLayout = new QVBoxLayout(shell);
    mShellLayout->setSpacing(12);
    mShellLayout->addWidget(mTabs, 1);
    mShellLayout->addLayout(bottomRow);

    mRoot->addWidget(mSetup);
    mRoot->addWidget(shell);
    auto* rootLayout = new QVBoxLayout(mPanorama);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(mRoot);
    setCentralWidget(mPanorama);

    connect(&mStore, &ProfileStore::Changed, this, &MainWindow::RefreshProfiles);
    connect(mProfileButton, &GameButton::clicked, this, &MainWindow::ShowProfileMenu);
    connect(mPlay, &PlayButton::clicked, this, &MainWindow::Play);
    connect(mLiveButton, &GameButton::clicked, this, &MainWindow::LiveAccount);
    QTimer* liveInvites = new QTimer(this);
    liveInvites->setInterval(4000);
    connect(liveInvites, &QTimer::timeout, this, &MainWindow::PollLiveInvites);
    liveInvites->start();
    QTimer* friendsRefresh = new QTimer(this);
    friendsRefresh->setInterval(15000);
    connect(friendsRefresh, &QTimer::timeout, this, &MainWindow::RefreshLiveFriends);
    friendsRefresh->start();
    QTimer* presence = new QTimer(this);
    presence->setInterval(15000);
    connect(presence, &QTimer::timeout, this, &MainWindow::SendLivePresence);
    presence->start();
    QTimer* cosmetics = new QTimer(this);
    cosmetics->setInterval(5 * 60 * 1000);
    connect(cosmetics, &QTimer::timeout, this, [this] { if (!mLiveToken.isEmpty()) RefreshLiveCosmetics(); });
    cosmetics->start();
    connect(mRunner, &GameRunner::Output, mLog, &GameLogTab::Add);
    connect(mRunner, &GameRunner::Started, this, &MainWindow::OnGameStarted);
    connect(mRunner, &GameRunner::Finished, this, &MainWindow::OnGameFinished);
    connect(mSetup, &SetupPage::Message, mLog, &GameLogTab::AddLauncherMessage);
    connect(mSetup, &SetupPage::Imported, this, &MainWindow::OnImported);
    connect(mSetup, &SetupPage::BackRequested, this, [this] { mRoot->setCurrentIndex(1); });
    connect(mNews, &NewsTab::UpdateChecked, this,
            [this, update](const QString& newerName, const QString& installerUrl, const QString& releaseUrl) {
        mUpdateName = newerName;
        mUpdateInstallerUrl = installerUrl;
        mUpdateReleaseUrl = releaseUrl;
        update->setVisible(!newerName.isEmpty());
        update->setEnabled(!installerUrl.isEmpty() || !releaseUrl.isEmpty());
        update->setToolTip(installerUrl.isEmpty() ? QString("%1 is available; its installer is not published yet.").arg(newerName)
                                                   : QString("Download and install %1.").arg(newerName));
    });
    connect(update, &GameButton::clicked, this, &MainWindow::DownloadLauncherUpdate);
    connect(profiles, &ProfilesTab::ReimportRequested, this, [this] {
        if (mRunner->IsRunning())
        {
            QMessageBox::information(this, "Re-import Game Files", "Close the game before importing its files again.");
            return;
        }
        mSetup->SetCanGoBack(true);
        mRoot->setCurrentIndex(0);
    });

    QSettings settings(LauncherPaths::SettingsFile(), QSettings::IniFormat);
    restoreGeometry(settings.value("window/geometry").toByteArray());
    const int page = settings.value("window/page", NewsPage).toInt();
    ShowPage(page >= 0 && page < PageCount ? page : NewsPage);
    const bool imported = QFileInfo(LauncherPaths::ContentCheckFile()).isFile();
    mRoot->setCurrentIndex(imported ? 1 : 0);
    if (imported && !settings.value("notices/packsFolder", false).toBool())
        QTimer::singleShot(0, this, &MainWindow::ShowPacksNotice);

    RefreshProfiles();
    RenderLiveFriends();
    RestoreLiveSession();
}

void MainWindow::DownloadLauncherUpdate()
{
#ifdef Q_OS_WIN
    if (mUpdateName.isEmpty())
        return;
    const QString installedPath = QDir::cleanPath(LauncherPaths::InstallDir());
    const QString setupPath = QDir::cleanPath(QDir(qEnvironmentVariable("LOCALAPPDATA"))
                                                   .filePath("Programs/LCEReborn Live"));
    if (installedPath.compare(setupPath, Qt::CaseInsensitive) != 0)
    {
        QMessageBox::information(this, "Update LCEReborn Live",
                                 "This is a portable copy. The release page will open so you can download the new portable archive.");
        if (!mUpdateReleaseUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(mUpdateReleaseUrl));
        return;
    }
    if (mUpdateInstallerUrl.isEmpty())
    {
        QMessageBox::information(this, "Update LCEReborn Live", "The installer is not attached to this release yet.");
        return;
    }
    if (mRunner->IsRunning())
    {
        QMessageBox::information(this, "Update LCEReborn Live", "Close the game before updating the launcher.");
        return;
    }
    const auto answer = QMessageBox::question(
        this, "Update LCEReborn Live",
        QString("Download %1? The launcher will close, install the update, and reopen automatically.").arg(mUpdateName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    const QUrl url(mUpdateInstallerUrl);
    if (!url.isValid() || url.scheme() != "https" || url.host() != "github.com")
    {
        QMessageBox::warning(this, "Update LCEReborn Live", "The release download URL is invalid.");
        return;
    }

    auto* file = new QTemporaryFile(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                        .filePath("LCEReborn-Live-Update-XXXXXX.exe"), this);
    file->setAutoRemove(false);
    if (!file->open())
    {
        file->deleteLater();
        QMessageBox::warning(this, "Update LCEReborn Live", "Could not create a temporary installer file.");
        return;
    }
    if (mUpdateButton) mUpdateButton->setEnabled(false);
    SetStatus("Downloading launcher update…", GameColours::kWarning);

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "LCEReborn-Launcher");
    request.setRawHeader("Accept", "application/octet-stream");
    request.setTransferTimeout(60000);
    QNetworkReply* reply = mLiveNetwork->get(request);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, file] {
        if (file->write(reply->readAll()) < 0)
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0)
            SetStatus(QString("Downloading update… %1%").arg((received * 100) / total), GameColours::kWarning);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file] {
        const QByteArray remaining = reply->readAll();
        bool saved = remaining.isEmpty() || file->write(remaining) == remaining.size();
        const QString error = reply->errorString();
        reply->deleteLater();
        saved = saved && reply->error() == QNetworkReply::NoError && file->flush() && file->size() > 1024;
        file->seek(0);
        saved = saved && file->read(2) == QByteArray("MZ", 2);
        const QString installerPath = file->fileName();
        file->close();
        delete file; // Release the download handle before asking Windows to execute the file.
        if (!saved)
        {
            if (mUpdateButton) mUpdateButton->setEnabled(true);
            QFile::remove(installerPath);
            SetStatus("Update download failed", GameColours::kWarning);
            QMessageBox::warning(this, "Update LCEReborn Live", QString("The installer could not be downloaded: %1").arg(error));
            return;
        }
#ifdef Q_OS_WIN
        QTemporaryDir helperDirectory(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                          .filePath("LCEReborn-Live-Updater-XXXXXX"));
        const QString helperSource = QDir(LauncherPaths::InstallDir()).filePath("LCERebornUpdateHelper.exe");
        const QString helperPath = QDir(helperDirectory.path()).filePath("LCERebornUpdateHelper.exe");
        const QString launcherPath = QCoreApplication::applicationFilePath();
        qint64 helperPid = 0;
        if (!helperDirectory.isValid() || !QFile::copy(helperSource, helperPath)
            || !QProcess::startDetached(helperPath,
                                        { installerPath, launcherPath, QString::number(GetCurrentProcessId()) },
                                        helperDirectory.path(), &helperPid))
        {
            if (mUpdateButton) mUpdateButton->setEnabled(true);
            SetStatus("Could not start the update helper", GameColours::kWarning);
            QMessageBox::warning(this, "Update LCEReborn Live",
                                 QString("The update was downloaded, but its helper could not start.\n\n"
                                         "The installer was kept here:\n%1")
                                     .arg(QDir::toNativeSeparators(installerPath)));
            return;
        }
        helperDirectory.setAutoRemove(false);
        QApplication::quit();
        return;
#else
        if (!QProcess::startDetached(installerPath, {}))
        {
            if (mUpdateButton) mUpdateButton->setEnabled(true);
            SetStatus("Could not start the installer", GameColours::kWarning);
            QMessageBox::warning(this, "Update LCEReborn Live",
                                 QString("The installer downloaded, but could not be started. The file was kept here:\n%1")
                                     .arg(QDir::toNativeSeparators(installerPath)));
            return;
        }
#endif
#ifndef Q_OS_WIN
        close();
#endif
    });
#else
    QMessageBox::information(this, "Update LCEReborn Live", "In-launcher updates are currently available for Windows releases.");
#endif
}

void MainWindow::ShowPage(int index)
{
    mTabs->SetCurrent(index);
    mPages->setCurrentIndex(index);
}

void MainWindow::LiveAccount()
{
    if (!mLiveToken.isEmpty())
    {
        QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/sessions/current")};
        request.setRawHeader("Authorization", QByteArray("Bearer ") + mLiveToken.toUtf8());
        QNetworkReply* reply = mLiveNetwork->deleteResource(request);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        mLiveToken.clear();
        SaveLiveToken(QString());
        mLiveName.clear();
        mLiveUserId = 0; mLiveSkinCachePath.clear(); mLiveCapeCachePath.clear();
        mFriends = {}; mIncomingFriends = {}; mSentFriends = {}; mLiveWorlds = {};
        mPendingLiveJoinId = 0; mPendingLiveJoinOwner.clear(); ++mLiveJoinGeneration;
        RenderLiveFriends();
        mSeenInvites.clear();
        mSeenFriendRequests.clear();
        if (mInvitePopup) mInvitePopup->close();
        mLiveButton->setText("Live Sign In");
        RefreshProfiles();
        return;
    }

    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/device/start")};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(10000);
    mLiveButton->setEnabled(false);
    QNetworkReply* reply = mLiveNetwork->post(request, "{}");
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
        const QString deviceCode = data.value("device_code").toString();
        const QString userCode = data.value("user_code").toString();
        if (reply->error() != QNetworkReply::NoError || deviceCode.size() != 64 || userCode.size() != 12)
        {
            mLiveButton->setEnabled(true);
            QMessageBox::warning(this, "LCE Live", "Could not start browser sign-in.");
            reply->deleteLater();
            return;
        }
        reply->deleteLater();
        QUrl url("https://lcereborn.xyz/authorize");
        url.setQuery("v=2");
        url.setFragment(userCode);
        if (!QDesktopServices::openUrl(url))
        {
            mLiveButton->setEnabled(true);
            QMessageBox::warning(this, "LCE Live", "Could not open the browser.");
            return;
        }
        auto* notice = new QMessageBox(QMessageBox::Information, "LCE Live",
            QString("Confirm sign-in in your browser. Verify this code: %1").arg(userCode), QMessageBox::Cancel, this);
        notice->setAttribute(Qt::WA_DeleteOnClose);
        auto* timer = new QTimer(notice);
        timer->setInterval(3000);
        connect(notice, &QObject::destroyed, this, [this] { mLiveButton->setEnabled(true); });
        connect(timer, &QTimer::timeout, this, [this, timer, notice, deviceCode] {
            if (timer->property("waiting").toBool()) return;
            timer->setProperty("waiting", true);
            QNetworkRequest poll{QUrl("https://api.lcereborn.xyz/v1/device/token")};
            poll.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            poll.setTransferTimeout(10000);
            QJsonObject body;
            body.insert("device_code", deviceCode);
            QNetworkReply* result = mLiveNetwork->post(poll, QJsonDocument(body).toJson(QJsonDocument::Compact));
            QPointer<QTimer> safeTimer(timer);
            QPointer<QMessageBox> safeNotice(notice);
            connect(result, &QNetworkReply::finished, this, [this, result, safeTimer, safeNotice] {
                const QJsonObject answer = QJsonDocument::fromJson(result->readAll()).object();
                const int status = result->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result->deleteLater();
                if (!safeTimer || !safeNotice) return;
                safeTimer->setProperty("waiting", false);
                if (status == 202) return;
                safeTimer->stop();
                const QString name = answer.value("user").toObject().value("username").toString();
                const QString token = answer.value("token").toString();
                safeNotice->close();
                if (status != 201 || name.isEmpty() || token.isEmpty())
                {
                    QMessageBox::warning(this, "LCE Live", "Sign-in was declined, expired, or could not be completed.");
                    return;
                }
                mLiveName = name;
                mLiveToken = token;
                mLiveUserId = answer.value("user").toObject().value("id").toVariant().toLongLong();
                if (!SaveLiveToken(token)) mLog->AddLauncherMessage(LogLevel::Warn, "Could not save the Live session securely.");
                mLiveButton->setText("Live Sign Out");
                RefreshProfiles();
                SendLivePresence();
                RefreshLiveFriends();
                RefreshLiveCosmetics();
                PollLiveInvites();
                ContinueLiveJoin();
            });
        });
        notice->show();
        timer->start();
    });
}

void MainWindow::RestoreLiveSession()
{
    const QString token = LoadLiveToken();
    if (token.isEmpty()) return;
    mLiveSessionRestoring = true;
    mLiveButton->setEnabled(false);
    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/me")};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
    request.setTransferTimeout(10000);
    QNetworkReply* reply = mLiveNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject account = QJsonDocument::fromJson(reply->readAll()).object();
        const QString name = account.value("username").toString();
        reply->deleteLater();
        mLiveButton->setEnabled(true);
        mLiveSessionRestoring = false;
        if (status == 401) { SaveLiveToken(QString()); ContinueLiveJoin(); return; }
        if (status != 200 || name.isEmpty()) { mLog->AddLauncherMessage(LogLevel::Warn, "Could not restore Live session. Check your connection."); return; }
        mLiveName = name;
        mLiveUserId = account.value("id").toVariant().toLongLong();
        mLiveToken = token;
        mLiveButton->setText("Live Sign Out");
        RefreshProfiles(); SendLivePresence(); RefreshLiveFriends(); RefreshLiveCosmetics(); PollLiveInvites();
        ContinueLiveJoin();
    });
}

void MainWindow::RefreshLiveCosmetics(std::function<void()> completion)
{
    if (mLiveToken.isEmpty())
    {
        if (completion) completion();
        return;
    }
    if (mCosmeticsRefreshPending)
    {
        if (completion) mCosmeticsReadyCallback = completion;
        return;
    }
    mCosmeticsRefreshPending = true;
    const QString session = mLiveToken;
    auto finish = [this, completion] {
        mCosmeticsRefreshPending = false;
        if (completion) completion();
        if (mCosmeticsReadyCallback)
        {
            std::function<void()> waiting = std::move(mCosmeticsReadyCallback);
            mCosmeticsReadyCallback = std::function<void()>();
            waiting();
        }
    };

    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/me/cosmetics")};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + session.toUtf8());
    request.setTransferTimeout(10000);
    QNetworkReply* metadataReply = mLiveNetwork->get(request);
    connect(metadataReply, &QNetworkReply::finished, this, [this, metadataReply, session, finish] {
        const int status = metadataReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject metadata = QJsonDocument::fromJson(metadataReply->readAll()).object();
        metadataReply->deleteLater();
        if (session != mLiveToken) { finish(); return; }
        const qint64 userId = metadata.value("user_id").toVariant().toLongLong();
        if (status != 200 || userId < 1)
        {
            if (status == 401)
            {
                mLiveToken.clear(); mLiveName.clear(); mLiveUserId = 0;
                SaveLiveToken(QString());
                mLiveButton->setText("Live Sign In");
                RefreshProfiles();
            }
            else mLog->AddLauncherMessage(LogLevel::Warn, "Could not load your Live skin and cape.");
            finish();
            return;
        }
        mLiveUserId = userId;
        const QString folder = LauncherPaths::DataDir() + "/live/cosmetics/" + QString::number(userId);
        if (!QDir().mkpath(folder)) { finish(); return; }
        const QString skinPath = folder + "/skin.png";
        const QString capePath = folder + "/cape.png";
        mLiveSkinCachePath = QFileInfo::exists(skinPath) ? skinPath : QString();
        mLiveCapeCachePath = QFileInfo::exists(capePath) ? capePath : QString();
        const bool hasSkin = metadata.value("skin").toBool();
        const bool hasCape = metadata.value("cape").toBool();
        auto remaining = std::make_shared<int>(2);
        auto oneFinished = [this, remaining, finish] {
            if (--(*remaining) == 0) finish();
        };
        const auto download = [this, session, userId, oneFinished](const QString& kind, const QString& path, bool present) {
            const bool skinAsset = kind == "skin";
            if (!present)
            {
                QFile::remove(path);
                if (skinAsset) mLiveSkinCachePath.clear();
                else mLiveCapeCachePath.clear();
                oneFinished();
                return;
            }
            QNetworkRequest assetRequest{QUrl(QString("https://api.lcereborn.xyz/v1/users/%1/%2").arg(userId).arg(kind))};
            assetRequest.setTransferTimeout(10000);
            QNetworkReply* assetReply = mLiveNetwork->get(assetRequest);
            connect(assetReply, &QNetworkReply::finished, this, [this, assetReply, session, path, skinAsset, oneFinished] {
                const int assetStatus = assetReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray bytes = assetReply->readAll();
                assetReply->deleteLater();
                if (session == mLiveToken && assetStatus == 200 && !bytes.isEmpty())
                {
                    QSaveFile file(path);
                    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())
                    {
                        if (skinAsset) mLiveSkinCachePath = path;
                        else mLiveCapeCachePath = path;
                    }
                }
                oneFinished();
            });
        };
        download("skin", skinPath, hasSkin);
        download("cape", capePath, hasCape);
    });
}

QWidget* MainWindow::CreateLiveFriendsPage()
{
    QWidget* page = new QWidget(this);
    page->setFont(GameFont(15));
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    QLabel* heading = new QLabel("LCE Live Friends", page);
    heading->setFont(GameFont(22));
    heading->setStyleSheet("color:#ffffff; background:rgba(20,18,18,210); padding:7px 10px; border-left:4px solid #bd3434;");
    layout->addWidget(heading);
    QHBoxLayout* add = new QHBoxLayout;
    mFriendName = new QLineEdit(page);
    mFriendName->setPlaceholderText("Player username");
    mFriendName->setMaxLength(20);
    mFriendName->setFont(GameFont(15));
    mFriendName->setStyleSheet("QLineEdit { background:rgba(20,18,18,235); color:#ffffff; border:2px solid #8f3434; padding:8px; selection-background-color:#bd3434; }");
    GameButton* send = new GameButton("Add Friend", page);
    GameButton* refresh = new GameButton("Refresh", page);
    send->SetPixelSize(15); refresh->SetPixelSize(15);
    send->SetReadableFont(false); refresh->SetReadableFont(false);
    send->setFixedSize(135, 40); refresh->setFixedSize(135, 40);
    add->addWidget(mFriendName, 1); add->addWidget(send); add->addWidget(refresh);
    layout->addLayout(add);
    GameButton* browserJoins = new GameButton("Enable browser joins", page);
    browserJoins->SetPixelSize(14);
    browserJoins->setToolTip("Allow lcereborn.xyz to open this launcher. Useful for portable copies.");
    connect(browserJoins, &GameButton::clicked, this, [this] {
        const bool ok = LiveJoin::RegisterBrowserHandler();
        QMessageBox::information(this, "LCE Live", ok
            ? "Browser joins enabled for this launcher. Keep this portable folder in the same location."
            : "Could not register browser joins. On Windows, try installing the Live client.");
    });
    layout->addWidget(browserJoins, 0, Qt::AlignLeft);
    mFriendsMessage = new QLabel(page);
    mFriendsMessage->setWordWrap(true);
    mFriendsMessage->setFont(GameFont(14));
    mFriendsMessage->setStyleSheet("color:#ffffff; background:rgba(20,18,18,220); padding:5px 8px;");
    layout->addWidget(mFriendsMessage);
    QScrollArea* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background:transparent; border:0; }");
    mFriendsList = new QWidget(scroll);
    mFriendsList->setStyleSheet("background:transparent;");
    QVBoxLayout* list = new QVBoxLayout(mFriendsList);
    list->setContentsMargins(0, 0, 6, 0);
    scroll->setWidget(mFriendsList);
    layout->addWidget(scroll, 1);
    connect(refresh, &GameButton::clicked, this, &MainWindow::RefreshLiveFriends);
    auto addFriend = [this] {
        const QString name = mFriendName->text().trimmed();
        if (name.isEmpty()) { mFriendsMessage->setText("Enter your friend's username."); return; }
        LiveFriendAction("POST", "/friends/requests", QJsonObject{{"username", name}});
    };
    connect(send, &GameButton::clicked, this, addFriend);
    connect(mFriendName, &QLineEdit::returnPressed, this, addFriend);
    return page;
}

void MainWindow::SendLivePresence()
{
    if (mLiveToken.isEmpty()) return;
    QString status = "launcher";
    QString activity;
    if (mRunner->IsRunning())
    {
        status = "game";
        QFile stateFile(mLiveDirectory + "/presence.json");
        if (stateFile.open(QIODevice::ReadOnly))
        {
            const QString reported = QJsonDocument::fromJson(stateFile.readAll()).object().value("activity").toString();
            if (QStringList{"battle", "tumble", "glide", "build"}.contains(reported))
            {
                status = "minigame";
                activity = reported;
            }
        }
    }
    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/presence")};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + mLiveToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(10000);
    const QByteArray body = QJsonDocument(QJsonObject{{"status", status}, {"activity", activity}}).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = mLiveNetwork->post(request, body);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void MainWindow::RefreshLiveFriends()
{
    if (mLiveToken.isEmpty()) { RenderLiveFriends(); return; }
    const QString session = mLiveToken;
    const QStringList paths = {"/friends", "/friends/requests", "/friends/requests/sent", "/servers"};
    for (int index = 0; index < paths.size(); ++index)
    {
        QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1" + paths[index])};
        request.setRawHeader("Authorization", QByteArray("Bearer ") + session.toUtf8());
        request.setTransferTimeout(10000);
        QNetworkReply* reply = mLiveNetwork->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, session, index] {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (session != mLiveToken) return;
            if (status != 200) {
                if (index == 3) { mLiveWorlds = {}; RenderLiveFriends(); }
                mFriendsMessage->setText("Could not refresh friends. Check your connection."); return;
            }
            if (index == 0) mFriends = data.value("friends").toArray();
            else if (index == 1)
            {
                mIncomingFriends = data.value("incoming").toArray();
                QSet<qint64> pendingIds;
                for (const QJsonValue& value : mIncomingFriends)
                {
                    const qint64 id = value.toObject().value("id").toVariant().toLongLong();
                    if (id > 0) pendingIds.insert(id);
                }
                mSeenFriendRequests.intersect(pendingIds);
                if (!mInvitePopup)
                {
                    for (const QJsonValue& value : mIncomingFriends)
                    {
                        const QJsonObject entry = value.toObject();
                        const qint64 id = entry.value("id").toVariant().toLongLong();
                        const QString username = entry.value("username").toString();
                        if (id <= 0 || username.isEmpty() || mSeenFriendRequests.contains(id)) continue;
                        mSeenFriendRequests.insert(id);
                        ShowLiveInvite(id, username, QString(), true);
                        break;
                    }
                }
            }
            else if (index == 2) mSentFriends = data.value("sent").toArray();
            else mLiveWorlds = data.value("servers").toArray();
            RenderLiveFriends();
        });
    }
}

void MainWindow::LiveFriendAction(const QByteArray& method, const QString& path, const QJsonObject& body)
{
    if (mLiveToken.isEmpty()) { mFriendsMessage->setText("Sign in to LCE Live to manage friends."); return; }
    const QString session = mLiveToken;
    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1" + path)};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + session.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(10000);
    QNetworkReply* reply = method == "DELETE" ? mLiveNetwork->deleteResource(request)
        : mLiveNetwork->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, path] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString error = QJsonDocument::fromJson(reply->readAll()).object().value("error").toString();
        reply->deleteLater();
        if (session != mLiveToken) return;
        if (status < 200 || status >= 300)
        {
            QString message = "Could not complete this action. Please try again.";
            if (error == "User not found") message = "Player not found.";
            else if (error == "Already friends") message = "You are already friends.";
            else if (error == "Request already sent") message = "Friend request already sent.";
            else if (error == "Accept the incoming request") message = "This player sent you a request. Accept it below.";
            else if (error == "Cannot add yourself") message = "You cannot add yourself.";
            mFriendsMessage->setText(message);
            return;
        }
        if (path == "/friends/requests") mFriendName->clear();
        mFriendsMessage->setText("Done.");
        RefreshLiveFriends();
    });
}

void MainWindow::RenderLiveFriends()
{
    if (!mFriendsList) return;
    QLayout* layout = mFriendsList->layout();
    while (QLayoutItem* item = layout->takeAt(0)) { delete item->widget(); delete item; }
    if (mLiveToken.isEmpty())
    {
        mFriendsMessage->setText("Sign in to LCE Live to see friends and requests.");
        return;
    }
    if (mFriendsMessage->text().startsWith("Sign in")) mFriendsMessage->clear();
    auto section = [this, layout](const QString& title, const QJsonArray& entries, int type) {
        QLabel* label = new QLabel(QString("%1 (%2)").arg(title).arg(entries.size()), mFriendsList);
        label->setFont(GameFont(18));
        label->setStyleSheet("color:#ffffff; background:rgba(18,16,16,225); border-left:4px solid #bd3434; font-weight:bold; padding:7px 9px; margin-top:8px;");
        layout->addWidget(label);
        if (entries.isEmpty())
        {
            QLabel* empty = new QLabel("Nothing here yet", mFriendsList);
            empty->setFont(GameFont(14));
            empty->setStyleSheet("color:#e1e1e1; background:rgba(18,16,16,195); padding:6px 10px;"); layout->addWidget(empty); return;
        }
        for (const QJsonValue& value : entries)
        {
            const QJsonObject entry = value.toObject();
            const qint64 id = entry.value("id").toVariant().toLongLong();
            if (id <= 0) continue;
            QWidget* row = new QWidget(mFriendsList);
            row->setStyleSheet("background:rgba(20,18,18,220); border-bottom:1px solid rgba(255,255,255,35);");
            QHBoxLayout* line = new QHBoxLayout(row); line->setContentsMargins(8, 5, 8, 5); line->setSpacing(10);
            QString name = entry.value("username").toString();
            QString presence;
            QString activityIcon;
            if (type == 0)
            {
                presence = "Offline";
                if (entry.value("online").toBool())
                {
                    const QString status = entry.value("presence").toString();
                    const QString activity = entry.value("activity").toString();
                    const QMap<QString, QString> games{{"battle", "Battle"}, {"tumble", "Tumble"}, {"glide", "Glide"}, {"build", "Build"}};
                    if (status == "game" || status == "minigame") presence = "Online | In Game";
                    else if (status == "website") presence = "Online | On Website";
                    else presence = "Online | In Launcher";
                    if (status == "minigame" && games.contains(activity)) {
                        presence += " | " + games.value(activity);
                        if (QStringList{"battle", "tumble", "glide"}.contains(activity)) activityIcon = ":/launcher/" + activity + ".png";
                    }
                }
            }
            QLabel* face = new QLabel(name.left(1).toUpper(), row);
            face->setAlignment(Qt::AlignCenter);
            face->setFixedSize(36, 36);
            face->setFont(GameFont(16));
            face->setStyleSheet("color:#ffffff; background:#55504d; border:2px solid #a83232;");
            LoadLiveFriendFace(id, face);
            line->addWidget(face);
            QVBoxLayout* words = new QVBoxLayout;
            words->setSpacing(3);
            QLabel* player = new QLabel(name, row); player->setFont(GameFont(15)); player->setStyleSheet("color:#ffffff; background:transparent; border:0;");
            player->setTextFormat(Qt::PlainText);
            words->addWidget(player);
            if (!presence.isEmpty()) {
                QHBoxLayout* statusLine = new QHBoxLayout;
                statusLine->setSpacing(6);
                if (!activityIcon.isEmpty()) {
                    QLabel* icon = new QLabel(row);
                    icon->setFixedSize(24, 24);
                    icon->setPixmap(QPixmap(activityIcon).scaled(24, 24, Qt::KeepAspectRatio, Qt::FastTransformation));
                    icon->setStyleSheet("background:transparent; border:0;");
                    statusLine->addWidget(icon);
                }
                QLabel* status = new QLabel(presence, row);
                status->setFont(GameFont(12)); status->setWordWrap(true);
                status->setStyleSheet("color:#d8ded1; background:transparent; border:0;");
                statusLine->addWidget(status, 1);
                words->addLayout(statusLine);
            }
            line->addLayout(words, 1);
            auto button = [this, line, row, id](const QString& text, const QByteArray& method, const QString& path) {
                GameButton* action = new GameButton(text, row); action->SetPixelSize(14); action->SetReadableFont(false); action->setFixedSize(95, 36);
                line->addWidget(action);
                connect(action, &GameButton::clicked, this, [this, id, method, path] { LiveFriendAction(method, path.arg(id)); });
            };
            if (type == 0) {
                bool joinable = false;
                for (const auto& world : mLiveWorlds) {
                    const auto listing = world.toObject();
                    if (listing.value("owner_id").toVariant().toLongLong() == id
                        && (listing.value("direct").isObject() || listing.value("relay").isObject())) joinable = true;
                }
                if (joinable && entry.value("online").toBool()) {
                    GameButton* join = new GameButton("Join", row);
                    join->SetPixelSize(14); join->setFixedSize(85, 36);
                    line->addWidget(join);
                    connect(join, &GameButton::clicked, this, [this, id] { JoinLiveWorld(id); });
                }
                button("Remove", "DELETE", "/friends/%1");
            }
            else if (type == 1) {
                button("Accept", "POST", "/friends/requests/%1/accept");
                button("Decline", "DELETE", "/friends/requests/%1");
            } else button("Cancel", "DELETE", "/friends/requests/sent/%1");
            layout->addWidget(row);
        }
    };
    section("Friends", mFriends, 0);
    section("Incoming requests", mIncomingFriends, 1);
    section("Sent requests", mSentFriends, 2);
    static_cast<QVBoxLayout*>(layout)->addStretch(1);
}

void MainWindow::LoadLiveFriendFace(qint64 userId, QLabel* target)
{
    if (userId <= 0 || !target) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (mFriendFaceFetchedAt.value(userId, 0) + 180 > now)
    {
        const QPixmap cached = mFriendFaceCache.value(userId);
        if (!cached.isNull()) { target->setText(QString()); target->setPixmap(cached); }
        return;
    }
    if (mFriendFacesPending.contains(userId)) return;

    mFriendFacesPending.insert(userId);
    QNetworkRequest request{QUrl(QString("https://api.lcereborn.xyz/v1/users/%1/skin").arg(userId))};
    request.setTransferTimeout(8000);
    QNetworkReply* reply = mLiveNetwork->get(request);
    QPointer<QLabel> safeTarget(target);
    connect(reply, &QNetworkReply::finished, this, [this, reply, safeTarget, userId] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();
        mFriendFacesPending.remove(userId);
        mFriendFaceFetchedAt.insert(userId, QDateTime::currentSecsSinceEpoch());

        QPixmap facePixmap;
        if (status == 200)
        {
            const QImage skin = QImage::fromData(bytes).convertToFormat(QImage::Format_ARGB32);
            if (!skin.isNull() && skin.width() >= 64 && skin.height() >= 16 && skin.width() % 64 == 0)
            {
                const int scale = skin.width() / 64;
                if (skin.height() >= 16 * scale)
                {
                    QImage face = skin.copy(8 * scale, 8 * scale, 8 * scale, 8 * scale);
                    QPainter painter(&face);
                    painter.drawImage(0, 0, skin.copy(40 * scale, 8 * scale, 8 * scale, 8 * scale));
                    painter.end();
                    facePixmap = QPixmap::fromImage(face.scaled(36, 36, Qt::IgnoreAspectRatio, Qt::FastTransformation));
                }
            }
        }
        mFriendFaceCache.insert(userId, facePixmap);
        if (safeTarget && !facePixmap.isNull())
        {
            safeTarget->setText(QString());
            safeTarget->setPixmap(facePixmap);
        }
    });
}

void MainWindow::PollLiveInvites()
{
    if (mLiveToken.isEmpty() || mInvitePollPending || mInvitePopup) return;
    mInvitePollPending = true;
    const QString session = mLiveToken;
    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/game-invites")};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + session.toUtf8());
    request.setTransferTimeout(10000);
    QNetworkReply* reply = mLiveNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session] {
        mInvitePollPending = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonArray invitations = QJsonDocument::fromJson(reply->readAll()).object().value("invitations").toArray();
        reply->deleteLater();
        if (status != 200 || session != mLiveToken || mInvitePopup) return;
        for (const QJsonValue& value : invitations)
        {
            const QJsonObject invite = value.toObject();
            const qint64 id = invite.value("owner_id").toVariant().toLongLong();
            const QString owner = invite.value("owner").toString();
            const QString key = QString::number(id) + ":" + invite.value("expires_at").toVariant().toString();
            if (id <= 0 || owner.isEmpty() || mSeenInvites.contains(key)) continue;
            mSeenInvites.insert(key);
            ShowLiveInvite(id, owner, invite.value("name").toString());
            break;
        }
    });
}

void MainWindow::ShowLiveInvite(qint64 ownerId, const QString& owner, const QString& world, bool friendRequest)
{
    QFrame* toast = new QFrame(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setObjectName("liveInviteToast");
    toast->setFixedWidth(380);
    toast->setStyleSheet("QFrame#liveInviteToast { background:#36342f; border:2px solid #171614; border-radius:12px; }"
                         "QLabel { color:#e7e3d8; border:0; background:transparent; }"
                         "QLabel#liveInviteAvatar { background:#5a564e; border:2px solid #171614; border-radius:8px; "
                         "color:#f2e5c4; font-weight:bold; }");
    QVBoxLayout* layout = new QVBoxLayout(toast);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);
    toast->setFont(QFont("Segoe UI", 10));
    auto* header = new QHBoxLayout;
    header->setSpacing(12);
    auto* avatar = new QLabel(owner.left(1).toUpper(), toast);
    avatar->setObjectName("liveInviteAvatar");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(48, 48);
    LoadLiveFriendFace(ownerId, avatar);
    auto* message = new QVBoxLayout;
    message->setSpacing(4);
    QLabel* title = new QLabel(friendRequest ? "Friend request" : "Game invitation", toast);
    title->setStyleSheet("font-weight:bold; font-size:16px; color:#f2e5c4; border:0;");
    QLabel* detail = new QLabel(friendRequest ? QString("%1 wants to be your friend").arg(owner)
                                             : QString("%1 invited you to %2").arg(owner, world.isEmpty() ? "their world" : world), toast);
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    message->addWidget(title);
    message->addWidget(detail);
    header->addWidget(avatar, 0, Qt::AlignTop);
    header->addLayout(message, 1);
    QHBoxLayout* actions = new QHBoxLayout;
    GameButton* accept = new GameButton("Accept", toast);
    GameButton* decline = new GameButton("Decline", toast);
    accept->SetPixelSize(15); decline->SetPixelSize(15);
    accept->SetReadableFont(false); decline->SetReadableFont(false);
    accept->SetPrimary(true);
    accept->setFixedHeight(36); decline->setFixedHeight(36);
    actions->addWidget(accept); actions->addWidget(decline);
    layout->addLayout(header);
    layout->addLayout(actions);
    if (friendRequest)
    {
        const QString session = mLiveToken;
        auto answer = [this, session, ownerId, toast, detail, accept, decline](bool allow) {
            if (session != mLiveToken || session.isEmpty()) { toast->close(); return; }
            accept->setEnabled(false);
            decline->setEnabled(false);
            const QString path = QString("/friends/requests/%1").arg(ownerId) + (allow ? "/accept" : "");
            QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1" + path)};
            request.setRawHeader("Authorization", QByteArray("Bearer ") + session.toUtf8());
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setTransferTimeout(10000);
            QNetworkReply* reply = allow ? mLiveNetwork->post(request, QByteArray("{}"))
                                        : mLiveNetwork->deleteResource(request);
            QPointer<QWidget> safeToast = toast;
            QPointer<QLabel> safeDetail = detail;
            QPointer<GameButton> safeAccept = accept, safeDecline = decline;
            connect(reply, &QNetworkReply::finished, this, [this, reply, session, safeToast, safeDetail, safeAccept, safeDecline] {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                reply->deleteLater();
                if (session != mLiveToken) return;
                if ((status >= 200 && status < 300) || status == 404)
                {
                    if (safeToast) safeToast->close();
                    RefreshLiveFriends();
                }
                else
                {
                    if (safeDetail) safeDetail->setText("Could not update the request. Please try again.");
                    if (safeAccept) safeAccept->setEnabled(true);
                    if (safeDecline) safeDecline->setEnabled(true);
                }
            });
        };
        connect(accept, &GameButton::clicked, this, [answer] { answer(true); });
        connect(decline, &GameButton::clicked, this, [answer] { answer(false); });
    }
    else
    {
        connect(accept, &GameButton::clicked, this, [this, toast, title, detail, accept, decline, owner] {
            const bool running = mRunner->IsRunning();
            AcceptLiveInvite(owner);
            if (!running) { toast->close(); return; }
            title->setText("Connecting");
            detail->setText("If you are in a world, return to the main menu to join.");
            accept->hide(); decline->hide();
            QTimer::singleShot(5000, toast, &QWidget::close);
        });
        connect(decline, &GameButton::clicked, this, [this, toast, ownerId] {
            toast->close();
            QNetworkRequest request{QUrl(QString("https://api.lcereborn.xyz/v1/game-invites/%1").arg(ownerId))};
            request.setRawHeader("Authorization", QByteArray("Bearer ") + mLiveToken.toUtf8());
            request.setTransferTimeout(10000);
            QNetworkReply* reply = mLiveNetwork->deleteResource(request);
            connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        });
    }
    mInvitePopup = toast;
    toast->adjustSize();
    QPoint destination = toast->pos();
    if (QScreen* screen = QGuiApplication::primaryScreen())
    {
        const QRect available = screen->availableGeometry();
        destination = QPoint(available.right() - toast->width() - 16, available.bottom() - toast->height() - 16);
        toast->move(available.right() + 4, destination.y());
    }
    PlayLiveInviteSound(this);
    toast->show();
    toast->raise();
    QPropertyAnimation* animation = new QPropertyAnimation(toast, "pos", toast);
    animation->setDuration(400);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(toast->pos());
    animation->setEndValue(destination);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::JoinLiveWorld(qint64 ownerId)
{
    if (ownerId <= 0 || ownerId > 9007199254740991LL) return;
    showNormal(); raise(); activateWindow();
    ShowPage(FriendsPage);
    ++mLiveJoinGeneration;
    mPendingLiveJoinOwner.clear();
    mPendingLiveJoinId = ownerId;
    ContinueLiveJoin();
}

void MainWindow::ContinueLiveJoin()
{
    if (!mPendingLiveJoinId || mLiveSessionRestoring || mLiveJoinRequestPending) return;
    if (mLiveToken.isEmpty()) {
        mFriendsMessage->setText("Sign in to Live to join this world. The join request will resume after sign-in.");
        if (mLiveButton->isEnabled()) LiveAccount();
        return;
    }
    const qint64 ownerId = mPendingLiveJoinId;
    if (ownerId == mLiveUserId) {
        mPendingLiveJoinId = 0;
        mFriendsMessage->setText("This is your own world.");
        return;
    }
    mLiveJoinRequestPending = true;
    const QString session = mLiveToken;
    QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/servers")};
    request.setRawHeader("Authorization", QByteArray("Bearer ") + session.toUtf8());
    request.setTransferTimeout(10000);
    QNetworkReply* reply = mLiveNetwork->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, session, ownerId] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonArray worlds = QJsonDocument::fromJson(reply->readAll()).object().value("servers").toArray();
        reply->deleteLater();
        mLiveJoinRequestPending = false;
        if (session != mLiveToken || ownerId != mPendingLiveJoinId) { ContinueLiveJoin(); return; }
        if (status == 401) {
            mLiveToken.clear(); mLiveName.clear(); mLiveUserId = 0;
            SaveLiveToken(QString());
            mLiveButton->setText("Live Sign In");
            ContinueLiveJoin();
            return;
        }
        if (status != 200) {
            mPendingLiveJoinId = 0;
            QMessageBox::warning(this, "LCE Live", "Could not check access to this world. Check your connection and Live sign-in.");
            return;
        }
        for (const auto& value : worlds) {
            const QJsonObject world = value.toObject();
            if (world.value("owner_id").toVariant().toLongLong() == ownerId
                && !world.value("owner").toString().isEmpty()
                && (world.value("direct").isObject() || world.value("relay").isObject())) {
                mPendingLiveJoinId = 0;
                AcceptLiveInvite(world.value("owner").toString());
                return;
            }
        }
        mPendingLiveJoinId = 0;
        QMessageBox::information(this, "LCE Live", "This world is no longer available, or this account does not have permission to join it.");
    });
}

void MainWindow::AcceptLiveInvite(const QString& owner)
{
    if (mLiveToken.isEmpty()) return;
    if (!mRunner->IsRunning()) {
        const QString problem = GameRunner::CheckGameReady();
        if (!problem.isEmpty()) { QMessageBox::information(this, "LCE Live", problem); return; }
        if (!mStore.Selected()) { QMessageBox::information(this, "LCE Live", "Choose a game profile before joining."); return; }
    }
    mPendingLiveJoinOwner = owner;
    if (mRunner->IsRunning()) {
        mFriendsMessage->setText("Joining " + owner + ". If you are in a world, return to the game's main menu.");
        WaitForLiveJoin(owner);
    } else Play();
}

void MainWindow::WaitForLiveJoin(const QString& owner, int attempt, quint64 generation)
{
    if (generation == 0) generation = ++mLiveJoinGeneration;
    if (generation != mLiveJoinGeneration || mLiveToken.isEmpty()) return;
    if (!mRunner->IsRunning() || mLiveDirectory.isEmpty()) return;
    QFile file(mLiveDirectory + "/directory.json");
    if (file.open(QIODevice::ReadOnly))
    {
        const QJsonArray servers = QJsonDocument::fromJson(file.readAll()).array();
        for (const QJsonValue& value : servers)
        {
            const QJsonObject server = value.toObject();
            const int port = server.value("port").toInt();
            if (server.value("owner").toString().compare(owner, Qt::CaseInsensitive) != 0 || port < 1 || port > 65535) continue;
            QSaveFile command(mLiveDirectory + "/join.txt");
            if (command.open(QIODevice::WriteOnly) && command.write(QByteArray::number(port)) > 0 && command.commit()) {
                mPendingLiveJoinOwner.clear();
                mLog->AddLauncherMessage(LogLevel::Info, "Live join queued for " + owner + ". Return to the main menu if a world is open.");
                return;
            }
            break;
        }
    }
    if (attempt < 60) QTimer::singleShot(1000, this, [this, owner, attempt, generation] { WaitForLiveJoin(owner, attempt + 1, generation); });
    else {
        mPendingLiveJoinOwner.clear();
        QMessageBox::warning(this, "LCE Live", QString("%1's world is unavailable. Try opening the Join list.").arg(owner));
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (mShellLayout == nullptr)
        return;
    const int sides = qMax(16, width() * 60 / 1280);
    mShellLayout->setContentsMargins(sides, qMax(12, height() * 22 / 720), sides, qMax(12, height() * 16 / 720));
}

void MainWindow::ShowProfileMenu()
{
    auto* popup = new ProfilePopup(mStore, this);
    connect(popup, &ProfilePopup::ProfileChosen, this, [this](const QString& id) {
        mStore.Select(id);
        SaveProfiles();
    });
    connect(popup, &ProfilePopup::NewRequested, this,
            [this] { ProfileDialog::Run(this, mStore, mStore.MakeNew(ProfileStore::DefaultUsername()), true); });
    connect(popup, &ProfilePopup::EditRequested, this, [this] {
        if (const Profile* profile = mStore.Selected())
            ProfileDialog::Run(this, mStore, *profile, false);
    });
    connect(popup, &ProfilePopup::ManageRequested, this, [this] { ShowPage(ProfilesPage); });
    popup->ShowAbove(mProfileButton);
}

void MainWindow::RefreshProfiles()
{
    if (const Profile* profile = mStore.Selected())
    {
        mProfileButton->setText(mLiveName.isEmpty() ? profile->username : mLiveName);
        mProfileButton->SetSubtitle(mLiveName.isEmpty() ? profile->name : QString("LCE Live Â· %1").arg(profile->name));
        mProfileButton->SetIcon(Skins::FaceFor(*profile, kSlotIconSize), true);
        mProfileButton->setToolTip(QString("Playing as %1 with the profile %2").arg(profile->username, profile->name));
        mLog->SetLogsFolder(ProfileStore::GameDirFor(*profile) + "/logs");
    }
    RefreshStatus();
}

void MainWindow::SetStatus(const QString& text, const QColor& colour)
{
    mStatus->SetText(text);
    mStatus->SetColour(colour);
    mStatus->setToolTip(text);
}

void MainWindow::RefreshStatus()
{
    const Profile* profile = mStore.Selected();
    const QString who = profile != nullptr ? QString("Play as %1").arg(mLiveName.isEmpty() ? profile->username : mLiveName) : QString();
    mTabs->SetMarked(LogPage, mRunner->IsRunning());
    if (mRunner->IsRunning())
    {
        mPlay->setEnabled(false);
        mPlay->SetLabels("Playing", "The game is running");
        SetStatus("The game is running. Its output is in the Game Log.", GameColours::kText);
        return;
    }
    const QString problem = GameRunner::CheckGameReady();
    if (!problem.isEmpty())
    {
        mPlay->setEnabled(false);
        mPlay->SetLabels("Play", problem);
        SetStatus(problem, GameColours::kWarning);
        return;
    }
    mPlay->setEnabled(true);
    mPlay->SetLabels("Play", who);
    SetStatus("Ready to play", GameColours::kReady);
}

void MainWindow::Play()
{
    if (mGameLaunchPending || mRunner->IsRunning()) return;
    if (mLiveToken.isEmpty()) { StartGame(); return; }
    mGameLaunchPending = true;
    mPlay->setEnabled(false);
    const QString session = mLiveToken;
    RefreshLiveCosmetics([this, session] {
        mGameLaunchPending = false;
        mPlay->setEnabled(true);
        if (session != mLiveToken) { mPendingLiveJoinOwner.clear(); return; }
        StartGame();
    });
}

void MainWindow::StartGame()
{
    const QString problem = GameRunner::CheckGameReady();
    if (!problem.isEmpty())
    {
        mLog->AddLauncherMessage(LogLevel::Warn, problem);
        RefreshStatus();
        return;
    }
    const Profile* profile = mStore.Selected();
    if (profile == nullptr)
        return;

    Profile activeProfile = *profile;
    if (!mLiveName.isEmpty())
        activeProfile.username = mLiveName;
    QString liveSkinPath;
    QString liveCapePath;
    if (!mLiveToken.isEmpty() && mLiveUserId > 0)
    {
        const QString liveAssetDir = ProfileStore::GameDirFor(activeProfile) + "/live";
        if (QDir().mkpath(liveAssetDir))
        {
            const QString localSkin = liveAssetDir + "/skin.png";
            const QString localCape = liveAssetDir + "/cape.png";
            QFile::remove(localSkin);
            QFile::remove(localCape);
            if (!mLiveSkinCachePath.isEmpty() && QFile::copy(mLiveSkinCachePath, localSkin))
                liveSkinPath = "live/skin.png";
            if (!mLiveCapeCachePath.isEmpty() && QFile::copy(mLiveCapeCachePath, localCape))
                liveCapePath = "live/cape.png";
        }
    }
    const QString packsDir = ProfileStore::GameDirFor(activeProfile) + "/packs";
    const QString livePackPath = packsDir + "/LCE-Live-Cosmetics.pck";
    QByteArray skinBytes;
    QByteArray capeBytes;
    if (!mLiveSkinCachePath.isEmpty())
    {
        QFile skinFile(mLiveSkinCachePath);
        if (skinFile.open(QIODevice::ReadOnly)) skinBytes = skinFile.readAll();
    }
    if (!mLiveCapeCachePath.isEmpty())
    {
        QFile capeFile(mLiveCapeCachePath);
        if (capeFile.open(QIODevice::ReadOnly)) capeBytes = capeFile.readAll();
    }
    const QByteArray livePack = BuildLiveCosmeticPck(mLiveUserId, activeProfile.username, skinBytes, capeBytes);
    if (!livePack.isEmpty() && QDir().mkpath(packsDir))
    {
        QSaveFile file(livePackPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(livePack) != livePack.size() || !file.commit())
            mLog->AddLauncherMessage(LogLevel::Warn, "Could not create the Live skin and cape PCK.");
    }
    else
    {
        QFile::remove(livePackPath);
        if ((!skinBytes.isEmpty() || !capeBytes.isEmpty()) && !QDir(packsDir).exists())
            mLog->AddLauncherMessage(LogLevel::Warn, "Could not create the game's packs folder for Live cosmetics.");
    }
    mLog->AddLauncherMessage(LogLevel::Info, QString("Starting the game as %1 with the profile %2.")
                                                 .arg(activeProfile.username, activeProfile.name));
    QString error;
    QString liveDirectory;
    if (!mLiveToken.isEmpty())
    {
        if (mLiveBridge && mLiveBridge->state() != QProcess::NotRunning)
        {
            QMessageBox::warning(this, "Live", "The previous Live session is still closing. Try again in a moment.");
            return;
        }
        const QString livePath = QCoreApplication::applicationDirPath() + "/live";
        QString node = livePath + "/node.exe";
        if (!QFileInfo::exists(node)) node = QStandardPaths::findExecutable("node");
        const QString script = livePath + "/game-session.js";
        if (node.isEmpty() || !QFileInfo::exists(script))
        {
            QMessageBox::warning(this, "Live", "Live networking files are missing. Reinstall the Live client.");
            return;
        }
        QTemporaryDir* directory = new QTemporaryDir(QDir::tempPath() + "/lcereborn-live-XXXXXX");
        if (!directory->isValid())
        {
            delete directory;
            QMessageBox::warning(this, "Live", "Could not create a temporary Live session.");
            return;
        }
        liveDirectory = directory->path();
        mLiveDirectory = liveDirectory;
        QProcess* bridge = new QProcess(this);
        mLiveBridge = bridge;
        connect(bridge, &QProcess::readyReadStandardError, this, [this, bridge] {
            const QString message = QString::fromUtf8(bridge->readAllStandardError()).trimmed();
            if (!message.isEmpty()) mLog->AddLauncherMessage(LogLevel::Warn, message);
        });
        connect(bridge, &QProcess::finished, this, [this, bridge, directory](int, QProcess::ExitStatus) {
            if (mLiveBridge == bridge) { mLiveBridge = nullptr; mLiveDirectory.clear(); }
            delete directory;
            bridge->deleteLater();
        });
        bridge->setProgram(node);
        bridge->setArguments(QStringList() << script);
        bridge->start();
        if (!bridge->waitForStarted(5000))
        {
            mLiveBridge = nullptr;
            mLiveDirectory.clear();
            delete directory;
            bridge->deleteLater();
            QMessageBox::warning(this, "Live", "Could not start Live networking.");
            return;
        }
        const QJsonObject init{{"apiUrl", "https://api.lcereborn.xyz"}, {"token", mLiveToken},
                               {"username", mLiveName}, {"directory", liveDirectory}};
        bridge->write(QJsonDocument(init).toJson(QJsonDocument::Compact) + '\n');
        QTimer* inviteTimer = new QTimer(bridge);
        inviteTimer->setInterval(1000);
        connect(inviteTimer, &QTimer::timeout, this, [this, inviteTimer, liveDirectory] {
            const QString inviteFile = liveDirectory + "/invite.txt";
            if (inviteTimer->property("busy").toBool() || !QFile::exists(inviteFile)) return;
            QFile::remove(inviteFile);
            if (mLiveToken.isEmpty()) return;
            inviteTimer->setProperty("busy", true);
            QNetworkRequest request{QUrl("https://api.lcereborn.xyz/v1/friends")};
            request.setRawHeader("Authorization", QByteArray("Bearer ") + mLiveToken.toUtf8());
            request.setTransferTimeout(10000);
            QNetworkReply* reply = mLiveNetwork->get(request);
            QPointer<QTimer> safeTimer(inviteTimer);
            connect(reply, &QNetworkReply::finished, this, [this, reply, safeTimer] {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QJsonArray friends = QJsonDocument::fromJson(reply->readAll()).object().value("friends").toArray();
                reply->deleteLater();
                if (!safeTimer) return;
                QStringList names;
                for (const QJsonValue& value : friends) names << value.toObject().value("username").toString();
                names.removeAll(QString());
                if (status != 200 || names.isEmpty())
                {
                    safeTimer->setProperty("busy", false);
                    QMessageBox::information(this, "LCE Live", status == 200 ? "You have no friends to invite." : "Could not load your friends.");
                    return;
                }
                bool accepted = false;
                const QString name = QInputDialog::getItem(this, "LCE Live", "Invite a friend to your world:", names,
                                                           0, false, &accepted, Qt::WindowStaysOnTopHint);
                if (!safeTimer) return;
                safeTimer->setProperty("busy", false);
                if (!accepted || name.isEmpty()) return;
                QNetworkRequest invite{QUrl("https://api.lcereborn.xyz/v1/servers/mine/invites")};
                invite.setRawHeader("Authorization", QByteArray("Bearer ") + mLiveToken.toUtf8());
                invite.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                invite.setTransferTimeout(10000);
                QNetworkReply* sent = mLiveNetwork->post(invite, QJsonDocument(QJsonObject{{"username", name}}).toJson(QJsonDocument::Compact));
                connect(sent, &QNetworkReply::finished, this, [this, sent, name] {
                    const int status = sent->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    sent->deleteLater();
                    QMessageBox::information(this, "LCE Live", status == 201
                        ? QString("Invitation sent to %1.").arg(name) : QString("Could not send the invitation. Check that your online world is open."));
                });
            });
        });
        inviteTimer->start();
    }
    // An AppImage's files go away when the launcher exits, so there the launcher waits hidden for the game instead.
    if (profile->afterLaunch == AfterLaunch::Close && !LauncherPaths::RunsFromAppImage() && mLiveToken.isEmpty())
    {
        if (!mRunner->StartDetached(activeProfile, &error))
        {
            mLog->AddLauncherMessage(LogLevel::Error, error);
            QMessageBox::warning(this, "Play", error);
            return;
        }
        close();
        return;
    }
    ShowPage(LogPage);
    if (!mRunner->Start(activeProfile, &error, liveDirectory,
                        mLiveUserId > 0 ? QString::number(mLiveUserId) : QString(), liveSkinPath, liveCapePath))
    {
        if (mLiveBridge) mLiveBridge->closeWriteChannel();
        mLog->AddLauncherMessage(LogLevel::Error, error);
        QMessageBox::warning(this, "Play", error);
        RefreshStatus();
    }
}

void MainWindow::OnGameStarted()
{
    if (!mPendingLiveJoinOwner.isEmpty()) WaitForLiveJoin(mPendingLiveJoinOwner);
    RefreshStatus();
    const Profile* profile = mStore.Selected();
    if (profile != nullptr && (profile->afterLaunch == AfterLaunch::Hide || profile->afterLaunch == AfterLaunch::Close))
    {
        mHiddenForGame = true;
        mCloseAfterGame = profile->afterLaunch == AfterLaunch::Close;
        hide();
    }
}

void MainWindow::OnGameFinished(int exitCode, bool crashed)
{
    ++mLiveJoinGeneration;
    mPendingLiveJoinOwner.clear();
    if (mLiveBridge) mLiveBridge->closeWriteChannel();
    if (crashed || exitCode != 0)
        mLog->AddLauncherMessage(LogLevel::Error, crashed ? QString("The game crashed.")
                                                          : QString("The game exited with code %1.").arg(exitCode));
    else
        mLog->AddLauncherMessage(LogLevel::Info, "The game closed.");
    if (mCloseAfterGame && !crashed && exitCode == 0)
    {
        if (mLiveBridge && mLiveBridge->state() != QProcess::NotRunning)
        {
            connect(mLiveBridge, &QProcess::finished, qApp, &QApplication::quit);
            QTimer::singleShot(12000, qApp, &QApplication::quit);
        }
        else QApplication::quit();
        return;
    }
    mCloseAfterGame = false;
    if (mHiddenForGame)
    {
        mHiddenForGame = false;
        show();
        raise();
        activateWindow();
    }
    if (crashed || exitCode != 0)
        ShowPage(LogPage);
    mStore.NotifySkinsChanged();
}

void MainWindow::OnImported()
{
    GameArt::Reload();
    QApplication::setFont(GameFont(16));
    for (QWidget* widget : QApplication::allWidgets())
    {
        QEvent fontChange(QEvent::ApplicationFontChange);
        QCoreApplication::sendEvent(widget, &fontChange);
        widget->updateGeometry();
    }
    mPanorama->Reload();
    mNews->Refresh();
    mStore.NotifySkinsChanged();
    mRoot->setCurrentIndex(1);
    QSettings(LauncherPaths::SettingsFile(), QSettings::IniFormat).setValue("notices/packsFolder", true);
    update();
}

void MainWindow::ShowPacksNotice()
{
    QSettings(LauncherPaths::SettingsFile(), QSettings::IniFormat).setValue("notices/packsFolder", true);
    QMessageBox notice(QMessageBox::Information, "Packs",
                       "DLC and skin packs no longer load from the game files. The packs that came free with the "
                       "game still load on their own, but packs you bought need adding again.\n\n"
                       "Copy the folders of the packs you own from content/WiiU/DLC in your update 688 dump into "
                       "the packs folder, and they load the next time you play.",
                       QMessageBox::NoButton, this);
    QPushButton* open = notice.addButton("Open Packs Folder", QMessageBox::ActionRole);
    notice.addButton(QMessageBox::Ok);
    notice.exec();
    const Profile* profile = mStore.Selected();
    if (notice.clickedButton() != open || profile == nullptr)
        return;
    const QString folder = ProfileStore::GameDirFor(*profile) + "/packs";
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MainWindow::SaveProfiles()
{
    QString error;
    if (!mStore.Save(&error))
        mLog->AddLauncherMessage(LogLevel::Error, error);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (mRunner->IsRunning())
    {
        const auto answer = QMessageBox::question(
            this, "Close the Launcher", "The game is still running. Closing the launcher closes the game too. Close both?");
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        mRunner->Stop();
    }
    if (mSetup->IsBusy())
    {
        const auto answer = QMessageBox::question(
            this, "Close the Launcher", "The game files are still being imported. Stop and close the launcher?");
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
    }
    QSettings settings(LauncherPaths::SettingsFile(), QSettings::IniFormat);
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/page", mPages->currentIndex());
    mLog->SaveSettings();
    SaveProfiles();
    event->accept();
}

