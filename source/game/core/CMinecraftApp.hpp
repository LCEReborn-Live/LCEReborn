#ifndef GAME_CORE_CMINECRAFTAPP_HPP
#define GAME_CORE_CMINECRAFTAPP_HPP
#include <string>
#include <vector>
#include <stdint.h>
#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>
#include "vendor/_SkinAdjustments.hpp"
#include "game/world/biome/arrayWithLength.hpp"
#include "game/world/generation/PlayerUID.hpp"
#include "game/minigame/EMiniGameId.hpp"
#include "game/core/DLCManager.hpp"
#include "game/core/GameRuleManager.hpp"
#include "game/core/ColourTable.hpp"
#include "game/item/not_null_ptr.hpp"
#include "game/network/DisconnectPacket.hpp"
#include "platform/CProfile.hpp"
#include "platform/C4JStorage.hpp"
#include "game/core/CriticalSection.hpp"
#include "platform/PlatformTiming.hpp"
#include "vendor/_XUSER_SIGNIN_INFO.hpp"
struct NOTIFICATION
{
    unsigned int type;
    unsigned int value;
};

class ArchiveFile;
class LevelGenerationOptions;
class LevelChunk;
class Level;
class CConsoleMinecraftApp;
class CStorage;
class C4JStorage;
class LevelGenerators;
class LevelRules;
class LevelRuleset;
class Block;
class BlockPos;
class Player;
class LocalPlayer;
class Inventory;
class Container;
class Merchant;
class SignBlockEntity;
class MinecartHopper;
class AbstractHorse;
class ItemInstance;
class CommandBlockEntity;
class Stat;
class DLCPack;
class ModelPart;
struct SKIN_BOX;
struct MEMDATA
{
    unsigned char* data;
    unsigned int size;
    unsigned char refCount;
};
struct MOJANG_DATA;
struct BANNEDLISTDATA;
struct _TMSPPRequest;
struct FEATURE_DATA
{
    int x;
    int z;
    int type;
};
enum eGameHostOption
{
    eGameHostOption_Difficulty = 0,
    eGameHostOption_FriendsOfFriends = 0x03,
    eGameHostOption_Gamertags = 0x04,
    eGameHostOption_Tutorial = 0x05,
    eGameHostOption_GameType = 0x06,
    eGameHostOption_LevelType = 0x07,
    eGameHostOption_Structures = 0x08,
    eGameHostOption_BonusChest = 0x09,
    eGameHostOption_HasBeenInCreative = 0x0a,
    eGameHostOption_PvP = 0x0b,
    eGameHostOption_TrustPlayers = 0x0c,
    eGameHostOption_TNT = 0x0d,
    eGameHostOption_FireSpreads = 0x0e,
    eGameHostOption_CheatsEnabled = 0x0f,
    eGameHostOption_HostCanFly = 0x10,
    eGameHostOption_HostCanChangeHunger = 0x11,
    eGameHostOption_HostCanBeInvisible = 0x12,
    eGameHostOption_ImmediateRespawn = 0x13,
    eGameHostOption_WorldSize = 0x14,
    eGameHostOption_DoWeatherCycle = 0x15,
    eGameHostOption_DisableSaving = 0x16,
    eGameHostOption_WasntSaveOwner = 0x17,
    eGameHostOption_MobGriefing = 0x18,
    eGameHostOption_KeepInventory = 0x19,
    eGameHostOption_DoMobSpawning = 0x1a,
    eGameHostOption_DoMobLoot = 0x1b,
    eGameHostOption_DoTileDrops = 0x1c,
    eGameHostOption_NaturalRegeneration = 0x1d,
    eGameHostOption_DoDaylightCycle = 0x1e,
    eGameHostOption_MiniGame = 0x1f,
    eGameHostOption_All = 0x20
};
enum eXuiServerAction
{
    eXuiServerAction_Idle = 0,
    eXuiServerAction_DropItem = 1,
    eXuiServerAction_SaveGame = 2,
    eXuiServerAction_AutoSaveGame = 3,
    eXuiServerAction_SpawnMob = 4,
    eXuiServerAction_PauseServer = 5,
    eXuiServerAction_ToggleRain = 6,
    eXuiServerAction_ToggleThunder = 7,
    eXuiServerAction_ServerSettingChanged = 8,
    eXuiServerAction_CloseToNetwork = 9
};
enum eXuiAction
{
    eXuiAction_Idle = 0,
    eXuiAction_SaveGame,
    eXuiAction_SaveGameNow,
    eXuiAction_ExitWorld,
    eXuiAction_ExitWorldNow,
    eXuiAction_Respawn,
    eXuiAction_RespawnProgress,
    eXuiAction_PrimarySignedOut,
    eXuiAction_PrimarySignedOutInGame,
    eXuiAction_PrimarySignedOutReturnToMenu,
    eXuiAction_RemoveLocalPlayer,
    eXuiAction_RemoveLocalPlayerNoStats,
    eXuiAction_ExitGame,
    eXuiAction_CloseScenesOnConnect,
    eXuiAction_ConnectionLost,
    eXuiAction_ConnectionLostWait,
    eXuiAction_ExitWorldAfterDisconnect,
    eXuiAction_ReturnToMenuAfterDisconnect,
    eXuiAction_JoinFromInvite,
    eXuiAction_ExitAndJoinFromInvite,
    eXuiAction_JoinInvitedGame,
    eXuiAction_ChangeSessionType,
    eXuiAction_SetDefaultGameOptions,
    eXuiAction_RemoteSave = 27,
    eXuiAction_RemoteSaveInProgress,
    eXuiAction_MultiplayerNotAllowed,
    eXuiAction_CheckSaveExists,
    eXuiAction_AutoSave,
    eXuiAction_ProfileReadError,
    eXuiAction_LavaNearSpawn,
    eXuiAction_BannedLevel,
    eXuiAction_BannedLevelPrompt,
    eXuiAction_ReloadTexturePack,
    eXuiAction_ReloadTexturePackInTransition,
    eXuiAction_ReloadSkin,
    eXuiAction_TexturePackMissing,
    eXuiAction_GameModeTransition,
    eXuiAction_GameModeTransitionInProgress,
    eXuiAction_Reset
};
enum eGameExtraData
{
    eGameExtraData_Unused,
    eGameExtraData_TutorialMode,
    eGameExtraData_TrialTexturePack,
    eGameExtraData_ModernEnd,
    eGameExtraData_SaveBuild,
    eGameExtraData_SaveVersion,
    eGameExtraData_All
};
const unsigned int EXTRA_DATA_SAVE_BUILD = 2177;
const unsigned int EXTRA_DATA_SAVE_VERSION = 11;
enum eGameMode
{
    eGameMode_0 = 0
};
enum eTPDFileType
{
    eTPDFileType_Icon = 1
};
enum eTMSAction
{
    eTMSAction_Default = 0
};
const unsigned int SKIN_ID_DLC_FLAG_SHIFT = 31;
const unsigned int SKIN_ID_DLC_MASK = 0x7fffffffU;
const unsigned int SKIN_ID_UGC_MASK = 0x7fffffe0U;
const unsigned int SKIN_ID_DEFAULT_MASK = 0x1fU;
enum eFileExtensionType
{
    eFileExtensionType_Default = 0
};
enum eTMSFileType
{
    eTMSFileType_Default = 0
};
enum _eTerrainFeatureType
{
    eTerrainFeatureType_0 = 0,
    eTerrainFeatureType_Stronghold = 1
};
enum EHTMLFontSize
{
    EHTMLFontSize_0 = 0
};
struct TIPSTRUCT
{
    unsigned int sortValue;
    unsigned int tipId;
};
class CMinecraftApp
{
public:
    class PngTagData
    {
    public:
        enum
        {
            eFlag_NumLoads = 0x02000000,
            eFlag_ExploredChunks = 0x04000000,
            eFlag_ExtraData = 0x08000000,
            eFlag_TexturePack = 0x10000000,
            eFlag_HostOptions = 0x20000000,
            eFlag_BaseSaveName = 0x40000000,
            eFlag_Seed = 0x80000000
        };

