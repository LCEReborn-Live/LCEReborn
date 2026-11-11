#include "game/core/IUIScene_LoadCreateJoin.hpp"
#include "game/core/StringIds.hpp"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/render/TexturePack.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/UnityClient6.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/DLCManager.hpp"
#include "game/core/File.hpp"
#include "game/core/FileInputStream.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/GameType.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/StringFormat.hpp"
#include "game/network/CPlatformNetworkManager.hpp"
#include "game/minigame/MiniGameDef.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/network/CPlatformNetworkManager.hpp"
#include "game/ui/UIGroup.hpp"
#include "game/ui/UILayer.hpp"
#include "game/ui/UIScene_LoadCreateJoinMenu.hpp"
#include "game/world/LevelGenerationOptions.hpp"
#include "game/world/LevelRuleset.hpp"
#include "game/world/biome/arrayWithLength.hpp"
#include "game/world/generation/LevelGenerators.hpp"
#include "game/world/generation/Player.hpp"
#include "net/ServerList.hpp"
#include "platform/CProfile.hpp"
#include "platform/CStorage.hpp"
#include "game/ui/UIScene_EditServerMenu.hpp"
#include "game/ui/UIScene_FullscreenProgress.hpp"
#include "vendor/_LoadingInputParams.hpp"
#include "vendor/_SaveListDetails.hpp"
#include "vendor/_NetworkGameInitData.hpp"
#include "vendor/DiskSaveBuffer.hpp"

extern CConsoleMinecraftApp gApp;
extern ConsoleUIController gConsoleUIController;
extern C_4JProfile* gProfile;
extern C4JStorage* gStorage;

namespace
{

struct LoadMenuSceneData
{
    int playerId;
    int saveIndex;
    LevelGenerationOptions* generationOptions;
    _SaveListDetails* saveDetails;
    EMiniGameId gameMode;

    LoadMenuSceneData()
        : playerId(0),
          saveIndex(0),
          generationOptions(NULL),
          saveDetails(NULL),
          gameMode(eMiniGameId_Any)
    {
    }
};

}

IUIScene_LoadCreateJoin::IUIScene_LoadCreateJoin()
{
    m_saveInfoReady = false;
    m_friendSessions = NULL;
    m_joinSceneData = NULL;
    m_onlinePlayAllowed = false;
    m_texturePackWorldCount = 0;
    m_currentTab = 0;
    m_buttonCount = 0;
    m_sessionListMode = true;
    m_pressedList = 0;
    m_closing = false;
    m_saveListDetails = NULL;
    m_ignoreInput = false;
    m_saveDetails = NULL;
    m_partyGamesAvailable = false;
    m_selectedSaveIndex = 0;
    m_thumbnailsComplete = false;
    m_thumbnailRequested = false;
    m_partyGamesOnly = false;
    m_miniGames = false;
    m_selectedGameIndex = 0;
    m_pendingAction = ePendingAction_None;
    m_returnToHomeMenu = false;
    m_selectedCreateIndex = 0;
    m_saveListRefreshState = 0;
    m_nextThumbnail = 0;
    m_removeServerIndex = -1;
    m_removeServerText[0] = L'\0';
    InitializeCriticalSection(&m_lock);
}

IUIScene_LoadCreateJoin::~IUIScene_LoadCreateJoin()
{
    if (m_friendSessions != NULL)
    {
        std::vector<FriendSessionInfo*>::iterator it = m_friendSessions->begin();
        for (; it < m_friendSessions->end(); ++it)
        {
            FriendSessionInfo* session = *it;
            if (session != NULL)
            {
                if (session->m_displayName != NULL)
                    delete session->m_displayName;
                delete session;
            }
        }
    }
    delete m_friendSessions;
    delete m_joinSceneData;
    DeleteCriticalSection(&m_lock);
}

uint32_t IUIScene_LoadCreateJoin::GetCustomGameModeID()
{
    return m_miniGames ? eMiniGameId_Battle : eMiniGameId_Any;
}

