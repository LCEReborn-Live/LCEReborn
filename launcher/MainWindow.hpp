#ifndef LAUNCHER_MAINWINDOW_HPP
#define LAUNCHER_MAINWINDOW_HPP

#include <QMainWindow>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <functional>

#include "GameLog.hpp"

class GameButton;
class GameLogTab;
class GameRunner;
class NewsTab;
class QNetworkAccessManager;
class QProcess;
class QLineEdit;
class QLabel;
class Panorama;
class PlayButton;
class ProfileStore;
class QBoxLayout;
class QStackedWidget;
class SetupPage;
class ShadowLabel;
class TabbedPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ProfileStore& store, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ShowPage(int index);
    void ShowProfileMenu();
    void RefreshProfiles();
    void RefreshStatus();
    void Play();
    void StartGame();
    void RefreshLiveCosmetics(std::function<void()> completion = std::function<void()>());
    void LiveAccount();
    void RestoreLiveSession();
    void PollLiveInvites();
    void ShowLiveInvite(qint64 ownerId, const QString& owner, const QString& world);
    void AcceptLiveInvite(const QString& owner);
    void WaitForLiveJoin(const QString& owner, int attempt = 0);
    QWidget* CreateLiveFriendsPage();
    void RefreshLiveFriends();
    void RenderLiveFriends();
    void LoadLiveFriendFace(qint64 userId, QLabel* target);
    void LiveFriendAction(const QByteArray& method, const QString& path, const QJsonObject& body = QJsonObject());
    void SendLivePresence();
    void OnGameStarted();
    void OnGameFinished(int exitCode, bool crashed);
    void OnImported();
    void ShowPacksNotice();
    void SaveProfiles();
    void SetStatus(const QString& text, const QColor& colour);

    ProfileStore& mStore;
    GameRunner* mRunner;
    QNetworkAccessManager* mLiveNetwork;
    GameButton* mLiveButton;
    QString mLiveName;
    QString mLiveToken;
    qint64 mLiveUserId = 0;
    QString mLiveSkinCachePath;
    QString mLiveCapeCachePath;
    bool mCosmeticsRefreshPending = false;
    std::function<void()> mCosmeticsReadyCallback;
    QString mLiveDirectory;
    QSet<QString> mSeenInvites;
    QPointer<QWidget> mInvitePopup;
    bool mInvitePollPending = false;
    QWidget* mFriendsList = nullptr;
    QLineEdit* mFriendName = nullptr;
    QLabel* mFriendsMessage = nullptr;
    QJsonArray mFriends, mIncomingFriends, mSentFriends;
    QSet<qint64> mFriendFacesPending;
    QHash<qint64, QPixmap> mFriendFaceCache;
    QHash<qint64, qint64> mFriendFaceFetchedAt;
    QProcess* mLiveBridge = nullptr;
    Panorama* mPanorama;
    QStackedWidget* mRoot;
    QBoxLayout* mShellLayout;
    TabbedPanel* mTabs;
    QStackedWidget* mPages;
    SetupPage* mSetup;
    NewsTab* mNews;
    GameLogTab* mLog;
    GameButton* mProfileButton;
    PlayButton* mPlay;
    ShadowLabel* mStatus;
    bool mHiddenForGame = false;
    bool mCloseAfterGame = false;
};

#endif