        unsigned int flags;
        long long seed;
        char baseSaveName[20];
        unsigned int hostOptions;
        unsigned int texturePack;
        unsigned int extraData;
        unsigned int exploredChunks[5];
        unsigned int numLoads;

        PngTagData();
    };
    class _TipData
    {
        unsigned int m_index;
        unsigned int m_count;
        TIPSTRUCT* m_tips;

    public:
        _TipData();
        _TipData(TIPSTRUCT* tips, unsigned int count);
        unsigned int getNextTip();
    };
    CMinecraftApp();
    virtual ~CMinecraftApp();
    virtual void CaptureScreenshot(int player);
    virtual void SetRichPresenceContext(int context, int value) = 0;
    virtual void FatalLoadError(const wchar_t* message, ...);
    virtual void StoreLaunchData();
    virtual void ExitGame();
    virtual void RunFrame();
    virtual void CaptureSaveThumbnail() = 0;
    virtual void GetSaveThumbnail(unsigned char** data, unsigned int* size) = 0;
    virtual void ReleaseSaveThumbnail() = 0;
    virtual void GetScreenshot(int player, unsigned char** data, unsigned int* size) = 0;
    virtual void ReadBannedList(int player, eTMSAction action, bool force) = 0;
    virtual uint32_t LoadLocalTMSFile(wchar_t* path) = 0;
    virtual uint32_t LoadLocalTMSFile(wchar_t* path, eFileExtensionType extension) = 0;
    virtual void FreeLocalTMSFiles(eTMSFileType type) = 0;
    virtual uint32_t GetLocalTMSFileIndex(wchar_t* path, bool create, eFileExtensionType extension) = 0;
    virtual uint32_t GetTMSGlobalFileListRead();
    virtual uint32_t GetTMSDLCInfoRead();
    virtual uint32_t GetTMSXUIDsFileRead();
    virtual void GetFileFromTPD(eTPDFileType type, unsigned char* source, unsigned int sourceSize, unsigned char** data,
                                unsigned int* size);
    void SetGameHostOption(unsigned int& options, eGameHostOption option, unsigned int value);
    void SetGameHostOption(eGameHostOption option, unsigned int value);
    void SetXuiServerAction(int player, eXuiServerAction action, void* param);
    uint32_t GetGameHostOption(unsigned int options, eGameHostOption option);
    uint32_t GetGameHostOption(eGameHostOption option);
    uint32_t GetOptionsCallbackStatus(int32_t player);
    void UpdateTime();
    int GetLocalPlayerCount();
    void loadMediaArchive();
    arrayWithLength<unsigned char> getArchiveFile(const std::wstring& path, bool ignoreTexturePack);
    bool hasArchiveFile(const std::wstring& path, bool ignoreTexturePack);
    std::vector<std::wstring> getBaseArchiveFiles();
    void InitTime();
    uint32_t IsActionPending(int player, eXuiAction action);
    static bool isValidStateTransition(eXuiAction current, eXuiAction next);
    void SetActionInternal(int player, eXuiAction action, void* param);
    void SetAction(int player, eXuiAction action, void* param);
    void SetBanListCheck(int32_t player, bool check);
    void InvalidateBannedList(int32_t player);
    void ClearDLCInstalled();
    uint8_t GetLiveLinkRequired();
    static int SignInChangeCallback(void* context, bool signedIn, unsigned int usersMask);
    static void ProfileReadErrorCallback(void* context);
    static int DefaultOptionsCallback(void* context, C4JStorage::PROFILESETTINGS* settings, int profile);
    static int OptionsDataCallback(void* context, int profile, unsigned short size,
                                   C4JStorage::eOptionsCallback status);
    void EnterSaveNotificationSection();
    void LeaveSaveNotificationSection();
    bool GetGameStarted();
    static bool IsDedicatedServer();
    static void SetDedicatedServer(bool dedicated);
    void SetDefaultHostOptions();
    void InitGameSettings();
    static int TipsSortFunction(const void* firstTip, const void* secondTip);
    void InitialiseTips();
    void SetAppPaused(bool paused);
    uint8_t GetReallyChangingSessionType();
    eXuiAction GetGlobalXuiAction();
    uint32_t GetTMSAction(int32_t player);
    static int EthernetDisconnectReturned(void* context, int pad, C4JStorage::EMessageResult result);
    static int BannedLevelDialogReturned(void* context, int pad, C4JStorage::EMessageResult result);
    void SetGlobalXuiAction(eXuiAction action);
    uint8_t GetChangingSessionType();
    void SetAutosaveTimerTime();
    void SetGameStarted(bool started);
    uint32_t GetXuiServerAction(int32_t player);
    void SetChangingSessionType(bool changing);
    uint32_t GetDisconnectReason();
    void SetReallyChangingSessionType(bool changing);
    static int SignoutExitWorldThreadProc(void* context);
    static int ExitAndJoinFromInvite(void* context, int pad, C4JStorage::EMessageResult result);
    static int ExitAndJoinFromInviteAndSaveReturned(void* context, int pad, C4JStorage::EMessageResult result);
    static int ExitAndJoinFromInviteDeclineSaveReturned(void* context, int pad, C4JStorage::EMessageResult result);
    static int ExitAndJoinFromInviteSaveDialogReturned(void* context, int pad, C4JStorage::EMessageResult result);
    void SetExtraGameData(unsigned int& options, eGameExtraData type, unsigned int value) const;
    void SetExtraGameData(eGameExtraData type, unsigned int value);
    void SetTutorialMode(bool tutorial, const boost::shared_ptr<Player>& player);
    eXuiAction GetXuiAction(int player);
    static int RemoteSaveThreadProc(void* context);
    static int ExitGameFromRemoteSaveDialogReturned(void* context, int pad, C4JStorage::EMessageResult result);
    static void ExitGameFromRemoteSave(void* context);
    static void SetupMiniGameMusic();
    static int GameModeTransitionThreadProc(void* context);
    static int GameModeTransitionComplete(void* context);
    void SetTMSAction(int32_t player, eTMSAction action);
    uint8_t IsAppPaused();
    void setLevelGenerationOptions(LevelGenerationOptions* options);
    uint8_t DLCInstallProcessCompleted();
    std::wstring getFilePath(unsigned int packId, std::wstring fileName, bool inPack, std::wstring drive);
    void HandleDLC(DLCPack* pack);
    static int DLCMountedCallback(void* context, int result, unsigned int contentType, unsigned int index);
    void MountNextDLC(int32_t pad);
    static int DLCInstalledCallback(void* context, int result, int index);
    uint32_t StartInstallDLCProcess(int32_t pad);
    int LoadPack(const std::string& path, const std::wstring& name);
    bool UnloadServerPacks();
    std::vector<LevelGenerationOptions*>* getLevelGenerators();
    void ClearTerrainFeaturePosition();
    void LocaleAndLanguageInit();
    void HandleButtonPresses();
    bool LoadInventoryMenu(int player, boost::shared_ptr<LocalPlayer> localPlayer, bool creative);
    bool LoadCreativeMenu(int player, boost::shared_ptr<LocalPlayer> localPlayer, bool creative);
    bool LoadEnchantingMenu(int player, boost::shared_ptr<Inventory> inventory, int x, int y, int z, Level* level,
                            const std::wstring& name);
    bool LoadFurnaceMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Container> container);
    bool LoadBrewingStandMenu(int player, boost::shared_ptr<Inventory> inventory,
                              boost::shared_ptr<Container> container);
    bool LoadContainerMenu(int player, boost::shared_ptr<Container> inventory, boost::shared_ptr<Container> container);
    bool LoadTrapMenu(int player, boost::shared_ptr<Container> inventory, boost::shared_ptr<Container> container);
    bool LoadCrafting2x2Menu(int player, boost::shared_ptr<LocalPlayer> localPlayer);
    bool LoadCrafting3x3Menu(int player, boost::shared_ptr<LocalPlayer> localPlayer, int x, int y, int z);
    bool LoadFireworksMenu(int player, boost::shared_ptr<LocalPlayer> localPlayer, int x, int y, int z);
    bool LoadSignEntryMenu(int player, boost::shared_ptr<SignBlockEntity> sign);
    bool LoadRepairingMenu(int player, boost::shared_ptr<Inventory> inventory, Level* level, int x, int y, int z);
    bool LoadTradingMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Merchant> merchant,
                         Level* level, const std::wstring& name);
    bool LoadShulkerBoxMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Container> container);
    bool LoadHopperMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Container> container);
    bool LoadHopperMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<MinecartHopper> hopper);
    bool LoadHorseMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Container> container,
                       boost::shared_ptr<AbstractHorse> horse);
    bool LoadBeaconMenu(int player, boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Container> container);
    bool LoadWritingBookMenu(int player, not_null_ptr<ItemInstance> item, bool signing,
                             const BlockPos* lectern = nullptr, int page = 0);
    bool IsConsoleChatCensored();
    uint32_t IsUUIDLocal(const std::wstring& uuid);
    bool GetTutorialMode(PlayerUID uid);
    bool GetTutorialMode(const boost::shared_ptr<Player>& player);
    void SetGameMode(eGameMode mode);
    void StartReloadingTexturePack();
    static void StaticCtor();
    void* GetXuiServerActionParam(int player);
    void SetDisconnectReason(DisconnectPacket::eDisconnectReason reason);
    uint8_t GetLoadSavesFromFolderEnabled();
    uint32_t GetOpacityTimer(int32_t player);
    void SetOpacityTimer(int32_t player);
    void TickOpacityTimer(int32_t player);
    bool IsLocalMultiplayerAvailable();
    static int NotificationsCallback(void* context, unsigned int notification, unsigned int value);
    void SetLiveLinkRequired(bool required);
    std::wstring GetDebugRootPath();
    uint32_t DebugSettingsOn();
    uint32_t DebugArtToolsOn();
    uint32_t DebugDevToolsOn();
    uint8_t DebugUIFocussed();
    void SetDebugUIFocussed(bool focussed);
    static uint32_t DebugInputCallback(void* context);
    void SetDebugSequence(const char* sequence);
    uint8_t DLCInstallPending();
    bool isXuidNotch(PlayerUID uid);
    bool isXuidDeadmau5(PlayerUID uid);
    void AddMemoryTextureFile(const std::wstring& path, unsigned char* data, unsigned int size);
    void GetMemFileDetails(const std::wstring& path, unsigned char** data, unsigned int* size);
    bool IsFileInMemoryTextures(const std::wstring& path);
    void GetTPD(int type, unsigned char** data, unsigned int* size);
    bool DefaultCapeExists();
    void RemoveLevelFromBannedLevelList(int player, PlayerUID uid, char* levelId);
    void SetUniqueMapName(char* name);
    uint64_t getArchiveFileSize(const std::wstring& path);
    static uint32_t tipFiltered(int32_t tipId);
    uint32_t GetNextTip(bool firstTipRequest, EMiniGameId miniGame);
    unsigned int GetHTMLColour(eMinecraftColour colour);
    unsigned int GetHTMLColor(eMinecraftColour colour);
    std::wstring GetSingleActionReplacement(int32_t pad, uint32_t buttons);
    std::wstring GetActionReplacement(int32_t pad, uint8_t action);
    std::wstring GetVKReplacement(int32_t pad, uint32_t key, bool small);
    std::wstring GetIconReplacement(uint32_t icon);
    std::wstring FormatHTMLString(int player, const std::wstring& value, int flags);
    float getAppTime();
    void AddTerrainFeaturePosition(_eTerrainFeatureType type, int x, int z);
    uint32_t GetTerrainFeaturePosition(_eTerrainFeatureType type, int* x, int* z);
    MOJANG_DATA* GetMojangDataForXuid(PlayerUID uid);
    uint8_t GetBanListCheck(int32_t player);
    bool AutosaveDue();
    uint32_t SecondsToAutosave();
    uint32_t GetExtraGameData(unsigned int& options, eGameExtraData type) const;
    uint32_t GetExtraGameData(eGameExtraData type);
    void SetResetNether(bool reset);
    uint8_t GetResetNether();
    void SetResetEnd(bool reset);
    uint8_t GetResetEnd();
    uint32_t CanRecordStatsAndAchievements(Stat* stat, arrayWithLength<unsigned char> players);
    void SetBaseSaveName(const std::wstring& name);
    void SetCurrentImageTextData(const PngTagData* data);
    void CurrentImageTextData(PngTagData* data);
    void ParseImageTextData(const unsigned char* image, unsigned int size, PngTagData* data);
    void GetImageTextData(const unsigned char* image, unsigned int size, PngTagData* data);
    unsigned int CreateImageTextData(unsigned char* image, const PngTagData* data);
    void processSchematics(LevelChunk* chunk);
    void processSchematicsLighting(LevelChunk* chunk);
    void loadDefaultGameRules();
    LevelRules* getLevelRules();
    LevelRuleset* getGameRuleDefinitions();
    LevelGenerationOptions* getLevelGenerationOptions();
    const wchar_t* GetGameRulesString(const std::wstring& key);
    bool ShouldCollideWith(Block* block);
    void UpdatePlayerInfo(uint8_t networkSmallId, int16_t playerColour, uint32_t privileges, uint8_t gameType);
    uint32_t GetPlayerColour(uint8_t networkSmallId);
    uint32_t GetPlayerPrivileges(uint8_t networkSmallId);
    uint8_t GetPlayerGameType(uint8_t networkSmallId);
    std::wstring getEntityName(uint64_t entityType, int32_t auxValue);
    void SetCorruptSaveDeleted(bool deleted);
    uint8_t GetCorruptSaveDeleted();
    void SetAdditionalSkinBoxes(unsigned int skinId, SKIN_BOX* boxes, unsigned int count, _SkinAdjustments adjustments);
    std::vector<ModelPart*>* SetAdditionalSkinBoxes(unsigned int skinId, std::vector<SKIN_BOX*>* boxes,
                                                    _SkinAdjustments adjustments);
    std::vector<ModelPart*>* GetAdditionalModelParts(unsigned int value);
    std::vector<SKIN_BOX*>* GetAdditionalSkinBoxes(unsigned int skinId);
    static unsigned int getSkinIdFromPath(const std::wstring& path);
    static std::wstring getSkinPathFromId(unsigned int id);
    void SetRequiredTexturePackID(uint32_t texturePackId);
    uint32_t GetRequiredTexturePackID() const;
    void getLocale(std::vector<std::wstring>& localeNames);
    void SetFirstMiniGameType(EMiniGameId type);
    EMiniGameId GetFirstMiniGameType();
    void ProcessFakePadValueA();
    bool IsVoiceChatEnabled();
    void HandleXuiActions();
    void SetSkinAdjustments(uint32_t id, _SkinAdjustments value);
    _SkinAdjustments GetSkinAdjustments(uint32_t id);

    static void ClearSignInChangeUsersMask();
    void HandleButtonPresses(int value);
    bool IntroRunning();
    void SetIntroRunning(bool value);
    static void DisplaySavingMessage(void* context, C4JStorage::ESavingMessage message, int value);
    uint32_t LoadCommandBlockMenu(int player, boost::shared_ptr<CommandBlockEntity> commandBlock);
    uint32_t GetGameMode();
    uint32_t GetGlobalXuiServerAction();
    void SetGlobalXuiServerAction(eXuiServerAction value);
    static void SetActionConfirmed(void* context);
    void SetLoadSavesFromFolderEnabled(bool value);
    bool GetWriteSavesToFolderEnabled();
    void SetWriteSavesToFolderEnabled(bool value);
    bool GetMobsDontAttackEnabled();
    void SetMobsDontAttackEnabled(bool value);
    bool GetUseDPadForDebug();
    void SetUseDPadForDebug(bool value);
    bool GetMobsDontTickEnabled();
    void SetMobsDontTickEnabled(bool value);
    bool GetFreezePlayers();
    void SetFreezePlayers(bool value);
    std::wstring toStringOptionsStatus(const C4JStorage::eOptionsCallback& status);
    void SetOptionsCallbackStatus(int profile, C4JStorage::eOptionsCallback status);
    uint32_t GetAdditionalModelParts(int value);
    static void PrimaryPlayerSignedOutReturned(void* context, int pad, C4JStorage::EMessageResult result);
    void RemoveMemoryTextureFile(const std::wstring& value);
    void AddMemoryTPDFile(int p1, unsigned char* p2, unsigned int p3);
    void RemoveMemoryTPDFile(int value);
    bool IsFileInTPD(int value);
    uint32_t GetTPDSize();
    uint32_t GetTPConfigVal(wchar_t* value);
    void AddLevelToBannedLevelList(int p1, PlayerUID p2, char* p3, bool p4);
    bool IsInBannedLevelList(int p1, PlayerUID p2, char* p3);
    std::wstring GetUniqueMapName();
    bool GetResourcesLoaded();
    void SetResourcesLoaded(bool value);
    uint32_t GetNotifications();
    uint32_t GetHTMLFontSize(EHTMLFontSize value);
    bool IsTerrainFeature(int p1, int p2);
    static void RegisterMojangData(wchar_t* p1, PlayerUID p2, wchar_t* p3, wchar_t* p4);
    static void RegisterConfigValues(wchar_t* p1, int p2);
    uint32_t getDebugGameRuleDefinitions();
    void setDebugGameRuleDefinitions(LevelRuleset* value);
    void RetrieveNextTMSPPContent();
    void TickTMSPPFilesRetrieved();
    void ClearTMSPPFilesRetrieved();
    void AddTMSPPFileTypeRequest(int p1, bool p2);
    static void TMSPPFileReturned(void* context, int result, int index, C4JStorage::TMSPP_FILEDATA* data,
                                  const char* name);
    bool GetBanListRead(int value);
    void SetBanListRead(int p1, bool p2);
    void ClearBanList(int value);
    bool IsLocaleKorea();
    uint32_t get_eMCLang(wchar_t* value);
    uint32_t get_xcLang(wchar_t* value);
    void SetFakePadValue(int p1, unsigned char p2);