void IUIScene_LoadCreateJoin::UpdateGamesList()
{
    if (m_ignoreInput != 0 || Minecraft::GetInstance()->mTexturePackRepository->getSelected()->isLoadingData()
        || Minecraft::GetInstance()->mTexturePackRepository->needsUIUpdate() || gConsoleUIController.IsReloadingSkin()
        || (m_saveInfoReady == 0 && m_miniGames == 0))
        return;

    bool hadSelection = false;
    unsigned int selectedSessionId = 0;
    int selectedRow = -1;
    if (DoesGamesListHaveFocus() && GetJoinListSize() > 0)
    {
        int selectedId = static_cast<int>(GetJoinListSelection());
        FriendSessionInfo* selected = GetJoinListSession(selectedId);
        if (selected != NULL)
        {
            selectedSessionId = selected->m_sessionId;
            hadSelection = true;
        }
        else
        {
            selectedRow = selectedId;
        }
    }

    ShowJoinListTimer(false);
    ShowNoGamesLabel(false);
    if (m_friendSessions != NULL)
    {
        for (std::vector<FriendSessionInfo*>::iterator it = m_friendSessions->begin(); it < m_friendSessions->end();
             ++it)
        {
            FriendSessionInfo* session = *it;
            if (session != NULL)
            {
                if (session->m_displayName != NULL)
                    delete session->m_displayName;
                delete session;
            }
        }
    }
    delete m_friendSessions;

    m_friendSessions = CGameNetworkManager::sInstance.GetSessionList(GetPad(), m_sessionListMode,
                                                                     m_partyGamesOnly != 0);
    SetJoinListEnabled(true);
    ClearJoinList();

    unsigned int position = 0;
    unsigned int selection = 0;
    if (!m_miniGames)
    {
        AddJoinListAction(L"Add Server", cJoinRowAddServer, L"Graphics\\CreateWorldIcon.png", L"CreateWorldIcon");
        if (selectedRow == cJoinRowAddServer)
            selection = position;
        ++position;
    }

    if (m_miniGames && m_onlinePlayAllowed)
    {
        struct MatchOption { EMiniGameId id; int row; const wchar_t* icon; };
        const MatchOption options[] = {
            {eMiniGameId_Battle, cJoinRowFindMatchBattle, L"BattleModeIcon"},
            {eMiniGameId_Tumble, cJoinRowFindMatchTumble, L"MG02icon"},
            {eMiniGameId_Glide, cJoinRowFindMatchGlide, L"MG03icon"}
        };
        for (unsigned int index = 0; index < sizeof(options) / sizeof(options[0]); ++index)
        {
            MiniGameDef* game = MiniGameDef::GetCustomGameModeById(options[index].id, true);
            if (game == NULL)
                continue;
            const std::wstring label = L10N::GetString(static_cast<int>(game->GetName()));
            const std::wstring icon = options[index].icon;
            const std::wstring path = std::wstring(L"Graphics\\") + icon + L".png";
            AddJoinListAction(label, options[index].row, path, icon);
            if (selectedRow == options[index].row)
                selection = position;
            ++position;
        }
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (unsigned int index = 0; index < m_friendSessions->size(); ++index)
        {
            FriendSessionInfo* session = m_friendSessions->at(index);
            if ((session->m_savedServer >= 0) != (pass == 0))
                continue;
            if (m_miniGames && session->m_savedServer < 0
                && session->m_gameSessionData.m_gameModeId != GameType::LOBBY->getId())
                continue;
            AddServerRow(session, static_cast<int>(index));
            if (hadSelection && session->m_sessionId == selectedSessionId)
                selection = position;
            ++position;
        }
    }

    AddJoinListScanningRow(L"Scanning for games on your local network", cJoinRowScanning);
    if (selectedRow == cJoinRowScanning)
        selection = position;
    SetGamesListSelection(selection);
    updateTooltips();
}

void IUIScene_LoadCreateJoin::AddServerRow(const FriendSessionInfo* session, int id)
{
    static const unsigned char kSignature[16] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13,
                                                 'I',  'H', 'D', 'R'};
    const std::vector<unsigned char>& icon = session->m_icon;
    const bool usable = icon.size() >= 24 && memcmp(&icon[0], kSignature, sizeof(kSignature)) == 0 && icon[16] == 0
                        && icon[17] == 0 && icon[18] == 0 && icon[19] == 64 && icon[20] == 0 && icon[21] == 0
                        && icon[22] == 0 && icon[23] == 64;
    if (usable)
        AddJoinListIcon(L"", id, formatwstr(L"si%08x", session->m_iconHash), &icon[0],
                        static_cast<unsigned int>(icon.size()));
    else
        AddJoinListAction(L"", id, L"Graphics\\MinecraftIcon.png", L"SavedServerIcon");
}

FriendSessionInfo* IUIScene_LoadCreateJoin::GetJoinListSession(int id)
{
    if (m_friendSessions == NULL || id < 0 || static_cast<unsigned int>(id) >= m_friendSessions->size())
        return NULL;
    return m_friendSessions->at(id);
}

int IUIScene_LoadCreateJoin::GetSelectedSavedServer()
{
    if (!DoesGamesListHaveFocus() || GetJoinListSize() == 0)
        return -1;
    FriendSessionInfo* session = GetJoinListSession(static_cast<int>(GetJoinListSelection()));
    return session != NULL ? session->m_savedServer : -1;
}

void IUIScene_LoadCreateJoin::UpdateGamesListCallback(void* context)
{
    if (context != 0)
        static_cast<IUIScene_LoadCreateJoin*>(context)->UpdateGamesList();
}

