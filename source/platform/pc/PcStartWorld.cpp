#include "platform/pc/PcStartWorld.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/FileHeader.hpp"
#include "game/core/Minecraft.hpp"
#include "game/minigame/MiniGameDef.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/ui/UIGroup.hpp"
#include "game/ui/UILayer.hpp"
#include "game/ui/UIScene_FullscreenProgress.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/world/generation/Player.hpp"
#include "platform/CInput.hpp"
#include "platform/CProfile.hpp"
#include "platform/CStorage.hpp"
#include "platform/pc/Log.hpp"
#include "platform/pc/PcApplication.hpp"
#include "platform/pc/PcLaunchOptions.hpp"
#include "platform/pc/PcLiveBridge.hpp"
#include "platform/pc/PcPaths.hpp"
#include "vendor/DiskSaveBuffer.hpp"
#include "vendor/_LoadingInputParams.hpp"
#include "vendor/_NetworkGameInitData.hpp"

extern CConsoleMinecraftApp gApp;

namespace
{
enum StartWorldState
{
    StartWorldState_Idle,
    StartWorldState_WaitForMenu,
    StartWorldState_Loading,
    StartWorldState_Done
};

const int kMenuSettleFrames = 60;
const double kTimeoutSeconds = 300.0;

const Logger sLog("StartWorld");
StartWorldState sState = StartWorldState_Idle;
int sMenuFrames = 0;
double sStateStart = 0.0;
std::string sLiveInvite;

std::string ReadLiveInvite()
{
    FILE* file = PcLiveBridge::Open(L"join.txt", L"rb");
    if (!file) return std::string();
    char text[16] = {};
    const bool read = fgets(text, sizeof(text), file) != NULL;
    fclose(file);
    PcLiveBridge::Remove(L"join.txt");
    if (!read) return std::string();
    char* end = NULL;
    long port = strtol(text, &end, 10);
    if (*end != '\0' || port < 1 || port > 65535) return std::string();
    return std::string("127.0.0.1:") + text;
}

double NowSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Enter(StartWorldState state)
{
    sState = state;
    sStateStart = NowSeconds();
}

void Fail(const char* message, const std::string& detail)
{
    LOG_ERROR(sLog, "%s: %s", message, detail.c_str());
    if (sLiveInvite.empty()) PcRequestExit();
    Enter(StartWorldState_Done);
}

bool LoadWorld(const std::string& folder, std::string& error)
{
    _NetworkGameInitData* initData = PcPrepareWorldLoad(folder, error);
    if (initData == NULL)
        return false;
    gApp.SetTutorialMode(false, boost::shared_ptr<Player>());
    CGameNetworkManager::sInstance.HostGame(0, false, false, static_cast<unsigned char>(initData->m_gameType),
                                            eMiniGameId_Any, 0, 0);
    CGameNetworkManager::sInstance.FakeLocalPlayerJoined();

    _LoadingInputParams* params = new _LoadingInputParams();
    params->m_threadProc = &CGameNetworkManager::RunNetworkGameThreadProc;
    params->m_threadParam = initData;
    FullscreenProgressState* progressState = new FullscreenProgressState();
    progressState->closeMode = 3;
    progressState->playerId = 0;
    params->m_completionData = progressState;
    gApp.SetAutosaveTimerTime();
    gConsoleUIController.NavigateToScene(gProfile->GetPrimaryPad(), EUIScene_FullscreenProgress, params, EUILayer_Scene,
                                         EUIGroup_PlayerDefault);
    return true;
}
}

bool PcSaveFolderExists(const std::string& folder)
{
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::path(PcSaveRoot()) / "games" / folder / "world.bin", error);
}

bool PcReadSaveFile(const std::string& folder, const char* file, std::vector<unsigned char>& data, std::string& error)
{
    std::filesystem::path path = std::filesystem::path(PcSaveRoot()) / "games" / folder / file;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = path.string() + " could not be opened";
        return false;
    }
    stream.seekg(0, std::ios::end);
    std::streamoff length = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if (length <= 0)
    {
        error = path.string() + " is empty";
        return false;
    }
    data.resize(static_cast<size_t>(length));
    stream.read(reinterpret_cast<char*>(&data[0]), length);
    if (!stream)
    {
        error = path.string() + " could not be read";
        return false;
    }
    return true;
}

