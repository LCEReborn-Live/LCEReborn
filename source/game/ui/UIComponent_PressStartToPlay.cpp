#include "game/ui/UIComponent_PressStartToPlay.hpp"
#include "game/core/StringIds.hpp"

#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "game/core/System.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "platform/pc/PcLiveBridge.hpp"
#include "game/ui/UILiveAvatar.hpp"
#include <string>
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
#include "platform/wiiu/WiiULiveSocial.hpp"
#endif
#if LCE_PLATFORM_PC && defined(_WIN32)
extern "C" unsigned int __stdcall mciSendStringW(const wchar_t*, wchar_t*, unsigned int, void*);
#pragma comment(lib, "winmm.lib")
#endif

#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Minecraft.hpp"
#include "game/ui/FJ_PressStartToPlay.hpp"
#include "game/ui/UILayer.hpp"
#include "game/core/StringUtils.hpp"
#include "game/core/StringConversion.hpp"
#include "game/ui/UIString.hpp"
#include "game/ui/UIScene_AchievementsMenu.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "platform/CProfile.hpp"

namespace
{
#if LCE_PLATFORM_PC && defined(_WIN32)
void PlayLiveToastSound()
{
    const wchar_t* liveDir = _wgetenv(L"LCE_LIVE_DIR");
    if (!liveDir || !*liveDir) return;
    std::wstring path(liveDir);
    if (path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += L"xbox-360-achievement-sound.mp3";
    mciSendStringW(L"close LCEGameToastSound", NULL, 0, NULL);
    const std::wstring open = L"open \"" + path + L"\" type mpegvideo alias LCEGameToastSound";
    if (mciSendStringW(open.c_str(), NULL, 0, NULL) == 0)
        mciSendStringW(L"play LCEGameToastSound", NULL, 0, NULL);
}
#endif
struct AvatarRequest
{
    AvatarRequest(UIComponent_PressStartToPlay* scene, int player, int achievement)
        : scene(scene),
          player(player),
          achievement(achievement)
    {
    }

    UIComponent_PressStartToPlay* scene;
    int player;
    int achievement;
};
}

UIComponent_PressStartToPlay::UIComponent_PressStartToPlay(int type, void* data, UILayer* layer) : UIScene(type, layer)
{
    initialiseMovie();
    m_showSaveIcon = false;
    m_showAutosaveTimer = false;
    for (int i = 0; i < 4; ++i)
        m_showPressStart[i] = false;
    m_autosaveTimer = L"";
    m_timerLabel.init(UIString(L""));
    m_timerLabel.setVisible(false);
    m_player = 0;
    m_achievement = 0;
    m_holdToShowAchievements = false;
    m_holdActionCancelled = false;
    m_holdCancelPending = false;
    m_achievementToastShowing = false;
    SetText(UIString(L10N::GetString(IDS_PRESS_START_TO_JOIN)), false);
    m_saveIcon.setVisible(false);
    m_playerNameLabel.init(UIString(L""));
    m_mainPanel.setVisible(false);
    m_playerNameLabel.setVisible(false);
}

UIComponent_PressStartToPlay::~UIComponent_PressStartToPlay()
{
}

bool UIComponent_PressStartToPlay::GetAchievementToastShowing(int player)
{
    return ((!m_socialToastShowing && m_achievementToastShowing) || m_holdToShowAchievements) && player == m_player;
}

bool UIComponent_PressStartToPlay::GetHoldToShowAchievementsActive(bool includeCancelled)
{
    if (includeCancelled)
        m_holdCancelPending = includeCancelled;
    return m_holdToShowAchievements;
}

unsigned int UIComponent_PressStartToPlay::handleHold(int player, int action, bool repeat, bool pressed, bool released,
                                                      bool& handled)
{
    if (GetAchievementToastShowing(player)
        && (action == EControllerActions_Inventory || action == EControllerActions_MenuY) && pressed && !repeat)
    {
        m_holdToShowAchievements = true;
        m_holdActionCancelled = false;
        addTimer(2, 1000);
        handled = true;
        return 1;
    }
    if (m_holdToShowAchievements && !handled
        && (action == EControllerActions_Inventory || action == EControllerActions_MenuY) && released)
    {
        if (m_holdCancelPending)
        {
            m_holdCancelPending = false;
            m_holdActionCancelled = true;
        }
        m_holdToShowAchievements = false;
        killTimer(2);
        handled = false;
        return 0;
    }
    return 1;
}

void UIComponent_PressStartToPlay::HideChestRefillAnimation()
{
    if (hasMovie())
        static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->HideChestRefillAnimation();
}

void UIComponent_PressStartToPlay::ResetUIComponents()
{
    m_achievementToastShowing = false;
    HideChestRefillAnimation();
}

void UIComponent_PressStartToPlay::showSaveIcon(bool show)
{
    m_showSaveIcon = show;
    if (!gConsoleUIController.IsExpectingOrReloadingSkin() && hasMovie()
        && GameSettings::GetGameSettings(m_playerId, eGameSetting_DisplaySaveIcon))
        m_saveIcon.setVisible(show);
}

void UIComponent_PressStartToPlay::ShowChestRefillAnimation(bool show, int yOffset)
{
    if (hasMovie())
    {
        static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())
            ->ShowChestRefillAnimation(show, yOffset);
        addTimer(3, 2500);
    }
}