void IUIScene_LoadCreateJoin::_init(bool miniGames)
{
    m_miniGames = miniGames;
    m_onlinePlayAllowed = gProfile->IsSignedInLive(GetPad()) && gProfile->AllowedToPlayMultiplayer(GetPad());

    CMinecraftApp::PngTagData imageData;
    memset(&imageData, 0, sizeof(imageData));
    imageData.extraData = 0;
    imageData.flags |= CMinecraftApp::PngTagData::eFlag_HostOptions | CMinecraftApp::PngTagData::eFlag_ExtraData;
    imageData.hostOptions = 0;
    gApp.SetCurrentImageTextData(&imageData);
    CGameNetworkManager::sInstance.SetWorldThumbnail(NULL, 0);
    if (gApp.StartInstallDLCProcess(GetPad()) != 0 || gApp.DLCInstallPending() != 0)
        m_ignoreInput = true;
    else
        Initialise();
    UpdateGamesList();
    CGameNetworkManager::sInstance.SetSessionsUpdatedCallback(&IUIScene_LoadCreateJoin::UpdateGamesListCallback, this);
    m_joinSceneData = new JoinSceneData();
    MinecraftServer::s_haltRequested = false;
    MinecraftServer::s_saveOnExitAnswered = false;
    if (!m_ignoreInput)
        gApp.mDLCManager.checkForCorruptDLCAndAlert(true);
}

void IUIScene_LoadCreateJoin::AddGameModeButton(std::wstring label, int id, std::wstring icon)
{
    AddLoadListButton(label, id);
    m_buttonIds.push_back(id);
    std::wstring path = formatwstr(L"Graphics\\%ls.png", icon.c_str());
    if (gApp.hasArchiveFile(path, false))
    {
        arrayWithLength<unsigned char> file = gApp.getArchiveFile(path, false);
        if (file.length != 0 && file.data != NULL)
            SetLoadListIcon(m_buttonCount, file.data, file.length, icon);
    }
    ++m_buttonCount;
}

void IUIScene_LoadCreateJoin::AddDefaultButtons()
{
    if (m_buttonCount >= 1)
        return;

    m_texturePackWorldCount = 0;
    m_levelGenerationOptions.clear();
    m_buttonCount = 0;
    ClearCreateList();
    ClearLoadList();

    if (m_miniGames)
    {
        struct MiniGameButton
        {
            unsigned int label;
            const wchar_t* icon;
            EMiniGameId id;
        };
        MiniGameButton buttons[] = {
            {IDS_MINI_GAME_01, L"BattleModeIcon", eMiniGameId_Battle},
            {IDS_MINI_GAME_02, L"MG02icon", eMiniGameId_Tumble},
            {IDS_MINI_GAME_03, L"MG03icon", eMiniGameId_Glide},
        };
        ShowSaveListTimer(false);
        for (int i = 0; i < 3; ++i)
        {
            std::wstring label = L10N::GetString(buttons[i].label);
            std::wstring icon = buttons[i].icon;
            AddGameModeButton(label, buttons[i].id, icon);
        }
        return;
    }

    AddCreateListButton(L10N::GetString(IDS_CREATE_NEW_WORLD), -1);
    ++m_buttonCount;
    if (gApp.hasArchiveFile(L"Graphics\\CreateWorldIcon.png", false))
    {
        arrayWithLength<unsigned char> file = gApp.getArchiveFile(L"Graphics\\CreateWorldIcon.png", false);
        if (file.length != 0 && file.data != NULL)
            SetCreateListIcon(0, file.data, file.length, L"CreateWorldIcon");
    }

    std::vector<LevelGenerationOptions*>* generators = gApp.getLevelGenerators();
    for (std::vector<LevelGenerationOptions*>::iterator it = generators->begin(); it != generators->end(); ++it)
    {
        LevelGenerationOptions* options = *it;
        if (options->getRequiredGameRules() != NULL
            && options->getRequiredGameRules()->getRuleType() != LevelRules::eLevelRuleType_Unknown)
            continue;
        if (!options->isGameModeLobby())
            m_levelGenerationOptions.push_back(options);
    }
    std::sort(m_levelGenerationOptions.begin(), m_levelGenerationOptions.end(), LevelGenerators::SortLevelGenerators);

    int id = 0;
    for (std::vector<LevelGenerationOptions*>::iterator it = m_levelGenerationOptions.begin();
         it != m_levelGenerationOptions.end(); ++it, ++id)
    {
        LevelGenerationOptions* options = *it;
        unsigned int texturePackId = options->getTexturePackId();
        std::wstring worldName(options->getWorldName());
        AddCreateListButton(worldName, id);
        if (texturePackId == 0)
        {
            if (options->isTutorial())
            {
                if (gApp.hasArchiveFile(L"Graphics\\TutorialIcon.png", false))
                {
                    arrayWithLength<unsigned char> file = gApp.getArchiveFile(L"Graphics\\TutorialIcon.png", false);
                    if (file.length != 0 && file.data != NULL)
                        SetCreateListIcon(m_buttonCount, file.data, file.length, L"TutorialIcon");
                }
            }
        }
        else
        {
            ++m_texturePackWorldCount;
            TexturePack* pack = Minecraft::GetInstance()->mTexturePackRepository->getTexturePackById(
                options->getTexturePackId());
            unsigned int iconSize = 0;
            unsigned char* icon =
                pack != NULL ? static_cast<unsigned char*>(const_cast<void*>(pack->getPackIcon(iconSize))) : NULL;
            if (iconSize != 0 && icon != NULL)
            {
                wchar_t name[64];
                std::swprintf(name, 64, L"tpack%08x", pack->getId());
                SetCreateListIcon(m_buttonCount, icon, iconSize, name);
            }
        }
        ++m_buttonCount;
    }
}