private:
    struct XuiActionAndParam
    {
        eXuiAction action;
        void* param;
    };

public:
    std::vector<std::wstring> m_dlcSkinNames;
    DLCManager mDLCManager;
    unsigned int mGameDefinedDataChangedBitmask;

private:
    bool mDebugUIFocussed;
    PlayerUID mPrimaryPlayerUID;
    boost::unordered_map<PlayerUID, unsigned char*> mUnusedPlayerDataMap;
    boost::unordered_map<std::wstring, MEMDATA*> mMemoryTextureFiles;
    boost::unordered_map<int, MEMDATA*> mMemoryTPDFiles;
    C4JCriticalSection mMemoryTextureMutex;
    C4JCriticalSection mMemoryTPDMutex;
    C4JCriticalSection mPendingActionsMutex;
    C4JCriticalSection mActionMutex;
    std::vector<NOTIFICATION*> mNotifications;
    unsigned int mUnusedNotificationWords[2];
    bool mGameStarted;
    bool mUnusedStartedFlag;
    bool mAppPaused;
    bool mChangingSessionType;
    bool mReallyChangingSessionType;
    boost::unordered_map<PlayerUID, bool> mTutorialModes;
    unsigned int mUnusedTutorialWord;
    ArchiveFile* mMediaArchive;
    std::vector<BANNEDLISTDATA*>* mBannedListData[4];
    bool mUnusedBannedListFlag;
    unsigned int mGameMode;
    bool mLoadSavesFromFolderEnabled;
    bool mUnusedGameModeFlags[3];
    unsigned int mUnusedGameModeWord;
    unsigned int mDisconnectReason;
    XuiActionAndParam mXuiActions[4];
    std::vector<XuiActionAndParam> mPendingActions[4];
    unsigned int mTMSActions[4];
    eXuiAction mGlobalXuiAction;
    unsigned int mXuiServerActions[4];
    void* mXuiServerActionParams[4];
    unsigned int mUnusedServerActionWord;
    bool mLiveLinkRequired;
    bool mDebugInputEnabled;
    _LARGE_INTEGER mLastPerformanceCounter;
    long long mAccumulatedPerformanceCounter;
    float mAppTime;
    float mFrameTime;
    float mPerformanceCounterScale;
    unsigned int mUnusedTimeWord;
    int mTipRotationIndex;
    _TipData mTipData[5];
    C4JCriticalSection mTerrainFeaturePositionsMutex;
    std::vector<FEATURE_DATA*> mTerrainFeaturePositions;
    bool mUnusedDLCInstallFlag;
    bool mDLCInstallProcessCompleted;
    bool mDLCInstallPending;
    int mDLCInstallCount;
    int mDLCInstallIndex;
    _XUSER_SIGNIN_INFO mSignInInfo[4];
    arrayWithLength<unsigned char> mBanListBuffers[4];
    bool mUnusedBanListFlag;
    bool mBanListRead[4];
    char mUniqueMapName[14];
    bool mBanListCheck[4];
    unsigned int mAutosaveTime;
    int mOpacityTimer[4];
    PngTagData mCurrentImageTextData;