void UIComponent_PressStartToPlay::showPlayerDisplayName(bool show)
{
    m_playerNameLabel.setVisible(false);
}

void UIComponent_PressStartToPlay::SetText(UIString text, bool multiLine)
{
    if (!multiLine)
        static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->SetText(text.getString());
    else
        static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->SetMultiLineText(text.getString());
}

void UIComponent_PressStartToPlay::showPressStart(int player, bool show)
{
    if (gConsoleUIController.IsSceneInStack(0xff, EUIScene_MessageBox))
        return;
    m_showPressStart[player] = show;
    if (!gConsoleUIController.IsExpectingOrReloadingSkin() && hasMovie())
    {
        if (gApp.IsDrcPreventingSplitscreen())
            SetText(UIString(L10N::GetString(0x9a0686fb)), true);
        else
            SetText(UIString(L10N::GetString(IDS_PRESS_START_TO_JOIN)), false);
        m_mainPanel.setVisible(show);
        if (show)
        {
            addTimer(0, 3000);
            static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->ShowController(player, false);
        }
    }
}

void UIComponent_PressStartToPlay::registerTexture(int achievement)
{
    AchievementInfo* info = (AchievementInfo*)(uintptr_t)UIScene_AchievementsMenu::GetInfo(achievement);
    wchar_t path[64];
    std::swprintf(path, 64, L"Graphics\\Achievements\\%ls.png", info->icon);
    if (gApp.hasArchiveFile(std::wstring(path), false))
    {
        arrayWithLength<unsigned char> data = gApp.getArchiveFile(std::wstring(path), false);
        if (data.length != 0 && data.data != 0)
            registerSubstitutionTexture(std::wstring(info->icon), data.data, data.length, true);
    }
}

void UIComponent_PressStartToPlay::ShowAchievementToast(const Toast& toast)
{
#if LCE_PLATFORM_PC && defined(_WIN32)
    PlayLiveToastSound();
#endif
    addTimer(1, 4000);
    m_achievementToastShowing = true;
    m_socialToastShowing = toast.social;
    if (!toast.social) {
        if (toast.player >= 0) m_player = toast.player;
        if (toast.achievement >= 0) m_achievement = toast.achievement;
    }
    static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())
        ->ShowAchievementToast(toast.title, toast.description, toast.gamerPic, toast.achievementIcon, toast.social);
}