_NetworkGameInitData* PcPrepareWorldLoad(const std::string& folder, std::string& error)
{
    std::vector<unsigned char> world;
    if (!PcReadSaveFile(folder, "world.bin", world, error))
        return NULL;

    int texturePackId = -1;
    std::vector<unsigned char> thumbnail;
    std::string thumbnailError;
    if (PcReadSaveFile(folder, "image.png", thumbnail, thumbnailError))
    {
        CGameNetworkManager::sInstance.SetWorldThumbnail(&thumbnail[0], static_cast<unsigned int>(thumbnail.size()));
        CMinecraftApp::PngTagData tagData;
        gApp.GetImageTextData(&thumbnail[0], static_cast<unsigned int>(thumbnail.size()), &tagData);
        tagData.flags |= CMinecraftApp::PngTagData::eFlag_NumLoads;
        ++tagData.numLoads;
        gApp.SetCurrentImageTextData(&tagData);
        if ((tagData.flags & CMinecraftApp::PngTagData::eFlag_TexturePack) != 0)
            texturePackId = static_cast<int>(tagData.texturePack);
    }
    else
    {
        CGameNetworkManager::sInstance.SetWorldThumbnail(NULL, 0);
        LOG_WARN(sLog, "Loading with default settings: %s", thumbnailError.c_str());
    }

    unsigned char* data = static_cast<unsigned char*>(std::malloc(world.size()));
    if (data == NULL)
    {
        error = "out of memory for the save";
        return NULL;
    }
    std::memcpy(data, &world[0], world.size());

    std::wstring name(folder.begin(), folder.end());
    std::vector<char> uniqueName(folder.begin(), folder.end());
    uniqueName.push_back('\0');
    gStorage->ResetSaveData();
    gStorage->SetSaveTitle(name.c_str());
    gStorage->SetSaveUniqueFilename(&uniqueName[0]);

    if (texturePackId >= 0)
        Minecraft::GetInstance()->mTexturePackRepository->selectTexturePackById(texturePackId, false);

    DiskSaveBuffer* saveBuffer = new DiskSaveBuffer;
    saveBuffer->data = data;
    saveBuffer->length = static_cast<unsigned int>(world.size());
    saveBuffer->name = name;

    _NetworkGameInitData* initData = new _NetworkGameInitData();
    initData->m_seed = 0;
    initData->m_saveData = saveBuffer;
    initData->m_recordLevelResume = 1;
    initData->m_hostOptions = gApp.GetGameHostOption(eGameHostOption_All);
    initData->m_miniGameHostOptions = Minecraft::GetInstance()->GetMiniGame()->GetHostOption(
        eCustomGameModeHostOption_All);
    if (texturePackId >= 0)
        initData->m_texturePackId = texturePackId;
    initData->m_savePlatform = SAVE_FILE_PLATFORM_LOCAL;
    initData->m_gameType = gApp.GetGameHostOption(eGameHostOption_GameType);
    return initData;
}

bool PcInWorld()
{
    Minecraft* minecraft = Minecraft::GetInstance();
    return gApp.GetGameStarted() != 0 && minecraft->mLevel != NULL && minecraft->mPlayer != NULL
           && !gConsoleUIController.IsSceneInStack(gProfile->GetPrimaryPad(), EUIScene_FullscreenProgress);
}

void PcStartWorldTick()
{
    const PcLaunchOptions& options = PcGetLaunchOptions();
    if ((sState == StartWorldState_Idle || sState == StartWorldState_Done) && !PcInWorld()
        && gConsoleUIController.IsSceneInStack(gProfile->GetPrimaryPad(), EUIScene_MainMenu))
    {
        const std::string invite = ReadLiveInvite();
        if (!invite.empty()) { sLiveInvite = invite; sMenuFrames = 0; Enter(StartWorldState_Idle); }
    }
    if (sLiveInvite.empty() && options.world.empty() && options.join.empty())
        return;
    const bool joining = !sLiveInvite.empty() || options.world.empty();
    const std::string& target = !sLiveInvite.empty() ? sLiveInvite : (joining ? options.join : options.world);

    switch (sState)
    {
    case StartWorldState_Idle:
        Enter(StartWorldState_WaitForMenu);
        break;
    case StartWorldState_WaitForMenu:
        if (++sMenuFrames < kMenuSettleFrames)
            break;
        if (gConsoleUIController.IsSceneInStack(gProfile->GetPrimaryPad(), EUIScene_SaveMessage))
        {
            gConsoleUIController.NavigateToHomeMenu(true);
            sMenuFrames = 0;
            break;
        }
        if (!gConsoleUIController.IsSceneInStack(gProfile->GetPrimaryPad(), EUIScene_MainMenu))
        {
            if (NowSeconds() - sStateStart > kTimeoutSeconds)
                Fail("The main menu never appeared", target);
            break;
        }
        gProfile->SetPrimaryPad(g_pInput->LastInputPad());
        {
            std::string error;
            if (!joining && !LoadWorld(options.world, error))
            {
                Fail("The world could not be loaded", error);
                break;
            }
            if (joining && CGameNetworkManager::sInstance.JoinServerAddress(target) != 0)
            {
                Fail("Could not join", target);
                break;
            }
        }
        LOG_INFO(sLog, joining ? "Joining %s" : "Loading %s", target.c_str());
        Enter(StartWorldState_Loading);
        break;
    case StartWorldState_Loading:
        if (PcInWorld())
        {
            Minecraft* minecraft = Minecraft::GetInstance();
            LOG_INFO(sLog, "In %s at %.2f,%.2f,%.2f facing %.1f,%.1f", target.c_str(), minecraft->mPlayer->mX,
                     minecraft->mPlayer->mY, minecraft->mPlayer->mZ, minecraft->mPlayer->mYRot,
                     minecraft->mPlayer->mXRot);
            Enter(StartWorldState_Done);
        }
        else if (NowSeconds() - sStateStart > kTimeoutSeconds)
        {
            Fail("The world did not finish loading", target);
        }
        break;
    case StartWorldState_Done:
        break;
    }
}