int IUIScene_LoadCreateJoin::HandleTelemetrySavesReturned(void*, SAVE_DETAILS* details, bool)
{
    telemetryManagerInstance->RecordSavesInformation(gProfile->GetPrimaryPad(), details, 0);
    return 0;
}

void IUIScene_LoadCreateJoin::GetSaveInfo()
{
    if (m_miniGames)
        return;

    if (gApp.DebugSettingsOn() != 0 && gApp.GetLoadSavesFromFolderEnabled() != 0)
    {
        unsigned int fileCount = 0;
        File directory(gApp.GetDebugRootPath() + L"Saves");
        if (directory.exists())
        {
            m_debugSaveFiles = directory.listFiles();
            fileCount = static_cast<unsigned int>(m_debugSaveFiles->size());
        }
        AddDefaultButtons();
        for (unsigned int i = 0; i < fileCount; ++i)
        {
            std::wstring fileName = m_debugSaveFiles->at(i)->getName();
            wchar_t* label = new wchar_t[fileName.length() + 1];
            for (unsigned int c = 0; c < fileName.length(); ++c)
                label[c] = fileName[c];
            label[fileName.length()] = L'\0';
            AddLoadListButton(std::wstring(label), -1);
        }
        m_thumbnailsComplete = true;
        m_ignoreInput = false;
        m_saveInfoReady = true;
        ScrollLoadListToSelection(0);
        return;
    }

    m_saveInfoReady = false;
    ClearLoadList();
    ShowSaveListTimer(true);
    m_saveDetails = gStorage->ReturnSavesInfo();
    if (m_saveDetails == NULL)
        gStorage->GetSavesInfo(GetPad(), &IUIScene_LoadCreateJoin::HandleTelemetrySavesReturned, NULL, "save");
    else
        HandleTelemetrySavesReturned(NULL, m_saveDetails, false);
}

void IUIScene_LoadCreateJoin::_tick()
{
    switch (m_pendingAction)
    {
    case ePendingAction_None:
        break;
    case ePendingAction_GetSaveInfo:
        m_ignoreInput = false;
        m_pendingAction = ePendingAction_None;
        m_thumbnailsComplete = false;
        m_thumbnailRequested = false;
        m_nextThumbnail = 0;
        m_saveListRefreshState = 0;
        GetSaveInfo();
        break;
    case ePendingAction_RebuildSaveList:
        m_ignoreInput = false;
        m_nextThumbnail = 0;
        m_saveListRefreshState = 0;
        m_thumbnailsComplete = false;
        m_thumbnailRequested = false;
        m_saveInfoReady = false;
        ClearLoadList();
        GetSaveInfo();
        m_pendingAction = ePendingAction_None;
        break;
    case ePendingAction_ReloadSaveInfo:
    case ePendingAction_ReloadSaveInfoUnused:
        m_ignoreInput = false;
        m_nextThumbnail = 0;
        m_saveListRefreshState = 0;
        m_thumbnailsComplete = false;
        m_thumbnailRequested = false;
        m_saveInfoReady = false;
        m_selectedSaveIndex = 0;
        ClearLoadList();
        gStorage->ClearSavesInfo();
        GetSaveInfo();
        m_pendingAction = ePendingAction_None;
        break;
    }
}