int UIComponent_PressStartToPlay::AvatarReturned(void* context, unsigned char* data, unsigned int size)
{
    AvatarRequest* request = static_cast<AvatarRequest*>(context);
    UIComponent_PressStartToPlay* scene = request->scene;
    int achievement = request->achievement;
    int player = request->player;
    delete request;

    if (data == 0 && size != 0)
    {
        scene->m_achievement = achievement;
        scene->m_player = player;
        std::wstring texture = L"PlayerIcon" + _toString<int>(player);
        scene->registerTexture(achievement);
        scene->registerSubstitutionTexture(texture, 0, size, true);
        AchievementInfo* info = (AchievementInfo*)(uintptr_t)UIScene_AchievementsMenu::GetInfo(achievement);
        std::wstring title = L10N::GetString(info->title);
        std::wstring text = L10N::GetString(0x4523129e);
        std::wstring description = gApp.FormatHTMLString(
            player, replaceAll(text, L"{*CONTROLLER_VK_X*}", L"{*CONTROLLER_VK_Y*}"), -1);
        Toast toast;
        toast.title = title;
        toast.description = description;
        toast.gamerPic = texture;
        toast.achievementIcon = info->icon;
        toast.player = player;
        toast.achievement = achievement;
        if (!scene->m_achievementToastShowing)
            scene->ShowAchievementToast(toast);
        else
            scene->m_achievementToasts.push_back(toast);
    }
    return 0;
}

void UIComponent_PressStartToPlay::SetAchievementUnlocked(int player, int achievement)
{
    gProfile->GetProfileAvatar(player, AvatarReturned, new AvatarRequest(this, player, achievement));
}

bool UIComponent_PressStartToPlay::GetHoldActionCancelled(int player)
{
    if (GetAchievementToastShowing(player))
    {
        bool cancelled = m_holdActionCancelled;
        m_holdActionCancelled = false;
        return cancelled;
    }
    return false;
}

unsigned int UIComponent_PressStartToPlay::mapElementsAndNames()
{
    UIScene::mapElementsAndNames();
    fuiRenderNode* root = m_movie->getRootNode();
    {
        m_timerLabel.setupControl(this, root, std::string("TrialTimer"));
        m_controls.push_back(&m_timerLabel);
    }
    {
        m_saveIcon.setupControl(this, root, std::string("SaveIcon"));
        m_controls.push_back(&m_saveIcon);
    }
    {
        m_playerNameLabel.setupControl(this, root, std::string("PlayerName"));
        m_controls.push_back(&m_playerNameLabel);
    }
    {
        m_mainPanel.setupControl(this, root, std::string("MainPanel"));
        m_controls.push_back(&m_mainPanel);
    }
    return 1;
}

std::wstring UIComponent_PressStartToPlay::getMoviePath()
{
    return std::wstring(L"PressStartToPlay");
}

void UIComponent_PressStartToPlay::handleReload()
{
    bool saveVisible = m_showSaveIcon && GameSettings::GetGameSettings(m_playerId, eGameSetting_DisplaySaveIcon);
    m_saveIcon.setVisible(saveVisible);
    m_timerLabel.setVisible(m_showAutosaveTimer);
    m_timerLabel.setLabel(UIString(m_autosaveTimer), false, false);
    SetText(UIString(L10N::GetString(IDS_PRESS_START_TO_JOIN)), false);
    bool any = false;
    for (int i = 0; i < 4; ++i)
    {
        bool show = m_showPressStart[i];
        any |= show;
        if (show)
        {
            addTimer(0, 3000);
            static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->ShowController(i, false);
        }
    }
    m_mainPanel.setVisible(any);
}

void UIComponent_PressStartToPlay::HideAchievementToast()
{
    m_achievementToastShowing = false;
    m_socialToastShowing = false;
    static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->HideAchievementToast();
    if (!m_achievementToasts.empty())
    {
        std::vector<Toast>::iterator it = m_achievementToasts.begin();
        ShowAchievementToast(*it);
        m_achievementToasts.erase(it);
    }
}