public:
    GameRuleManager mGameRuleManager;

private:
    unsigned char mUnusedPlayerInfoByte;
    unsigned char mPlayerIds[8];
    unsigned int mPlayerPrivileges[8];
    unsigned char mPlayerGameTypes[8];
    C4JCriticalSection mSaveNotificationMutex;
    int mSaveNotificationCount;
    std::vector<_TMSPPRequest*> mTMSPPRequests;
    C4JCriticalSection mAdditionalModelPartsMutex;
    C4JCriticalSection mAdditionalSkinBoxesMutex;
    C4JCriticalSection mSkinAdjustmentsMutex;
    C4JCriticalSection mUnusedMutex;
    bool mCorruptSaveDeleted;
    unsigned int mUnusedSaveWords[4];
    unsigned int mUnusedClearedWords[4];
    boost::unordered_map<unsigned int, std::vector<ModelPart*>*> mAdditionalModelParts;
    boost::unordered_map<unsigned int, std::vector<SKIN_BOX*>*> mAdditionalSkinBoxes;
    boost::unordered_map<unsigned int, _SkinAdjustments> mSkinAdjustments;
    bool mResetNether;
    bool mResetEnd;
    unsigned int mRequiredTexturePackID;
    unsigned int mOptionsCallbackStatus[4];
    boost::unordered_map<int, std::wstring> mLocaleToString;
    boost::unordered_map<std::wstring, int> mStringToLocale;
    boost::unordered_map<std::wstring, int> mStringToXcLocale;
    EMiniGameId mFirstMiniGameType;
    unsigned long long mFakePadValues[4];
    static unsigned int sPreviousSignInUsersMask;
};
extern CConsoleMinecraftApp gApp;
extern C4JStorage* gStorage;

uint32_t DeviceFullErrorReturn(void* context);
#endif