void IUIScene_LoadCreateJoin::_updateTooltips()
{
    int selectTooltip = IDS_TOOLTIPS_SELECT;
    int listTooltip = -1;
    int partyTooltip = -1;
    if (DoesGamesListHaveFocus() && GetJoinListSize() > 0)
    {
        if (GetSelectedSavedServer() >= 0)
        {
            partyTooltip = IDS_TOOLTIPS_REMOVE;
            listTooltip = IDS_TOOLTIPS_OPTIONS;
        }
        else if (static_cast<int>(GetJoinListSelection()) == cJoinRowScanning)
        {
            selectTooltip = -1;
        }
    }
    else if (!m_miniGames && DoesLoadListHaveFocus() && GetLoadListSize() > 0)
    {
        if (!gStorage->GetSaveDisabled() && gStorage->EnoughSpaceForAMinSaveGame())
            listTooltip = IDS_TOOLTIPS_SAVEOPTIONS;
        else
            listTooltip = IDS_TOOLTIPS_DELETESAVE;
    }
    else if (DoesMashUpWorldHaveFocus())
    {
        listTooltip = IDS_TOOLTIPS_HIDE;
    }
    else
    {
        DoesCreateListHaveFocus();
    }

    if (m_partyGamesAvailable && DoesGamesListHaveFocus() && partyTooltip == -1)
        partyTooltip = m_partyGamesOnly ? IDS_TOOLTIPS_ALL_GAMES : IDS_TOOLTIPS_PARTY_GAMES;

    if (gStorage->GetSaveDisabled())
        DoesLoadListHaveFocus();

    gConsoleUIController.SetTooltips(0, selectTooltip, IDS_TOOLTIPS_BACK, partyTooltip, listTooltip, -1, -1, -1,
                                     -1, -1, -1, -1, -1, -1, -1, -1, -1, false, false);
}

void IUIScene_LoadCreateJoin::handleTimer(int timer)
{
    if (timer != 0)
        return;

    bool onlinePlayAllowed = gProfile->IsSignedInLive(GetPad()) && gProfile->AllowedToPlayMultiplayer(GetPad());
    if (onlinePlayAllowed == m_onlinePlayAllowed)
        return;
    if (!onlinePlayAllowed)
    {
        m_partyGamesAvailable = false;
        ClearJoinList();
        SetJoinListEnabled(false);
        ShowJoinListTimer(true);
        ShowNoGamesLabel(false);
    }
    m_onlinePlayAllowed = onlinePlayAllowed;
}

int IUIScene_LoadCreateJoin::DeleteSaveDialogReturned(void* context, int, C4JStorage::EMessageResult result)
{
    IUIScene_LoadCreateJoin* scene = static_cast<IUIScene_LoadCreateJoin*>(context);
    if (result == C4JStorage::EMessageResult_Decline
        && (!gApp.DebugSettingsOn() || !gApp.GetLoadSavesFromFolderEnabled()))
    {
        scene->DeleteSaveFile(scene->m_selectedSaveIndex);
        scene->ShowSaveListTimer(true);
    }
    else
    {
        scene->m_ignoreInput = false;
    }
    return 0;
}

int IUIScene_LoadCreateJoin::SaveOptionsDialogReturned(void* context, int userIndex, C4JStorage::EMessageResult result)
{
    return static_cast<IUIScene_LoadCreateJoin*>(context)->handleSaveOptionsDialogReturned(userIndex, result);
}