void UIComponent_PressStartToPlay::ShowAchievementMenu()
{
    m_holdToShowAchievements = false;
    m_achievementToastShowing = false;
    if (gApp.GetGameStarted() != 0)
        gConsoleUIController.CloseAllPlayersScenes(false);
    gConsoleUIController.NavigateToScene(m_player, EUIScene_AchievementsMenu,
                                         (void*)(uintptr_t)(unsigned int)m_achievement, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void UIComponent_PressStartToPlay::handleTimerComplete(int timerId)
{
    if (gConsoleUIController.IsExpectingOrReloadingSkin() || !hasMovie())
        return;
    if (timerId == 0)
    {
        m_mainPanel.setVisible(false);
        for (int i = 0; i < 4; ++i)
            m_showPressStart[i] = false;
        gConsoleUIController.ClearPressStart();
    }
    else if (timerId == 3)
    {
        HideChestRefillAnimation();
        killTimer(3);
    }
    else if (timerId == 1)
    {
        killTimer(1);
        HideAchievementToast();
    }
    else if (timerId == 2)
    {
        ShowAchievementMenu();
        killTimer(2);
    }
}

void UIComponent_PressStartToPlay::setAutosaveTimer(const std::wstring& timer)
{
    m_autosaveTimer = timer;
    if (!gConsoleUIController.IsExpectingOrReloadingSkin() && hasMovie())
        m_timerLabel.setLabel(UIString(timer), false, false);
}

void UIComponent_PressStartToPlay::showAutosaveTimer(bool show)
{
    m_showAutosaveTimer = show;
    if (!gConsoleUIController.IsExpectingOrReloadingSkin() && hasMovie())
        m_timerLabel.setVisible(show);
}

void UIComponent_PressStartToPlay::ShowBetaWatermark()
{
    if (hasMovie())
        static_cast<FJ_PressStartToPlay*>(m_movie->getRootNode()->getFuiNode())->ShowBetaWatermark();
}

void UIComponent_PressStartToPlay::tick()
{
    UIScene::tick();
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    if (hasMovie() && !gConsoleUIController.IsExpectingOrReloadingSkin()) PollLiveSocial();
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    if (hasMovie() && !gConsoleUIController.IsExpectingOrReloadingSkin() && !m_achievementToastShowing
        && !m_holdToShowAchievements && m_achievementToasts.empty())
    {
        Toast toast = {};
        if (WiiULiveSocial::TakeNotification(toast.title, toast.description))
        {
            toast.social = true;
            registerTexture(1);
            toast.achievementIcon = UIScene_AchievementsMenu::GetInfo(1)->icon;
            gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
            ShowAchievementToast(toast);
        }
    }
#endif
    Minecraft* minecraft = Minecraft::GetInstance();
    if (!gConsoleUIController.IsExpectingOrReloadingSkin() && hasMovie())
    {
        for (int i = 0; i < 4; ++i)
            if (m_showPressStart[i] && minecraft->mLocalPlayers[i] != 0)
                showPressStart(i, false);
    }
}

int UIComponent_PressStartToPlay::getSceneType()
{
    return EUIScene_PressStartToPlay;
}

bool UIComponent_PressStartToPlay::stealsFocus()
{
    return false;
}

bool UIComponent_PressStartToPlay::hasFocus(int sceneType)
{
    return false;
}

bool UIComponent_PressStartToPlay::hidesLowerScenes()
{
    return false;
}

void UIComponent_PressStartToPlay::PollLiveSocial()
{
    if (!PcLiveBridge::Enabled()) return;
    const long long now = System::processTimeInMilliSecs();
    if (now >= m_nextLivePoll)
    {
        m_nextLivePoll = now + 1000;
        FILE* events = PcLiveBridge::Open(L"social-events.tsv", L"rb");
        if (events)
        {
            char line[512];
            for (int row = 0; row < 32 && std::fgets(line, sizeof(line), events); ++row)
            {
                std::istringstream input(line);
                std::string sequence, kind, id, username, revision;
                if (!std::getline(input, sequence, '\t') || !std::getline(input, kind, '\t')
                    || !std::getline(input, id, '\t') || !std::getline(input, username, '\t')
                    || !std::getline(input, revision)) continue;
                const unsigned long long serial = std::strtoull(sequence.c_str(), NULL, 10);
                if (serial <= m_lastLiveNotification) continue;
                m_lastLiveNotification = serial;
                if ((kind != "R" && kind != "I") || username.empty() || username.size() > 20
                    || username.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos
                    || m_socialToasts.size() >= 8) continue;
                revision.erase(revision.find_last_not_of("\r\n") + 1);
                const std::wstring avatar = RegisterLiveFaceIcon(*this, static_cast<unsigned int>(std::strtoul(id.c_str(), NULL, 10)), revision);
                Toast toast;
                toast.social = true;
                toast.title = kind == "I" ? L"Game invitation" : L"Friend request";
                toast.description = std::wstring(username.begin(), username.end())
                    + (kind == "I" ? L" invited you to play" : L" wants to add you");
                toast.achievementIcon = avatar;
                if (avatar.empty()) { registerTexture(1); toast.achievementIcon = UIScene_AchievementsMenu::GetInfo(1)->icon; }
                m_socialToasts.push_back(toast);
            }
            std::fclose(events);
        }
        FILE* cloudAchievements = PcLiveBridge::Open(L"achievements-cloud.txt", L"rb");
        if (cloudAchievements)
        {
            char text[1024];
            const size_t size = std::fread(text, 1, sizeof(text) - 1, cloudAchievements);
            std::fclose(cloudAchievements);
            text[size] = '\0';
            std::string snapshot(text);
            snapshot.erase(snapshot.find_last_not_of(" \t\r\n") + 1);
            if (snapshot != m_lastCloudAchievementSnapshot)
            {
                m_lastCloudAchievementSnapshot = snapshot;
                if (!snapshot.empty())
                {
                    std::istringstream input(snapshot);
                    std::string value;
                    while (std::getline(input, value, ','))
                    {
                        char* end = NULL;
                        const long id = std::strtol(value.c_str(), &end, 10);
                        if (value.empty() || end == value.c_str() || *end != '\0' || id < 0 || id > 96)
                            continue;
                        if (!gProfile->IsAwardsFlagSet(0, static_cast<int>(id)))
                            gProfile->Award(0, static_cast<int>(id), false);
                    }
                }
            }
        }
        // Only the launcher's primary profile belongs to this Live account.
        // Never upload split-screen guest progress to the signed-in owner.
        std::string achievements;
        for (int index = 0; index < UIScene_AchievementsMenu::DISPLAY_COUNT; ++index)
        {
            const int id = UIScene_AchievementsMenu::sDisplayList[index];
            if (gProfile->IsAwardsFlagSet(0, id))
            {
                if (!achievements.empty()) achievements += ',';
                achievements += std::to_string(id);
            }
        }
        achievements += '\n';
        if (achievements != m_lastAchievementSnapshot)
        {
            FILE* file = PcLiveBridge::Open(L"achievements.tmp", L"wb");
            if (file)
            {
                const bool written = std::fwrite(achievements.data(), 1, achievements.size(), file) == achievements.size();
                std::fclose(file);
                if (written && PcLiveBridge::Rename(L"achievements.tmp", L"achievements.txt")) m_lastAchievementSnapshot = achievements;
            }
        }
    }
    // Existing achievement notifications always drain first. Social messages
    // use the same native animation without enabling the achievement shortcut.
    if (!m_achievementToastShowing && !m_holdToShowAchievements && m_achievementToasts.empty() && !m_socialToasts.empty())
    {
        ShowAchievementToast(m_socialToasts.front());
        m_socialToasts.erase(m_socialToasts.begin());
    }
}