bool IUIScene_LoadCreateJoin::handleKeyDown(int pad, int action, bool)
{
    if (m_ignoreInput || (!m_saveInfoReady && !m_miniGames))
        return 0;

    bool handled = false;
    switch (action)
    {
    case EControllerActions_MenuB:
    case EControllerActions_MenuCancel:
        gConsoleUIController.NavigateBack(GetPad(), false, EUIScene_None, EUILayer_Count);
        handled = true;
        break;
    case EControllerActions_MenuX:
    {
        int serverIndex = GetSelectedSavedServer();
        if (serverIndex < 0)
            break;
        const std::vector<lcenet::SavedServer>& servers = lcenet::SavedServers().Servers();
        if (static_cast<unsigned int>(serverIndex) >= servers.size())
            break;
        std::wstring name;
        for (size_t index = 0; index < servers[serverIndex].name.size(); ++index)
        {
            if (servers[serverIndex].name[index] == L'%')
                name.push_back(L'%');
            name.push_back(servers[serverIndex].name[index]);
        }
        std::swprintf(m_removeServerText, sizeof(m_removeServerText) / sizeof(m_removeServerText[0]),
                      L"Remove %ls from your servers?", name.c_str());
        m_removeServerIndex = serverIndex;
        m_ignoreInput = true;
        int options[] = {IDS_CONFIRM_CANCEL, IDS_CONFIRM_OK};
        gConsoleUIController.RequestAlertMessage(IDS_TOOLTIPS_REMOVE, -1, options, 2, pad,
                                                 &IUIScene_LoadCreateJoin::RemoveServerDialogReturned, this,
                                                 m_removeServerText, true);
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        handled = true;
        break;
    }
    case EControllerActions_MenuY:
        if (GetSelectedSavedServer() >= 0)
        {
            gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
            EditSavedServer(GetSelectedSavedServer());
            handled = true;
            break;
        }
        if (!m_miniGames && DoesLoadListHaveFocus() && GetLoadListSize() > 0)
        {
            m_ignoreInput = true;
            if (gStorage->GetSaveDisabled())
            {
                int options[] = {IDS_CONFIRM_CANCEL, IDS_CONFIRM_OK};
                gConsoleUIController.RequestAlertMessage(IDS_TOOLTIPS_DELETESAVE, IDS_TEXT_DELETE_SAVE, options, 2, pad,
                                                         &IUIScene_LoadCreateJoin::DeleteSaveDialogReturned, this, 0,
                                                         true);
                gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
                break;
            }
            if (!gStorage->EnoughSpaceForAMinSaveGame())
            {
                int options[] = {IDS_CONFIRM_CANCEL, IDS_CONFIRM_OK};
                gConsoleUIController.RequestAlertMessage(IDS_TOOLTIPS_DELETESAVE, IDS_TEXT_DELETE_SAVE, options, 2, pad,
                                                         &IUIScene_LoadCreateJoin::DeleteSaveDialogReturned, this, 0,
                                                         true);
                gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
                break;
            }
            int options[4];
            options[0] = IDS_TITLE_RENAMESAVE;
            options[1] = IDS_TOOLTIPS_DELETESAVE;
            options[2] = IDS_TOOLTIPS_DELETESAVE;
            unsigned int optionCount = 2;
            if (!gApp.HasNoSpaceForSaveQuota())
            {
                optionCount = 3;
                options[2] = IDS_COPYSAVE;
            }
            if (gProfile->IsSignedInLive(gProfile->GetPrimaryPad()))
                options[optionCount++] = IDS_TOOLTIPS_SAVETRANSFER_TO_NX;
            gConsoleUIController.RequestAlertMessage(
                IDS_TOOLTIPS_SAVEOPTIONS, IDS_TEXT_SAVEOPTIONS, options, optionCount, pad,
                &IUIScene_LoadCreateJoin::SaveOptionsDialogReturned, this, 0, true);
            gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
            break;
        }
        if (DoesMashUpWorldHaveFocus())
        {
            if (m_selectedSaveIndex != 0)
            {
                LevelGenerationOptions* options = m_levelGenerationOptions.at(m_selectedSaveIndex - 1);
                if (!options->isTutorial() && options->requiresTexturePack())
                {
                    unsigned int texturePackId = options->getTexturePackId();
                    m_ignoreInput = true;
                    GameSettings::HideMashupPackWorld(GetPad(), texturePackId);
                    m_pendingAction = ePendingAction_RebuildSaveList;
                }
            }
            gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        }
        break;
    case EControllerActions_MenuRight:
    case EControllerActions_MenuRightScroll:
    {
        ++m_currentTab;
        if (m_miniGames && m_currentTab == 1)
            ++m_currentTab;
        int tabCount = GetHideLoadTab() ? 2 : 3;
        if (m_currentTab >= tabCount)
            m_currentTab = 0;
        SelectTab(m_currentTab);
        updateTooltips();
        break;
    }
    case EControllerActions_MenuLeft:
    case EControllerActions_MenuLeftScroll:
    {
        --m_currentTab;
        if (m_miniGames && m_currentTab == 1)
            --m_currentTab;
        int tabCount = GetHideLoadTab() ? 2 : 3;
        if (m_currentTab < 0)
            m_currentTab = tabCount - 1;
        SelectTab(m_currentTab);
        updateTooltips();
        break;
    }
    }
    return handled;
}

void IUIScene_LoadCreateJoin::LoadLevelGen(LevelGenerationOptions* options)
{
    gApp.ClearTerrainFeaturePosition();
    gStorage->ResetSaveData();
    CGameNetworkManager::sInstance.SetWorldThumbnail(NULL, 0);
    gStorage->SetSaveTitle(options->getDefaultSaveName().c_str());

    bool isClientSide = false;
    bool isPrivate = false;
    unsigned int hostOption = gApp.GetGameHostOption(eGameHostOption_All);
    uint8_t tutorialMode;
    {
        boost::shared_ptr<Player> player;
        tutorialMode = gApp.GetTutorialMode(player);
    }
    if (tutorialMode != 0)
    {
        isClientSide = false;
        hostOption = gApp.GetGameHostOption(eGameHostOption_Tutorial);
    }
    unsigned char gameType = static_cast<unsigned char>(gApp.GetGameHostOption(hostOption, eGameHostOption_GameType));
    CGameNetworkManager::sInstance.HostGame(0, isClientSide, isPrivate, gameType, eMiniGameId_Any, 0, 0);

    _NetworkGameInitData* initData = new _NetworkGameInitData();
    initData->m_seed = 0;
    initData->m_saveData = 0;
    initData->m_hostOptions = hostOption;
    initData->m_levelGenerationOptions = options;
    initData->m_gameType = gameType;
    initData->m_texturePackId = options->getTexturePackId();
    if (options->requiresTexturePack())
    {
        Minecraft::GetInstance()->mTexturePackRepository->selectTexturePackById(
            static_cast<unsigned int>(initData->m_texturePackId), false);
    }

    CGameNetworkManager::sInstance.FakeLocalPlayerJoined();
    _LoadingInputParams* params = new _LoadingInputParams();
    params->m_threadProc = &CGameNetworkManager::RunNetworkGameThreadProc;
    params->m_threadParam = initData;

    FullscreenProgressState* progressState = new FullscreenProgressState();
    progressState->closeMode = 3;
    progressState->playerId = 0;
    params->m_completionData = progressState;
    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_FullscreenProgress, params, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void IUIScene_LoadCreateJoin::LoadSaveFromDisk(File* file, ESavePlatform platform)
{
    gStorage->ResetSaveData();
    CGameNetworkManager::sInstance.SetWorldThumbnail(NULL, 0);
    gStorage->SetSaveTitle(file->getName().c_str());

    unsigned int fileLength = static_cast<unsigned int>(file->length());
    FileInputStream input(*file);
    arrayWithLength<unsigned char> fileData(fileLength, true);
    input.read(fileData);
    input.close();

    bool isPrivate = false;
    bool isClientSide = gProfile->IsSignedInLive(gProfile->GetPrimaryPad());
    uint8_t tutorialMode;
    {
        boost::shared_ptr<Player> player;
        tutorialMode = gApp.GetTutorialMode(player);
    }
    if (tutorialMode != 0)
        isClientSide = false;

    gApp.SetGameHostOption(eGameHostOption_GameType, GameType::CREATIVE->getId());
    unsigned char gameType = static_cast<unsigned char>(gApp.GetGameHostOption(eGameHostOption_GameType));
    CGameNetworkManager::sInstance.HostGame(0, isClientSide, isPrivate, gameType, eMiniGameId_Any, 0, 0);

    DiskSaveBuffer* saveBuffer = new DiskSaveBuffer;
    saveBuffer->data = fileData.data;
    saveBuffer->length = fileLength;
    saveBuffer->name = file->getName();

    _NetworkGameInitData* initData = new _NetworkGameInitData();
    initData->m_seed = 0;
    initData->m_saveData = saveBuffer;
    initData->m_hostOptions = gApp.GetGameHostOption(eGameHostOption_All);
    const MiniGameDef* miniGame = Minecraft::GetInstance()->GetMiniGame();
    initData->m_miniGameHostOptions = miniGame->GetHostOption(eCustomGameModeHostOption_All);
    initData->m_savePlatform = platform;
    initData->m_gameType = gApp.GetGameHostOption(eGameHostOption_GameType);

    CGameNetworkManager::sInstance.FakeLocalPlayerJoined();

    _LoadingInputParams* params = new _LoadingInputParams();
    params->m_threadProc = &CGameNetworkManager::RunNetworkGameThreadProc;
    params->m_threadParam = initData;

    FullscreenProgressState* progressState = new FullscreenProgressState();
    progressState->closeMode = 3;
    progressState->playerId = 0;
    params->m_completionData = progressState;

    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_FullscreenProgress, params, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void IUIScene_LoadCreateJoin::LoadGameMode(EMiniGameId gameMode)
{
    LevelGenerationOptions* lobbyOptions = 0;
    std::vector<LevelGenerationOptions*>* generators = gApp.getLevelGenerators();
    for (std::vector<LevelGenerationOptions*>::iterator it = generators->begin(); it != generators->end(); ++it)
    {
        if ((*it)->isGameModeLobby())
            lobbyOptions = *it;
    }

    LoadMenuSceneData* sceneData = new LoadMenuSceneData();
    sceneData->playerId = GetPad();
    sceneData->saveIndex = -1;
    sceneData->generationOptions = lobbyOptions;
    sceneData->saveDetails = NULL;
    sceneData->gameMode = gameMode;

    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_LoadMenu, sceneData, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void IUIScene_LoadCreateJoin::LoadMiniGameMatch(EMiniGameId gameMode)
{
    LoadMenuSceneData* sceneData = new LoadMenuSceneData();
    sceneData->playerId = GetPad();
    sceneData->saveIndex = -1;
    sceneData->generationOptions = NULL;
    sceneData->saveDetails = NULL;
    sceneData->gameMode = gameMode;
    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_LoadMenu, sceneData, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void IUIScene_LoadCreateJoin::HandleLoadListPress(int index)
{
    m_ignoreInput = true;
    if (m_miniGames)
    {
        int gameMode = m_buttonIds[index];
        if (gameMode > eMiniGameId_Build)
            gameMode = eMiniGameId_Build;
        m_selectedMiniGame = static_cast<EMiniGameId>(gameMode);
        LoadGameMode(m_selectedMiniGame);
        return;
    }

    gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    if (gApp.DebugSettingsOn() && gApp.GetLoadSavesFromFolderEnabled())
    {
        gApp.SetTutorialMode(false, boost::shared_ptr<Player>());
        LoadSaveFromDisk(m_debugSaveFiles->at(index), eSavePlatform_WIIU);
        return;
    }

    gApp.SetTutorialMode(false, boost::shared_ptr<Player>());
    LoadMenuSceneData* sceneData = new LoadMenuSceneData();
    sceneData->playerId = GetPad();
    sceneData->saveIndex = index;
    sceneData->generationOptions = NULL;
    sceneData->saveDetails = &m_saveListDetails[index];
    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_LoadMenu, sceneData, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void IUIScene_LoadCreateJoin::HandleNewListPress(int index, int variant)
{
    m_ignoreInput = 1;
    gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    if (m_miniGames == 0)
    {
        if (index == 0)
        {
            {
                boost::shared_ptr<Player> player;
                gApp.SetTutorialMode(false, player);
            }
            ShowJoinListTimer(false);
            gApp.SetCorruptSaveDeleted(false);
            LoadMenuSceneData* sceneData = new LoadMenuSceneData();
            sceneData->playerId = GetPad();
            sceneData->saveIndex = -1;
            sceneData->generationOptions = NULL;
            sceneData->saveDetails = NULL;
            gConsoleUIController.NavigateToScene(GetPad(), EUIScene_CreateWorldMenu, sceneData, EUILayer_Scene,
                                                 EUIGroup_PlayerDefault);
            return;
        }
        if (variant < 0 || static_cast<unsigned int>(variant) >= m_levelGenerationOptions.size())
            return;
        LevelGenerationOptions* options = m_levelGenerationOptions.at(static_cast<unsigned int>(variant));
        gApp.SetBaseSaveName(options->getBaseSavePath());
        {
            boost::shared_ptr<Player> player;
            gApp.SetTutorialMode(options->isTutorial(), player);
        }
        gApp.SetAutosaveTimerTime();
        if (options->isTutorial())
        {
            gApp.SetGameHostOption(eGameHostOption_Gamertags,
                                   GameSettings::GetGameSettings(gProfile->GetPrimaryPad(), eGameSetting_DisplayMii));
            LoadLevelGen(options);
            return;
        }
        LoadMenuSceneData* sceneData = new LoadMenuSceneData();
        sceneData->playerId = GetPad();
        sceneData->saveIndex = -1;
        sceneData->generationOptions = options;
        sceneData->saveDetails = NULL;
        gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_LoadMenu, sceneData, EUILayer_Scene,
                                             EUIGroup_PlayerDefault);
        return;
    }
    m_ignoreInput = 0;
}

void IUIScene_LoadCreateJoin::CheckAndJoinGame(int index)
{
    if (GetJoinListSize() <= 0)
        return;
    if (static_cast<unsigned int>(index) >= m_friendSessions->size())
        return;

    m_joinSceneData->userIndex = GetPad();
    m_joinSceneData->session = m_friendSessions->at(index);
    m_joinSceneData->miniGames = m_miniGames;

    ShowJoinListTimer(false);
    m_ignoreInput = true;
    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_JoinMenu, m_joinSceneData, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
}

void IUIScene_LoadCreateJoin::HandleGamesListPressed(int index)
{
    if (index == cJoinRowFindMatchBattle)
    {
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_ignoreInput = true;
        LoadMiniGameMatch(eMiniGameId_Battle);
        return;
    }
    if (index == cJoinRowFindMatchTumble)
    {
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_ignoreInput = true;
        LoadMiniGameMatch(eMiniGameId_Tumble);
        return;
    }
    if (index == cJoinRowFindMatchGlide)
    {
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        m_ignoreInput = true;
        LoadMiniGameMatch(eMiniGameId_Glide);
        return;
    }
    if (index == cJoinRowScanning)
        return;
    if (index == cJoinRowAddServer)
    {
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        EditSavedServer(-1);
        return;
    }
    m_ignoreInput = true;
    m_pressedList = 2;
    gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    m_selectedGameIndex = index;
    CheckAndJoinGame(index);
}

void IUIScene_LoadCreateJoin::SetIgnoreInput(bool ignore)
{
    m_ignoreInput = ignore;
}

void IUIScene_LoadCreateJoin::EditSavedServer(int serverIndex)
{
    m_ignoreInput = true;
    gConsoleUIController.NavigateToScene(GetPad(), EUIScene_EditServerMenu, new EditServerSceneData(serverIndex),
                                         EUILayer_Scene, EUIGroup_PlayerDefault);
}

int IUIScene_LoadCreateJoin::RemoveServerDialogReturned(void* context, int, C4JStorage::EMessageResult result)
{
    IUIScene_LoadCreateJoin* scene = static_cast<IUIScene_LoadCreateJoin*>(context);
    if (result == C4JStorage::EMessageResult_Decline && scene->m_removeServerIndex >= 0)
    {
        lcenet::SavedServers().Remove(static_cast<size_t>(scene->m_removeServerIndex));
        lcenet::SavedServers().Save();
    }
    scene->m_removeServerIndex = -1;
    scene->m_ignoreInput = false;
    scene->UpdateGamesList();
    return 0;
}
