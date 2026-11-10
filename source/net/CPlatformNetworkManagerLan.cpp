#include "net/CPlatformNetworkManagerLan.hpp"

#include <limits.h>
#include <string.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/GameModeOptions.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Socket.hpp"
#include "game/minigame/MiniGameDef.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientboundPreLoginPacket.hpp"
#include "game/network/DisconnectPacket.hpp"
#include "platform/C4JEventImpl.hpp"
#include "platform/C4JThreadImpl.hpp"
#include "platform/CInput.hpp"
#include "platform/CProfile.hpp"
#include "game/core/System.hpp"
#include "net/NetLog.hpp"
#include "net/NetMessages.hpp"
#include "net/NetworkPlayerLan.hpp"
#include "net/ServerList.hpp"

namespace
{
const unsigned short cSessionDataVersion = 0x881;
const int cMaxLocalPlayers = 4;
const int cAllUsersMask = (1 << cMaxLocalPlayers) - 1;
const int cRetireGraceMs = 1000;
const int64_t cUnjoinedTimeoutMs = 10000;
const int64_t cQueryTimeoutMs = 6000;
const int64_t cServerProbeIntervalMs = 5000;
const int64_t cBeaconRefreshMs = 1000;

struct LiveEntry { std::string address; std::wstring name; };
std::vector<LiveEntry> g_liveEntries;
std::string g_liveSnapshot;
std::string g_liveWorld;
int64_t g_liveUpdated = 0;
unsigned int g_liveRevision = 0;
unsigned int g_liveReportedRevision = 0;

FILE* LiveFile(const wchar_t* file, const wchar_t* mode)
{
#ifdef _WIN32
    const wchar_t* directory = _wgetenv(L"LCE_LIVE_DIR");
    if (directory == 0 || *directory == 0) return 0;
    const std::wstring path = std::wstring(directory) + L"\\" + file;
    FILE* result = 0;
    _wfopen_s(&result, path.c_str(), mode);
    return result;
#else
    (void)file; (void)mode;
    return 0;
#endif
}

std::wstring LiveName(const char* utf8)
{
    std::wstring result;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
    while (*p && result.size() < 31)
    {
        unsigned int code = *p++;
        if (code >= 0xc2 && code <= 0xdf && (p[0] & 0xc0) == 0x80)
            { code = ((code & 31) << 6) | (p[0] & 63); ++p; }
        else if (code >= 0xe0 && code <= 0xef && p[0] && p[1] && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80)
            { code = ((code & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
        else if (code >= 0x80) code = '?';
        result.push_back(static_cast<wchar_t>(code));
    }
    return result.empty() ? L"Live world" : result;
}

std::vector<LiveEntry> ReadLiveEntries(std::string& snapshot)
{
    std::vector<LiveEntry> entries;
    FILE* file = LiveFile(L"servers.txt", L"rb");
    if (!file) return entries;
    char line[512];
    while (entries.size() < 128 && fgets(line, sizeof(line), file))
    {
        snapshot += line;
        char* separator = strchr(line, '\t');
        if (!separator) continue;
        *separator++ = 0;
        char* end = 0;
        long port = strtol(line, &end, 10);
        if (*end || port <= 0 || port > 65535) continue;
        separator[strcspn(separator, "\r\n")] = 0;
        LiveEntry entry;
        entry.address = std::string("127.0.0.1:") + line;
        entry.name = LiveName(separator);
        entries.push_back(entry);
    }
    fclose(file);
    return entries;
}

bool SameName(const std::wstring& left, const std::wstring& right)
{
    if (left.size() != right.size())
        return false;
    for (unsigned int index = 0; index < left.size(); ++index)
        if (towlower(left[index]) != towlower(right[index]))
            return false;
    return true;
}

std::string Narrow(const std::wstring& text)
{
    std::string result;
    for (unsigned int index = 0; index < text.size(); ++index)
        result.push_back(text[index] < 0x80 ? (char)text[index] : '?');
    return result;
}

void CopyGamertag(GameSessionData::Gamertag& gamertag, const wchar_t* name)
{
    memset(gamertag.m_name, 0, sizeof(gamertag.m_name));
    if (name == 0)
        return;
    for (unsigned int index = 0; index + 1 < sizeof(gamertag.m_name) && name[index] != 0; ++index)
        gamertag.m_name[index] = name[index] < 0x80 ? (char)name[index] : '?';
}

void CopySessionDetails(FriendSessionInfo* info, const lcenet::SessionDescription& session)
{
    info->m_gameSessionData = session.data;
    info->m_motd = session.motd;
    info->m_icon = session.icon;
    info->m_iconHash = session.icon.empty() ? 0 : lcenet::DetailsHash(std::wstring(), session.icon);
    info->m_maxPlayers = session.maxPlayers;
    info->m_dedicated = session.dedicated;
}

void MergeBeaconSession(lcenet::SessionDescription& known, const lcenet::SessionDescription& beacon)
{
    const bool keepDetails = known.hasDetails && known.detailsHash == beacon.detailsHash;
    std::wstring motd;
    std::vector<unsigned char> icon;
    if (keepDetails)
    {
        motd.swap(known.motd);
        icon.swap(known.icon);
    }
    known = beacon;
    if (keepDetails)
    {
        known.motd.swap(motd);
        known.icon.swap(icon);
        known.hasDetails = true;
    }
}

int PingLevel(int pingMs)
{
    if (pingMs < 0)
        return -1;
    if (pingMs < 150)
        return 0;
    if (pingMs < 300)
        return 1;
    if (pingMs < 600)
        return 2;
    return pingMs < 1000 ? 3 : 4;
}
}

CPlatformNetworkManagerLan::CPlatformNetworkManagerLan()
    : m_gameNetworkManager(0),
      m_internalState(eState_Uninitialised),
      m_state(eState_Uninitialised),
      m_nextSmallId(0),
      m_host(false),
      m_inSession(false),
      m_localGame(false),
      m_privateGame(false),
      m_matchmakingSession(false),
      m_matchmaking(false),
      m_leavingGame(false),
      m_leavePending(false),
      m_sessionData(),
      m_sessionsUpdatedCallback(0),
      m_sessionsUpdatedCallbackData(0),
      m_connectPending(false),
      m_connectShowProgress(false),
      m_connectCallback(0),
      m_connectCallbackData(0),
      m_nextKnownSessionId(1),
      m_acceptThread(0),
      m_acceptStop(false),
      m_listening(false),
      m_listenPort(lcenet::kDefaultPort),
      m_nextSystemId(1),
      m_sessionNonce(0),
      m_hostLink(0),
      m_ownHostSystemId(0),
      m_joinMask(0),
      m_beaconRunning(false),
      m_beaconUpdated(0),
      m_scannerRevision(0),
      m_probeRevision(0),
      m_reportedProbeRevision(0),
      m_serverIconFixed(false)
{
    InitializeCriticalSection(&m_lock);
    InitializeCriticalSection(&m_acceptLock);
    for (int index = 0; index < cMaxLocalPlayers; ++index)
    {
        m_playerChangedCallbacks[index] = 0;
        m_playerChangedCallbackData[index] = 0;
    }
}

void CPlatformNetworkManagerLan::Initialise(CGameNetworkManager* manager, int)
{
    EnterCriticalSection(&m_lock);
    m_gameNetworkManager = manager;
    m_leavingGame = false;
    m_leavePending = false;
    m_localGame = false;
    m_sessionsUpdatedCallback = 0;
    m_sessionsUpdatedCallbackData = 0;
    for (int index = 0; index < cMaxLocalPlayers; ++index)
    {
        m_playerChangedCallbacks[index] = 0;
        m_playerChangedCallbackData[index] = 0;
    }
    SetState(eState_Idle);
    LeaveCriticalSection(&m_lock);
    gProfile->SetNetworkStatusWithoutSigninChange(true);
    lcenet::Startup();
    NET_LOG_INFO("LAN and internet play ready (port %u)", (unsigned int)m_listenPort);
}

void CPlatformNetworkManagerLan::Terminate()
{
    m_scanner.Stop();
    EnterCriticalSection(&m_lock);
    StopListening();
    if (m_beaconRunning)
    {
        m_beacon.Stop();
        m_beaconRunning = false;
    }
    for (unsigned int index = 0; index < m_systems.size(); ++index)
        RetireLink(m_systems[index].link, 0);
    m_systems.clear();
    if (m_hostLink != 0)
    {
        RetireLink(m_hostLink, 0);
        m_hostLink = 0;
    }
    for (unsigned int index = 0; index < m_sessionQueries.size(); ++index)
        RetireLink(m_sessionQueries[index].link, 0);
    m_sessionQueries.clear();
    LeaveCriticalSection(&m_lock);
    DeleteRetiredLinks();
}

int CPlatformNetworkManagerLan::GetJoiningReadyPercentage()
{
    return 100;
}

int CPlatformNetworkManagerLan::CorrectErrorIDS(int errorIds)
{
    return errorIds;
}

void CPlatformNetworkManagerLan::DoWork()
{
    DeleteRetiredLinks();

    bool leave = false;
    EnterCriticalSection(&m_lock);
    if (m_leavePending)
    {
        m_leavePending = false;
        leave = true;
    }
    LeaveCriticalSection(&m_lock);
    if (leave)
        LeaveRoom();

    UpdateSessionQueries();
    PumpLinks();
    FinishJoining();
    ProcessStateChanges();
    UpdateBeacon();
    const int64_t liveNow = lcenet::NowMs();
    if (liveNow - g_liveUpdated >= 1000)
    {
        g_liveUpdated = liveNow;
        char state[64] = "0";
        EnterCriticalSection(&m_lock);
        if (m_host && m_listening && !m_localGame && m_internalState == eState_Playing && m_sessionData.m_bGameStarted)
            snprintf(state, sizeof(state), "1\t%u\t%d", (unsigned int)m_listenPort, m_privateGame ? 1 : 0);
        LeaveCriticalSection(&m_lock);
        if (g_liveWorld != state)
        {
            FILE* file = LiveFile(L"world.txt", L"wb");
            if (file) { fputs(state, file); fclose(file); g_liveWorld = state; }
        }
        std::string snapshot;
        std::vector<LiveEntry> entries = ReadLiveEntries(snapshot);
        EnterCriticalSection(&m_lock);
        if (snapshot != g_liveSnapshot)
        {
            g_liveSnapshot.swap(snapshot);
            g_liveEntries.swap(entries);
            ++g_liveRevision;
        }
        LeaveCriticalSection(&m_lock);
    }
    UpdateLanSessions();

    EnterCriticalSection(&m_lock);
    bool connectPending = m_connectPending;
    bool connectShowProgress = m_connectShowProgress;
    int (*connectCallback)(void*, bool, int) = m_connectCallback;
    void* connectCallbackData = m_connectCallbackData;
    m_connectPending = false;
    m_connectShowProgress = false;
    m_connectCallback = 0;
    m_connectCallbackData = 0;
    LeaveCriticalSection(&m_lock);
    if (connectPending && connectShowProgress && connectCallback != 0)
        connectCallback(connectCallbackData, false, 0);
}

int CPlatformNetworkManagerLan::GetPlayerCount()
{
    EnterCriticalSection(&m_lock);
    int count = m_roomPlayers.empty() ? 1 : (int)m_roomPlayers.size();
    LeaveCriticalSection(&m_lock);
    return count;
}

int CPlatformNetworkManagerLan::GetOnlinePlayerCount()
{
    EnterCriticalSection(&m_lock);
    int count = 0;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
        if (!m_roomPlayers[index]->IsLocal())
            ++count;
    LeaveCriticalSection(&m_lock);
    return count;
}

int CPlatformNetworkManagerLan::GetLocalPlayerMask(int userIndex)
{
    return 1 << userIndex;
}

bool CPlatformNetworkManagerLan::AddLocalPlayerByUserIndex(int userIndex)
{
    if (userIndex < 0 || userIndex >= cMaxLocalPlayers)
        return false;
    EnterCriticalSection(&m_lock);
    if (!m_host)
    {
        bool sent = m_internalState == eState_Playing && m_hostLink != 0 && FindLocalPlayer(userIndex) == 0;
        if (sent)
        {
            m_joinMask |= 1 << userIndex;
            SendJoin(1 << userIndex);
        }
        LeaveCriticalSection(&m_lock);
        return sent;
    }
    if (m_roomPlayers.size() >= CGameNetworkManager::sInstance.GetMaxPlayers() || FindLocalPlayer(userIndex) != 0)
    {
        LeaveCriticalSection(&m_lock);
        return false;
    }
    NetworkPlayerLan* player = AddRoomPlayer(userIndex, false);
    HandlePlayerJoined(player);
    ResyncSessionPlayers();
    BroadcastRoomSync();
    UpdateLocalMultiplayer();
    LeaveCriticalSection(&m_lock);
    return true;
}

int CPlatformNetworkManagerLan::RemovePlayerOnSocketClosedThreadProc(void* data)
{
    PendingRemoval* removal = static_cast<PendingRemoval*>(data);
    removal->socket->mCloseEvent->WaitForSignal(-1);
    CPlatformNetworkManagerLan* manager = removal->manager;
    EnterCriticalSection(&manager->m_lock);
    bool present = manager->ContainsPlayer(removal->player);
    if (present && removal->player->GetSocket() == removal->socket)
        removal->player->SetSocket(0);
    LeaveCriticalSection(&manager->m_lock);
    delete removal->socket;
    int result = present ? manager->RemoveLocalPlayer(removal->player) : 1;
    delete removal;
    return result;
}

bool CPlatformNetworkManagerLan::RemoveLocalPlayerByUserIndex(int userIndex)
{
    EnterCriticalSection(&m_lock);
    NetworkPlayerLan* player = FindLocalPlayer(userIndex);
    Socket* socket = player != 0 ? player->GetSocket() : 0;
    LeaveCriticalSection(&m_lock);
    if (player == 0)
        return true;
    if (socket == 0)
        return RemoveRoomPlayer(userIndex);
    PendingRemoval* removal = new PendingRemoval;
    removal->manager = this;
    removal->player = player;
    removal->socket = socket;
    C4JThreadImpl* thread = new C4JThreadImpl(RemovePlayerOnSocketClosedThreadProc, removal,
                                              "RemovePlayerOnSocketClosed", 0);
    thread->SetProcessor(2);
    thread->SetDeleteOnExit(true);
    thread->Run();
    return true;
}

bool CPlatformNetworkManagerLan::AddLocalPlayerByPlayerUID(PlayerUID)
{
    return false;
}

INetworkPlayer* CPlatformNetworkManagerLan::GetLocalPlayerByUserIndex(int userIndex)
{
    EnterCriticalSection(&m_lock);
    INetworkPlayer* player = FindLocalPlayer(userIndex);
    LeaveCriticalSection(&m_lock);
    return player;
}

INetworkPlayer* CPlatformNetworkManagerLan::GetPlayerByIndex(int index)
{
    EnterCriticalSection(&m_lock);
    INetworkPlayer* player = index >= 0 && index < (int)m_roomPlayers.size() ? m_roomPlayers[index] : 0;
    LeaveCriticalSection(&m_lock);
    return player;
}

INetworkPlayer* CPlatformNetworkManagerLan::GetPlayerByXuid(PlayerUID uid)
{
    EnterCriticalSection(&m_lock);
    INetworkPlayer* player = 0;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        if (m_roomPlayers[index]->GetUID() == uid)
        {
            player = m_roomPlayers[index];
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
    return player;
}

INetworkPlayer* CPlatformNetworkManagerLan::GetPlayerBySmallId(unsigned char smallId)
{
    EnterCriticalSection(&m_lock);
    INetworkPlayer* player = 0;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        if (m_roomPlayers[index]->GetSmallId() == smallId)
        {
            player = m_roomPlayers[index];
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
    return player;
}

bool CPlatformNetworkManagerLan::ShouldMessageForFullSession()
{
    return false;
}

INetworkPlayer* CPlatformNetworkManagerLan::GetHostPlayer()
{
    EnterCriticalSection(&m_lock);
    INetworkPlayer* player = m_roomPlayers.empty() ? 0 : m_roomPlayers[0];
    LeaveCriticalSection(&m_lock);
    return player;
}

bool CPlatformNetworkManagerLan::IsHost()
{
    return m_host;
}

bool CPlatformNetworkManagerLan::JoinGameFromInviteInfo(int, int, const nn::fp::FriendPresence*)
{
    return false;
}

bool CPlatformNetworkManagerLan::LeaveGame(bool error)
{
    if (m_leavingGame)
        return true;
    m_leavingGame = true;
    CGameNetworkManager& manager = CGameNetworkManager::sInstance;
    INetworkPlayer* player = GetLocalPlayerByUserIndex(manager.GetPrimaryPad());
    if (player != 0)
    {
        Socket* socket = player->GetSocket();
        if (socket != 0)
        {
            socket->mCloseEvent->WaitForSignal(-1);
            if (IsInSession())
            {
                player = GetLocalPlayerByUserIndex(manager.GetPrimaryPad());
                if (player != 0)
                    player->SetSocket(0);
            }
            delete socket;
        }
    }
    if (m_host && manager.ServerStoppedValid())
    {
        manager.ServerStoppedWait();
        manager.ServerStoppedDestroy();
    }
    return _LeaveGame(error, true);
}

bool CPlatformNetworkManagerLan::IsInSession()
{
    return m_inSession;
}

bool CPlatformNetworkManagerLan::IsInGameplay()
{
    return m_state == eState_Playing;
}

bool CPlatformNetworkManagerLan::IsReadyToPlayOrIdle()
{
    EnterCriticalSection(&m_lock);
    bool ready = m_stateChanges.empty()
                 && (m_internalState == eState_Hosting || m_internalState == eState_Playing
                     || m_internalState == eState_Idle);
    LeaveCriticalSection(&m_lock);
    return ready;
}

bool CPlatformNetworkManagerLan::IsInStatsEnabledSession()
{
    return true;
}

bool CPlatformNetworkManagerLan::IsMatchmakingEnabled()
{
    return m_matchmaking;
}

bool CPlatformNetworkManagerLan::IsHandlingBootInvite()
{
    return false;
}

bool CPlatformNetworkManagerLan::SessionHasSpace(unsigned int extraPlayers)
{
    EnterCriticalSection(&m_lock);
    unsigned int count = m_roomPlayers.empty() ? 1 : (unsigned int)m_roomPlayers.size();
    LeaveCriticalSection(&m_lock);
    return count + extraPlayers <= CGameNetworkManager::sInstance.GetMaxPlayers();
}

bool CPlatformNetworkManagerLan::SessionHasMii(int userIndex)
{
    PlayerUID uid(0);
    gProfile->GetXUID(userIndex, &uid, true);
    EnterCriticalSection(&m_lock);
    bool found = false;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        if (m_roomPlayers[index]->GetUID() == uid)
        {
            found = true;
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
    return found;
}

void CPlatformNetworkManagerLan::SendInviteGUI(int)
{
    FILE* file = LiveFile(L"invite.txt", L"wb");
    if (file) { fputs("invite", file); fclose(file); }
}

bool CPlatformNetworkManagerLan::IsAddingPlayer()
{
    return false;
}

void CPlatformNetworkManagerLan::HostGame(int userIndexMask, bool onlineGame, bool privateGame, unsigned char gameMode,
                                         EMiniGameId miniGameId, unsigned int worldId, unsigned int* attributes)
{
    SetLocalGame(!onlineGame);
    SetPrivateGame(privateGame);
    SystemFlagReset();
    m_matchmakingSession = worldId != 0;
    userIndexMask |= GetLocalPlayerMask(CGameNetworkManager::sInstance.GetPrimaryPad());
    _HostGame(userIndexMask, gameMode, miniGameId, worldId, attributes);
}

int CPlatformNetworkManagerLan::JoinGame(FriendSessionInfo* sessionInfo, int userIndexMask, int, bool)
{
    EnterCriticalSection(&m_lock);
    if (m_internalState != eState_Idle)
    {
        LeaveCriticalSection(&m_lock);
        return eJoinFailedReason_SessionUnavailable;
    }
    int mask = 0;
    for (int index = 0; index < cMaxLocalPlayers; ++index)
        if ((userIndexMask & (1 << index)) != 0 && gProfile->IsSignedIn(index))
            mask |= 1 << index;
    if (mask == 0)
        mask = 1 << CGameNetworkManager::sInstance.GetPrimaryPad();

    DestroyRoomPlayers();
    m_host = false;
    m_inSession = true;
    m_joinMask = mask;
    m_ownHostSystemId = 0;
    SetState(eState_Joining);

    KnownSession* known = sessionInfo != 0 ? FindKnownSession(sessionInfo->m_sessionId) : 0;
    if (known == 0)
    {
        NET_LOG_WARN("Can't join: that game is no longer known");
        ResetToIdle(DisconnectPacket::eDisconnectReason_CannotJoinGame);
    }
    else
    {
        m_hostLink = new lcenet::Link(cHostSystemId, this);
        if (known->lan)
        {
            NET_LOG_INFO("Joining %s", known->address.Text().c_str());
            m_hostLink->Connect(known->address);
        }
        else
        {
            NET_LOG_INFO("Joining %s", known->addressText.c_str());
            m_hostLink->Connect(known->addressText);
        }
        SendJoin(mask);
    }
    LeaveCriticalSection(&m_lock);
    return 0;
}

int CPlatformNetworkManagerLan::FindAndJoinGame(int, int, EMiniGameId, unsigned int, unsigned int*, unsigned int*)
{
    return 1;
}

bool CPlatformNetworkManagerLan::SetLocalGame(bool localGame)
{
    if (m_state == eState_Idle)
        m_localGame = localGame;
    return true;
}

bool CPlatformNetworkManagerLan::IsLocalGame()
{
    return m_localGame;
}

unsigned short CPlatformNetworkManagerLan::GetListenPort()
{
    return m_listening ? m_listenPort : 0;
}

void CPlatformNetworkManagerLan::SetListenPort(unsigned short port)
{
    EnterCriticalSection(&m_lock);
    if (!m_listening)
        m_listenPort = port;
    LeaveCriticalSection(&m_lock);
}

bool CPlatformNetworkManagerLan::OpenToNetwork()
{
    EnterCriticalSection(&m_lock);
    if (m_host && m_localGame && m_internalState == eState_Playing)
    {
        StartListening();
        if (m_listening)
        {
            m_localGame = false;
            m_sessionData.m_bPublicGame = !m_privateGame;
        }
    }
    const bool open = m_host && !m_localGame && m_listening;
    LeaveCriticalSection(&m_lock);
    return open;
}

void CPlatformNetworkManagerLan::CloseToNetwork()
{
    EnterCriticalSection(&m_lock);
    if (m_host && !m_localGame)
    {
        StopListening();
        m_localGame = true;
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SetVisibleOnNetwork(bool visible)
{
    EnterCriticalSection(&m_lock);
    m_privateGame = !visible;
    m_sessionData.m_bPublicGame = visible;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SetMotd(const std::wstring& motd)
{
    EnterCriticalSection(&m_lock);
    m_motd = motd;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SetServerIcon(const std::vector<unsigned char>& icon, bool fixed)
{
    EnterCriticalSection(&m_lock);
    if (fixed || !m_serverIconFixed)
    {
        m_serverIcon = icon.size() <= lcenet::kMaxIconSize ? icon : std::vector<unsigned char>();
        m_serverIconFixed = fixed && !m_serverIcon.empty();
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SetPrivateGame(bool privateGame)
{
    m_privateGame = privateGame;
}

bool CPlatformNetworkManagerLan::IsPrivateGame()
{
    return m_privateGame;
}

bool CPlatformNetworkManagerLan::IsLeavingGame()
{
    return m_leavingGame;
}

void CPlatformNetworkManagerLan::ResetLeavingGame()
{
    m_leavingGame = false;
}

void CPlatformNetworkManagerLan::RegisterPlayerChangedCallback(int userIndex,
                                                              void (*callback)(void*, INetworkPlayer*, bool),
                                                              void* callbackData)
{
    if (userIndex < 0 || userIndex >= cMaxLocalPlayers)
        return;
    EnterCriticalSection(&m_lock);
    m_playerChangedCallbacks[userIndex] = callback;
    m_playerChangedCallbackData[userIndex] = callbackData;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::UnRegisterPlayerChangedCallback(int userIndex, void (*)(void*, INetworkPlayer*, bool),
                                                                void* callbackData)
{
    if (userIndex < 0 || userIndex >= cMaxLocalPlayers)
        return;
    EnterCriticalSection(&m_lock);
    if (m_playerChangedCallbackData[userIndex] == callbackData)
    {
        m_playerChangedCallbacks[userIndex] = 0;
        m_playerChangedCallbackData[userIndex] = 0;
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::HandleSignInChange()
{
}

bool CPlatformNetworkManagerLan::_RunNetworkGame()
{
    if (IsHost())
    {
        StartGame();
        EnterCriticalSection(&m_lock);
        m_sessionData.m_bGameStarted = true;
        LeaveCriticalSection(&m_lock);
    }
    return true;
}

bool CPlatformNetworkManagerLan::_LeaveGame(bool, bool)
{
    EnterCriticalSection(&m_lock);
    m_leavePending = true;
    m_leavingGame = true;
    LeaveCriticalSection(&m_lock);
    return true;
}

void CPlatformNetworkManagerLan::_HostGame(int userIndexMask, unsigned char gameMode, EMiniGameId miniGameId,
                                          unsigned int, unsigned int*)
{
    EnterCriticalSection(&m_lock);
    memset(&m_sessionData, 0, sizeof(m_sessionData));
    m_sessionData.m_version = cSessionDataVersion;
    m_sessionData.m_bPublicGame = !IsPrivateGame();
    m_sessionData.m_bGameStarted = false;
    m_sessionData.m_numPlayers = 0;
    m_sessionData.m_gameHostOptions = gApp.GetGameHostOption(eGameHostOption_All);
    m_sessionData.m_customGameModeHostOptions = Minecraft::GetInstance()->GetMiniGame()->GetHostOption(
        eCustomGameModeHostOption_All);
    m_sessionData.m_gameModeId = gameMode;
    m_sessionData.m_miniGameId = miniGameId;
    for (int index = 0; index < cMaxLocalPlayers; ++index)
        if ((userIndexMask & (1 << index)) != 0)
            ++m_sessionData.m_numPlayers;
    CreateRoom(CGameNetworkManager::sInstance.GetPrimaryPad(), userIndexMask);
    LeaveCriticalSection(&m_lock);
}

bool CPlatformNetworkManagerLan::_StartGame()
{
    StartGame();
    return true;
}

void CPlatformNetworkManagerLan::UpdateAndSetGameSessionData(INetworkPlayer*)
{
    if (m_leavingGame)
        return;
    INetworkPlayer* host = GetHostPlayer();
    EnterCriticalSection(&m_lock);
    if (host != 0)
        CopyGamertag(m_sessionData.m_hostGamertag, host->GetOnlineName());
    m_sessionData.m_gameHostOptions = gApp.GetGameHostOption(eGameHostOption_All);
    LeaveCriticalSection(&m_lock);
}

bool CPlatformNetworkManagerLan::RemoveLocalPlayer(INetworkPlayer* player)
{
    if (player->IsLocal())
        return RemoveRoomPlayer(player->GetUserIndex());
    return true;
}

void CPlatformNetworkManagerLan::SystemFlagAddPlayer(INetworkPlayer* player)
{
    EnterCriticalSection(&m_lock);
    PlayerFlags entry;
    entry.player = player;
    for (unsigned int index = 0; index < m_playerFlags.size(); ++index)
    {
        if (player->IsSameSystem(m_playerFlags[index].player))
        {
            entry.flags = m_playerFlags[index].flags;
            break;
        }
    }
    m_playerFlags.push_back(entry);
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SystemFlagSet(INetworkPlayer* player, int flag)
{
    if (flag < 0 || player == 0)
        return;
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < m_playerFlags.size(); ++index)
        if (player->IsSameSystem(m_playerFlags[index].player))
            m_playerFlags[index].flags.insert(flag);
    LeaveCriticalSection(&m_lock);
}

bool CPlatformNetworkManagerLan::SystemFlagGet(INetworkPlayer* player, int flag)
{
    if (flag < 0 || player == 0)
        return false;
    EnterCriticalSection(&m_lock);
    bool set = false;
    for (unsigned int index = 0; index < m_playerFlags.size(); ++index)
    {
        if (m_playerFlags[index].player == player)
        {
            set = m_playerFlags[index].flags.find(flag) != m_playerFlags[index].flags.end();
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
    return set;
}

void CPlatformNetworkManagerLan::SystemFlagClear(INetworkPlayer* player, int flag)
{
    if (flag < 0 || player == 0)
        return;
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < m_playerFlags.size(); ++index)
        if (player->IsSameSystem(m_playerFlags[index].player))
            m_playerFlags[index].flags.erase(flag);
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SystemFlagClearAll(INetworkPlayer* player)
{
    if (player == 0)
        return;
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < m_playerFlags.size(); ++index)
        if (m_playerFlags[index].player == player)
            m_playerFlags[index].flags.clear();
    LeaveCriticalSection(&m_lock);
}

std::wstring CPlatformNetworkManagerLan::GatherStats()
{
    return std::wstring(L"");
}

std::wstring CPlatformNetworkManagerLan::GatherRTTStats()
{
    return std::wstring(L"");
}

void CPlatformNetworkManagerLan::SetSessionTexturePackParentId(int id)
{
    EnterCriticalSection(&m_lock);
    m_sessionData.m_texturePackParentId = id;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SetSessionSubTexturePackId(int id)
{
    EnterCriticalSection(&m_lock);
    m_sessionData.m_subTexturePackId = (unsigned char)id;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SetSessionGameModeId(int id)
{
    EnterCriticalSection(&m_lock);
    m_sessionData.m_gameModeId = id;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::Notify(int, unsigned long)
{
}

std::vector<FriendSessionInfo*>* CPlatformNetworkManagerLan::GetSessionList(int, int, bool)
{
    std::vector<FriendSessionInfo*>* sessions = new std::vector<FriendSessionInfo*>();
    std::vector<lcenet::LanSession> found = m_scanner.Sessions();
    const std::vector<lcenet::SavedServer>& servers = lcenet::SavedServers().Servers();
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < servers.size(); ++index)
    {
        unsigned int id = RememberServer(lcenet::NarrowAddress(servers[index].address));
        FriendSessionInfo* info = new FriendSessionInfo;
        info->m_displayName = new wchar_t[32];
        memset(info->m_displayName, 0, 32 * sizeof(wchar_t));
        servers[index].name.copy(info->m_displayName, 31);
        info->m_displayNameLength = (unsigned char)wcslen(info->m_displayName);
        info->m_sessionId = id;
        memset(&info->m_searchResult, 0, sizeof(info->m_searchResult));
        info->m_searchResult.m_sessionId = id;
        info->m_savedServer = (int)index;
        KnownSession* known = FindKnownSession(id);
        if (known != 0)
        {
            info->m_serverStatus = known->status;
            info->m_pingMs = known->pingMs;
            CopySessionDetails(info, known->session);
        }
        sessions->push_back(info);
    }
    for (unsigned int index = 0; index < g_liveEntries.size(); ++index)
    {
        const LiveEntry& entry = g_liveEntries[index];
        unsigned int id = RememberServer(entry.address);
        FriendSessionInfo* info = new FriendSessionInfo;
        info->m_displayName = new wchar_t[32];
        memset(info->m_displayName, 0, 32 * sizeof(wchar_t));
        entry.name.copy(info->m_displayName, 31);
        info->m_displayNameLength = (unsigned char)wcslen(info->m_displayName);
        info->m_sessionId = id;
        memset(&info->m_searchResult, 0, sizeof(info->m_searchResult));
        info->m_searchResult.m_sessionId = id;
        KnownSession* known = FindKnownSession(id);
        if (known) { info->m_serverStatus = known->status; info->m_pingMs = known->pingMs; CopySessionDetails(info, known->session); }
        sessions->push_back(info);
    }
    for (unsigned int index = 0; index < found.size(); ++index)
    {
        const GameSessionData& data = found[index].session.data;
        if (!data.m_bGameStarted || data.m_version != cSessionDataVersion)
            continue;
        unsigned int id = RememberLanSession(found[index]);
        FriendSessionInfo* info = new FriendSessionInfo;
        const std::wstring& name = found[index].session.hostName;
        info->m_displayName = new wchar_t[32];
        memset(info->m_displayName, 0, 32 * sizeof(wchar_t));
        name.copy(info->m_displayName, 31);
        info->m_displayNameLength = (unsigned char)wcslen(info->m_displayName);
        info->m_sessionId = id;
        memset(&info->m_searchResult, 0, sizeof(info->m_searchResult));
        info->m_searchResult.m_sessionId = id;
        std::string ownerName = Narrow(name);
        ownerName.copy(info->m_searchResult.m_ownerName, sizeof(info->m_searchResult.m_ownerName) - 1);
        KnownSession* known = FindKnownSession(id);
        CopySessionDetails(info, known != 0 ? known->session : found[index].session);
        info->m_serverStatus = FriendSessionInfo::eServerStatus_Online;
        info->m_pingMs = known != 0 ? known->pingMs : -1;
        sessions->push_back(info);
    }
    LeaveCriticalSection(&m_lock);
    return sessions;
}

bool CPlatformNetworkManagerLan::GetGameSessionInfo(int, unsigned int, FriendSessionInfo*)
{
    return false;
}

void CPlatformNetworkManagerLan::SetSessionsUpdatedCallback(void (*callback)(void*), void* callbackData)
{
    EnterCriticalSection(&m_lock);
    m_sessionsUpdatedCallback = callback;
    m_sessionsUpdatedCallbackData = callbackData;
    LeaveCriticalSection(&m_lock);
    if (callback != 0)
        m_scanner.Start();
    else
        m_scanner.Stop();
}

void CPlatformNetworkManagerLan::GetFullFriendSessionInfo(FriendSessionInfo* info, void (*callback)(bool, void*),
                                                         void* callbackData)
{
    SessionQuery query;
    query.link = 0;
    query.knownId = info != 0 ? info->m_sessionId : 0;
    query.info = info;
    query.callback = callback;
    query.callbackData = callbackData;
    query.started = lcenet::NowMs();
    query.finished = true;
    query.found = false;

    std::vector<lcenet::LanSession> found = m_scanner.Sessions();
    EnterCriticalSection(&m_lock);
    KnownSession* known = info != 0 ? FindKnownSession(info->m_sessionId) : 0;
    if (known != 0 && known->lan)
    {
        for (unsigned int index = 0; index < found.size(); ++index)
            if (found[index].session.nonce == known->session.nonce
                && found[index].address.port == known->address.port)
                MergeBeaconSession(known->session, found[index].session);
        CopySessionDetails(info, known->session);
        query.found = true;
    }
    else if (known != 0)
    {
        StartSessionQuery(*known, info, callback, callbackData);
        LeaveCriticalSection(&m_lock);
        return;
    }
    m_sessionQueries.push_back(query);
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::StartSessionQuery(KnownSession& known, FriendSessionInfo* info,
                                                   void (*callback)(bool, void*), void* callbackData)
{
    SessionQuery query;
    query.knownId = known.id;
    query.info = info;
    query.callback = callback;
    query.callbackData = callbackData;
    query.started = lcenet::NowMs();
    query.finished = false;
    query.found = false;
    query.link = new lcenet::Link(0, this);
    query.link->Connect(known.addressText);
    lcenet::ByteWriter writer;
    writer.U16(lcenet::kProtocolVersion);
    query.link->SendControl(lcenet::kChannelQuery, writer.Data());
    known.probing = true;
    m_sessionQueries.push_back(query);
}

void CPlatformNetworkManagerLan::ProbeSavedServers()
{
    const std::vector<lcenet::SavedServer>& servers = lcenet::SavedServers().Servers();
    std::vector<lcenet::LanSession> found = m_scanner.Sessions();
    int64_t now = lcenet::NowMs();
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < servers.size(); ++index)
    {
        KnownSession* known = FindKnownSession(RememberServer(lcenet::NarrowAddress(servers[index].address)));
        if (known == 0 || known->probing)
            continue;
        if (known->lastProbe != 0 && now - known->lastProbe < cServerProbeIntervalMs)
            continue;
        StartSessionQuery(*known, 0, 0, 0);
    }
    for (unsigned int index = 0; index < g_liveEntries.size(); ++index)
    {
        KnownSession* known = FindKnownSession(RememberServer(g_liveEntries[index].address));
        if (known && !known->probing && (known->lastProbe == 0 || now - known->lastProbe >= cServerProbeIntervalMs))
            StartSessionQuery(*known, 0, 0, 0);
    }
    for (unsigned int index = 0; index < found.size(); ++index)
    {
        KnownSession* known = FindKnownSession(RememberLanSession(found[index]));
        if (known == 0 || known->probing)
            continue;
        if (known->session.hasDetails && known->session.detailsHash == found[index].session.detailsHash)
            continue;
        if (known->lastProbe != 0 && now - known->lastProbe < cServerProbeIntervalMs)
            continue;
        StartSessionQuery(*known, 0, 0, 0);
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::ForceFriendsSessionRefresh()
{
}

void CPlatformNetworkManagerLan::ClearFriendsSessions()
{
}

bool CPlatformNetworkManagerLan::AlreadyInSession(const nn::fp::FriendPresence*)
{
    return false;
}

bool CPlatformNetworkManagerLan::GetAgeRestricted()
{
    return false;
}

bool CPlatformNetworkManagerLan::usingAdhocMode()
{
    return false;
}

bool CPlatformNetworkManagerLan::setAdhocMode(bool)
{
    return false;
}

void CPlatformNetworkManagerLan::startAdhocMatching()
{
}

bool CPlatformNetworkManagerLan::isAdhocConnected()
{
    return false;
}

bool CPlatformNetworkManagerLan::connectToAdhoc()
{
    return false;
}

bool CPlatformNetworkManagerLan::isAdhocModeChangeInProgress()
{
    return false;
}

void CPlatformNetworkManagerLan::AttemptAdhocSignIn(int (*)(void*, bool, int), void*, bool)
{
}

bool CPlatformNetworkManagerLan::setLanMode(bool mode)
{
    return !mode;
}

bool CPlatformNetworkManagerLan::usingLanMode()
{
    return false;
}

bool CPlatformNetworkManagerLan::isLanConnected()
{
    return false;
}

bool CPlatformNetworkManagerLan::isLanModeChangeInProgress()
{
    return false;
}

bool CPlatformNetworkManagerLan::checkValidInviteData(const nn::fp::FriendPresence*)
{
    return true;
}

void CPlatformNetworkManagerLan::SetExitAndJoinFromInviteInProgress(bool)
{
}

void CPlatformNetworkManagerLan::CancelDisconnectFromExitAndJoin()
{
}

std::wstring CPlatformNetworkManagerLan::GetDisplayNameByGamertag(std::wstring gamertag)
{
    EnterCriticalSection(&m_lock);
    std::map<std::wstring, std::wstring>::const_iterator found = m_displayNames.find(gamertag);
    std::wstring name = found != m_displayNames.end() ? found->second : gamertag;
    LeaveCriticalSection(&m_lock);
    return name;
}

int CPlatformNetworkManagerLan::GetOutstandingAckCount(INetworkPlayer* player)
{
    if (player == 0)
        return 0;
    EnterCriticalSection(&m_lock);
    int count = 0;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
        if (m_roomPlayers[index] == player || m_roomPlayers[index]->IsSameSystem(player))
            count += m_roomPlayers[index]->PendingAcks();
    LeaveCriticalSection(&m_lock);
    return count;
}

void CPlatformNetworkManagerLan::TryToConnectOnline(int (*callback)(void*, bool, int), void* callbackData,
                                                   bool showProgress)
{
    CGameNetworkManager::sInstance.ResetLeavingGame();
    EnterCriticalSection(&m_lock);
    m_connectPending = true;
    m_connectShowProgress = showProgress;
    m_connectCallback = callback;
    m_connectCallbackData = callbackData;
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::AttemptPSNSignIn(int (*)(void*, bool, int), void*, bool, int)
{
}

void CPlatformNetworkManagerLan::RecvInviteGUI()
{
}

bool CPlatformNetworkManagerLan::UpdateInviteData(void*)
{
    return false;
}

void CPlatformNetworkManagerLan::CheckFriendsCanPlay(int userIndex, int& friendCount,
                                                    boost::shared_ptr<ClientboundPreLoginPacket> packet, int, int&,
                                                    int&, int&, int&)
{
    CGameNetworkManager& manager = CGameNetworkManager::sInstance;
    bool isPrimaryPad = userIndex == 0;
    if (!manager.IsLocalGame())
        friendCount = !manager.IsUGCRestrictedMode(isPrimaryPad ? 0xff : userIndex, packet->mMiniGameId);
}

void CPlatformNetworkManagerLan::LocalDataSend(NetworkPlayerLan* from, NetworkPlayerLan* to, const void* data,
                                              unsigned int size)
{
    if (m_state == eState_Ending)
        return;
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    if (to->IsHost())
    {
        Socket* socket = from->GetSocket();
        if (socket != 0)
            socket->pushDataToQueue(bytes, size, false);
    }
    else
    {
        Socket* socket = to->GetSocket();
        if (socket != 0)
            socket->pushDataToQueue(bytes, size, true);
    }
}

void CPlatformNetworkManagerLan::RemoteDataSend(NetworkPlayerLan* from, NetworkPlayerLan* to, const void* data,
                                               unsigned int size, bool requiresAck)
{
    EnterCriticalSection(&m_lock);
    if (m_host)
    {
        if (to->GetType() == NetworkPlayerLan::eType_Remote && ContainsPlayer(to))
        {
            lcenet::Link* link = FindSystemLink(to->GetSystemId());
            if (link != 0)
            {
                if (requiresAck)
                    to->AckRequested();
                link->Send((unsigned char)to->GetUserIndex(), requiresAck ? lcenet::kFlagAckRequest : 0, data, size);
            }
        }
    }
    else if (m_hostLink != 0 && from->IsLocal() && to->IsHost())
    {
        m_hostLink->Send((unsigned char)from->GetUserIndex(), 0, data, size);
    }
    LeaveCriticalSection(&m_lock);
}

int CPlatformNetworkManagerLan::GetSendQueueSize(NetworkPlayerLan*, INetworkPlayer* to, bool bytes)
{
    EnterCriticalSection(&m_lock);
    size_t total = 0;
    NetworkPlayerLan* target = static_cast<NetworkPlayerLan*>(to);
    if (m_host && target != 0 && target->GetType() == NetworkPlayerLan::eType_Remote && ContainsPlayer(target))
    {
        lcenet::Link* link = FindSystemLink(target->GetSystemId());
        if (link != 0)
            total = bytes ? link->QueuedBytes() : link->QueuedFrames();
    }
    else if (m_host)
    {
        for (unsigned int index = 0; index < m_systems.size(); ++index)
            total += bytes ? m_systems[index].link->QueuedBytes() : m_systems[index].link->QueuedFrames();
    }
    else if (m_hostLink != 0)
    {
        total = bytes ? m_hostLink->QueuedBytes() : m_hostLink->QueuedFrames();
    }
    LeaveCriticalSection(&m_lock);
    return total > (size_t)INT_MAX ? INT_MAX : (int)total;
}

int CPlatformNetworkManagerLan::GetSessionIndex(NetworkPlayerLan* player)
{
    EnterCriticalSection(&m_lock);
    int sessionIndex = 0;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        if (m_roomPlayers[index] == player)
        {
            sessionIndex = (int)index;
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
    return sessionIndex;
}

void CPlatformNetworkManagerLan::SetState(EState state, int reason)
{
    EnterCriticalSection(&m_lock);
    if (state == eState_Idle)
        m_matchmaking = false;
    if (state != m_internalState)
    {
        StateChange change;
        change.oldState = m_internalState;
        change.newState = state;
        change.reason = reason;
        m_stateChanges.push_back(change);
        m_internalState = state;
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::ProcessStateChanges()
{
    while (true)
    {
        EnterCriticalSection(&m_lock);
        if (m_stateChanges.empty())
        {
            LeaveCriticalSection(&m_lock);
            return;
        }
        StateChange change = m_stateChanges.front();
        LeaveCriticalSection(&m_lock);

        HandleStateChange(change.oldState, change.newState, change.reason);

        EnterCriticalSection(&m_lock);
        if (change.newState == eState_Idle)
            m_inSession = false;
        m_state = change.newState;
        m_stateChanges.pop_front();
        LeaveCriticalSection(&m_lock);
    }
}

void CPlatformNetworkManagerLan::HandleStateChange(EState oldState, EState newState, int reason)
{
    CGameNetworkManager& manager = CGameNetworkManager::sInstance;
    if (newState == eState_Hosting)
    {
        m_leavingGame = false;
        m_leavePending = false;
        manager.StateChange_AnyToHosting();
    }
    else if (newState == eState_Joining)
    {
        m_leavingGame = false;
        m_leavePending = false;
        manager.StateChange_AnyToJoining(false);
    }
    else if (newState == eState_Idle && oldState == eState_Joining)
    {
        manager.StateChange_JoiningToIdle(reason == DisconnectPacket::eDisconnectReason_GameFull
                                              ? eJoinFailedReason_SessionFull
                                              : eJoinFailedReason_ConnectionLost);
        if (reason != 0)
            Minecraft::GetInstance()->mDisconnectReasons[gProfile->GetPrimaryPad()] =
                (DisconnectPacket::eDisconnectReason)reason;
    }
    else if (newState == eState_Idle && oldState == eState_Hosting)
    {
        m_leavingGame = true;
    }
    else if (newState == eState_Starting)
    {
        manager.StateChange_AnyToStarting();
    }
    else if (newState == eState_Ending)
    {
        manager.StateChange_AnyToEnding(oldState == eState_Playing, false);
        if (m_host)
            m_leavingGame = true;
    }
}

void CPlatformNetworkManagerLan::CreateRoom(int primaryUserIndex, int userIndexMask)
{
    EnterCriticalSection(&m_lock);
    if (!m_roomPlayers.empty())
        DestroyRoomPlayers();
    m_inSession = true;
    m_host = true;
    m_matchmaking = m_matchmakingSession;
    SetState(eState_Hosting);
    HandlePlayerJoined(AddRoomPlayer(primaryUserIndex, true));
    for (int index = 0; index < cMaxLocalPlayers; ++index)
    {
        if (index == primaryUserIndex || (userIndexMask & (1 << index)) == 0)
            continue;
        HandlePlayerJoined(AddRoomPlayer(index, false));
    }
    ResyncSessionPlayers();
    m_sessionNonce = (uint32_t)(System::processTimeInNanoSecs() ^ (System::processTimeInNanoSecs() >> 32))
                     ^ (uint32_t)(uintptr_t)this;
    if (!m_localGame)
        StartListening();
    LeaveCriticalSection(&m_lock);
}

NetworkPlayerLan* CPlatformNetworkManagerLan::AddRoomPlayer(int userIndex, bool host)
{
    PlayerUID uid = GetPlayerUID(userIndex);
    std::wstring onlineName = gProfile->GetDisplayName(userIndex);
    std::wstring displayName(gProfile->GetMiiName(userIndex));
    NetworkPlayerLan* player = new NetworkPlayerLan(this, host ? NetworkPlayerLan::eType_Host : NetworkPlayerLan::eType_Local,
                                                  cLocalSystemId, userIndex, AllocateSmallId(), uid, onlineName,
                                                  displayName);
    m_roomPlayers.push_back(player);
    m_displayNames[onlineName] = displayName;
    return player;
}

bool CPlatformNetworkManagerLan::RemoveRoomPlayer(int userIndex)
{
    EnterCriticalSection(&m_lock);
    NetworkPlayerLan* player = 0;
    for (std::vector<NetworkPlayerLan*>::iterator it = m_roomPlayers.begin(); it != m_roomPlayers.end(); ++it)
    {
        if ((*it)->IsLocal() && (*it)->GetUserIndex() == userIndex)
        {
            player = *it;
            m_roomPlayers.erase(it);
            break;
        }
    }
    if (player == 0)
    {
        LeaveCriticalSection(&m_lock);
        UpdateLocalMultiplayer();
        return false;
    }
    if (!m_host && m_hostLink != 0)
    {
        lcenet::ByteWriter writer;
        writer.U8(1 << userIndex);
        m_hostLink->SendControl(lcenet::kChannelLeave, writer.Data());
        m_joinMask &= ~(1 << userIndex);
    }
    HandlePlayerLeaving(player);
    delete player;
    ResyncSessionPlayers();
    if (m_host)
        BroadcastRoomSync();
    UpdateLocalMultiplayer();
    LeaveCriticalSection(&m_lock);
    return true;
}

void CPlatformNetworkManagerLan::DestroyRoomPlayers()
{
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        SystemFlagRemovePlayer(m_roomPlayers[index]);
        delete m_roomPlayers[index];
    }
    m_roomPlayers.clear();
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::StartGame()
{
    SetState(eState_Starting);
    SetState(eState_Playing);
}

void CPlatformNetworkManagerLan::LeaveRoom()
{
    EnterCriticalSection(&m_lock);
    if (m_internalState == eState_Hosting || m_internalState == eState_Joining || m_internalState == eState_Starting
        || m_internalState == eState_Playing)
    {
        lcenet::ByteWriter writer;
        if (m_host)
        {
            writer.U8(cAllUsersMask);
            for (unsigned int index = 0; index < m_systems.size(); ++index)
            {
                if (m_systems[index].joined)
                    m_systems[index].link->SendControl(lcenet::kChannelLeave, writer.Data());
                RetireLink(m_systems[index].link, cRetireGraceMs);
            }
            m_systems.clear();
        }
        else if (m_hostLink != 0)
        {
            writer.U8(LocalPlayerMask());
            m_hostLink->SendControl(lcenet::kChannelLeave, writer.Data());
            RetireLink(m_hostLink, cRetireGraceMs);
            m_hostLink = 0;
        }
        SetState(eState_Leaving);
        SetState(eState_Ending);
        ResetToIdle();
    }
    else if (m_internalState == eState_Ending)
    {
        ResetToIdle();
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::ResetToIdle(int reason)
{
    EnterCriticalSection(&m_lock);
    m_host = false;
    m_nextSmallId = 0;
    StopListening();
    if (m_beaconRunning)
    {
        m_beacon.Stop();
        m_beaconRunning = false;
    }
    for (unsigned int index = 0; index < m_systems.size(); ++index)
        RetireLink(m_systems[index].link, 0);
    m_systems.clear();
    if (m_hostLink != 0)
    {
        RetireLink(m_hostLink, 0);
        m_hostLink = 0;
    }
    m_joinMask = 0;
    DestroyRoomPlayers();
    SetState(eState_Idle, reason);
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::HandlePlayerJoined(NetworkPlayerLan* player)
{
    CGameNetworkManager& manager = CGameNetworkManager::sInstance;
    bool localConnection = player->IsLocal();
    bool createSocket = (localConnection && !player->IsHost())
                        || (m_host && player->GetType() == NetworkPlayerLan::eType_Remote);

    manager.PlayerJoining(player);
    if (createSocket)
        manager.CreateSocket(player, localConnection);

    if (m_host)
    {
        manager.UpdateAndSetGameSessionData(0);
        SystemFlagAddPlayer(player);
    }

    for (int index = 0; index < cMaxLocalPlayers; ++index)
        if (m_playerChangedCallbacks[index] != 0)
            m_playerChangedCallbacks[index](m_playerChangedCallbackData[index], player, false);
}

void CPlatformNetworkManagerLan::HandlePlayerLeaving(NetworkPlayerLan* player)
{
    CGameNetworkManager& manager = CGameNetworkManager::sInstance;
    Socket* socket = player->GetSocket();
    if (socket != 0)
    {
        if (m_host)
            manager.CloseConnection(player);
        player->SetSocket(0);
    }

    if (m_host)
    {
        manager.UpdateAndSetGameSessionData(player);
        SystemFlagRemovePlayer(player);
    }

    manager.PlayerLeaving(player);
    for (int index = 0; index < cMaxLocalPlayers; ++index)
        if (m_playerChangedCallbacks[index] != 0)
            m_playerChangedCallbacks[index](m_playerChangedCallbackData[index], player, true);
}

void CPlatformNetworkManagerLan::ResyncSessionPlayers()
{
    EnterCriticalSection(&m_lock);
    m_sessionData.m_numPlayers = 0;
    for (int index = 0; index < GameSessionData::MAX_SESSION_PLAYERS; ++index)
    {
        if (index < (int)m_roomPlayers.size())
        {
            CopyGamertag(m_sessionData.m_playerGamertags[index], m_roomPlayers[index]->GetOnlineName());
            ++m_sessionData.m_numPlayers;
        }
        else
        {
            memset(m_sessionData.m_playerGamertags[index].m_name, 0, GameSessionData::MAX_GAMERTAG_LENGTH);
        }
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::UpdateLocalMultiplayer()
{
    int localPlayerCount = 0;
    for (int index = 0; index < cMaxLocalPlayers; ++index)
        if (GetLocalPlayerByUserIndex(index) != 0)
            ++localPlayerCount;
    g_pInput->SetLocalMultiplayer(localPlayerCount > 1);
}

void CPlatformNetworkManagerLan::SystemFlagRemovePlayer(INetworkPlayer* player)
{
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < m_playerFlags.size(); ++index)
    {
        if (m_playerFlags[index].player == player)
        {
            m_playerFlags[index] = m_playerFlags.back();
            m_playerFlags.pop_back();
            break;
        }
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::SystemFlagReset()
{
    EnterCriticalSection(&m_lock);
    m_playerFlags.clear();
    LeaveCriticalSection(&m_lock);
}

PlayerUID CPlatformNetworkManagerLan::GetPlayerUID(int userIndex)
{
    PlayerUID uid(0);
    gProfile->GetXUID(userIndex, &uid, true);
    if (uid == PlayerUID(0))
        gProfile->GetXUID(userIndex, &uid, false);
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        if (m_roomPlayers[index]->GetUID() == uid)
        {
            uid.flags |= PlayerUID::FlagMiiPrincipal;
            break;
        }
    }
    return uid;
}

bool CPlatformNetworkManagerLan::ContainsPlayer(NetworkPlayerLan* player)
{
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
        if (m_roomPlayers[index] == player)
            return true;
    return false;
}

NetworkPlayerLan* CPlatformNetworkManagerLan::FindLocalPlayer(int userIndex)
{
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
        if (m_roomPlayers[index]->IsLocal() && m_roomPlayers[index]->GetUserIndex() == userIndex)
            return m_roomPlayers[index];
    return 0;
}

void CPlatformNetworkManagerLan::OnLinkData(lcenet::Link& link, unsigned char channel, unsigned char flags,
                                           const unsigned char* data, size_t size)
{
    EnterCriticalSection(&m_lock);
    if (m_host)
    {
        NetworkPlayerLan* from = FindSystem(&link) != 0 ? FindSystemPlayer(link.SystemId(), channel) : 0;
        if (from != 0)
        {
            if ((flags & lcenet::kFlagAck) != 0)
            {
                from->AckReceived();
            }
            else if (size > 0)
            {
                Socket* socket = from->GetSocket();
                if (socket != 0)
                    socket->pushDataToQueue(data, (unsigned int)size, false);
            }
        }
    }
    else if (&link == m_hostLink)
    {
        NetworkPlayerLan* to = FindLocalPlayer(channel);
        if (to != 0 && size > 0)
        {
            Socket* socket = to->GetSocket();
            if (socket != 0)
                socket->pushDataToQueue(data, (unsigned int)size, true);
        }
        if ((flags & lcenet::kFlagAckRequest) != 0)
            link.Send(channel, lcenet::kFlagAck, 0, 0);
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::StartListening()
{
    if (m_listening)
        return;
    std::string error;
    if (!m_listenSocket.Listen(m_listenPort, error, getenv("LCE_LIVE_DIR") != 0))
    {
        NET_LOG_WARN("Other players can't join this game: %s", error.c_str());
        return;
    }
    m_listening = true;
    m_acceptStop = false;
    m_acceptThread = new C4JThreadImpl(&CPlatformNetworkManagerLan::AcceptEntry, this, "NetAccept", 0x10000);
    m_acceptThread->Run();
    NET_LOG_INFO("Accepting players on port %u", (unsigned int)m_listenPort);
}

void CPlatformNetworkManagerLan::StopListening()
{
    if (!m_listening)
        return;
    EnterCriticalSection(&m_acceptLock);
    m_acceptStop = true;
    LeaveCriticalSection(&m_acceptLock);
    if (m_acceptThread != 0)
    {
        m_acceptThread->WaitForCompletion(-1);
        delete m_acceptThread;
        m_acceptThread = 0;
    }
    m_listenSocket.Close();
    m_listening = false;
    EnterCriticalSection(&m_acceptLock);
    for (unsigned int index = 0; index < m_acceptedSockets.size(); ++index)
        delete m_acceptedSockets[index].socket;
    m_acceptedSockets.clear();
    LeaveCriticalSection(&m_acceptLock);
}

int CPlatformNetworkManagerLan::AcceptEntry(void* manager)
{
    static_cast<CPlatformNetworkManagerLan*>(manager)->AcceptLoop();
    return 0;
}

bool CPlatformNetworkManagerLan::AcceptStopped()
{
    EnterCriticalSection(&m_acceptLock);
    bool stop = m_acceptStop;
    LeaveCriticalSection(&m_acceptLock);
    return stop;
}

void CPlatformNetworkManagerLan::AcceptLoop()
{
    while (!AcceptStopped())
    {
        AcceptedSocket accepted;
        accepted.socket = new lcenet::TcpSocket();
        if (m_listenSocket.Accept(100, *accepted.socket, accepted.peer))
        {
            EnterCriticalSection(&m_acceptLock);
            m_acceptedSockets.push_back(accepted);
            LeaveCriticalSection(&m_acceptLock);
        }
        else
        {
            delete accepted.socket;
        }
    }
}

void CPlatformNetworkManagerLan::AdoptAcceptedSockets()
{
    std::vector<AcceptedSocket> accepted;
    EnterCriticalSection(&m_acceptLock);
    accepted.swap(m_acceptedSockets);
    LeaveCriticalSection(&m_acceptLock);
    if (accepted.empty())
        return;
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < accepted.size(); ++index)
    {
        if (m_host && m_listening)
        {
            RemoteSystem system;
            system.link = new lcenet::Link(m_nextSystemId++, this);
            system.joined = false;
            system.query = false;
            system.link->Adopt(*accepted[index].socket, accepted[index].peer);
            m_systems.push_back(system);
            NET_LOG_DEBUG("Connection from %s", accepted[index].peer.Text().c_str());
        }
        delete accepted[index].socket;
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::PumpLinks()
{
    AdoptAcceptedSockets();

    std::vector<lcenet::Link*> links;
    EnterCriticalSection(&m_lock);
    for (unsigned int index = 0; index < m_systems.size(); ++index)
        links.push_back(m_systems[index].link);
    if (m_hostLink != 0)
        links.push_back(m_hostLink);
    LeaveCriticalSection(&m_lock);

    for (unsigned int index = 0; index < links.size(); ++index)
    {
        lcenet::Link* link = links[index];
        EnterCriticalSection(&m_lock);
        lcenet::Frame frame;
        while ((link == m_hostLink || FindSystem(link) != 0) && link->TakeControl(frame))
            HandleControl(link, frame);
        if ((link == m_hostLink || FindSystem(link) != 0) && link->IsClosed())
            HandleLinkClosed(link);
        LeaveCriticalSection(&m_lock);
    }

    EnterCriticalSection(&m_lock);
    int64_t now = lcenet::NowMs();
    for (unsigned int index = 0; index < m_systems.size();)
    {
        RemoteSystem& system = m_systems[index];
        if (!system.joined && !system.query && now - system.link->OpenedAt() > cUnjoinedTimeoutMs)
        {
            RetireLink(system.link, 0);
            m_systems.erase(m_systems.begin() + index);
        }
        else
        {
            ++index;
        }
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::HandleControl(lcenet::Link* link, lcenet::Frame& frame)
{
    if (m_host)
    {
        if (frame.channel == lcenet::kChannelJoin)
            HostHandleJoin(link, frame.payload);
        else if (frame.channel == lcenet::kChannelLeave)
            HostHandleLeave(link, frame.payload);
        else if (frame.channel == lcenet::kChannelQuery)
            HostHandleQuery(link);
    }
    else if (link == m_hostLink)
    {
        if (frame.channel == lcenet::kChannelRoomSync)
            ClientHandleRoomSync(frame.payload);
        else if (frame.channel == lcenet::kChannelJoinFailed)
            ClientHandleJoinFailed(frame.payload);
        else if (frame.channel == lcenet::kChannelLeave)
            ClientLostHost(DisconnectPacket::eDisconnectReason_Quit);
    }
}

void CPlatformNetworkManagerLan::HandleLinkClosed(lcenet::Link* link)
{
    if (link == m_hostLink)
    {
        std::string error = link->Error();
        NET_LOG_WARN("Lost the connection to the host: %s", error.empty() ? "closed" : error.c_str());
        ClientLostHost(m_internalState == eState_Joining ? DisconnectPacket::eDisconnectReason_CannotJoinGame
                                                         : DisconnectPacket::eDisconnectReason_ConnectionClosed);
        return;
    }
    for (unsigned int index = 0; index < m_systems.size(); ++index)
    {
        if (m_systems[index].link != link)
            continue;
        bool joined = m_systems[index].joined;
        unsigned int systemId = link->SystemId();
        m_systems.erase(m_systems.begin() + index);
        RetireLink(link, 0);
        if (joined)
        {
            NET_LOG_INFO("%s disconnected", link->Peer().Text().c_str());
            RemoveSystemPlayers(systemId, cAllUsersMask);
        }
        return;
    }
}

void CPlatformNetworkManagerLan::HostHandleJoin(lcenet::Link* link, const std::vector<unsigned char>& payload)
{
    RemoteSystem* system = FindSystem(link);
    if (system == 0)
        return;

    struct Joiner
    {
        int userIndex;
        PlayerUID uid;
        std::wstring onlineName;
        std::wstring displayName;
    };

    lcenet::ByteReader reader(payload);
    unsigned int protocol = reader.U16();
    unsigned int version = reader.U16();
    int mask = (int)reader.U8() & cAllUsersMask;
    std::vector<Joiner> joiners;
    for (int userIndex = 0; userIndex < cMaxLocalPlayers; ++userIndex)
    {
        if ((mask & (1 << userIndex)) == 0)
            continue;
        Joiner joiner;
        joiner.userIndex = userIndex;
        joiner.uid = reader.Uid();
        joiner.onlineName = reader.Text();
        joiner.displayName = reader.Text();
        if (FindSystemPlayer(link->SystemId(), userIndex) == 0)
            joiners.push_back(joiner);
    }

    int reason = 0;
    if (!reader.Ok())
        reason = DisconnectPacket::eDisconnectReason_CannotJoinGame;
    else if (protocol < lcenet::kProtocolVersion || version < cSessionDataVersion)
        reason = DisconnectPacket::eDisconnectReason_ClientOutOfDate;
    else if (protocol > lcenet::kProtocolVersion || version > cSessionDataVersion)
        reason = DisconnectPacket::eDisconnectReason_HostOutOfDate;
    else if (m_localGame || !m_listening || m_internalState != eState_Playing)
        reason = DisconnectPacket::eDisconnectReason_CannotJoinGame;
    else if (m_roomPlayers.size() + joiners.size() > CGameNetworkManager::sInstance.GetMaxPlayers())
        reason = DisconnectPacket::eDisconnectReason_GameFull;
    for (unsigned int index = 0; index < joiners.size() && reason == 0; ++index)
    {
        for (unsigned int player = 0; player < m_roomPlayers.size() && reason == 0; ++player)
        {
            if (m_roomPlayers[player]->GetUID() == joiners[index].uid)
                reason = DisconnectPacket::eDisconnectReason_MiiInUse;
            else if (SameName(m_roomPlayers[player]->GetOnlineName(), joiners[index].onlineName))
                reason = DisconnectPacket::eDisconnectReason_NameInUse;
        }
        for (unsigned int other = 0; other < index && reason == 0; ++other)
            if (SameName(joiners[other].onlineName, joiners[index].onlineName))
                reason = DisconnectPacket::eDisconnectReason_NameInUse;
    }

    if (reason != 0)
    {
        NET_LOG_INFO("Turned away a join from %s (reason %d)", link->Peer().Text().c_str(), reason);
        SendJoinFailed(link, mask, reason);
        if (!system->joined)
        {
            for (unsigned int index = 0; index < m_systems.size(); ++index)
                if (m_systems[index].link == link)
                    m_systems.erase(m_systems.begin() + index);
            RetireLink(link, cRetireGraceMs);
        }
        return;
    }
    if (joiners.empty())
        return;

    system->joined = true;
    std::vector<NetworkPlayerLan*> added;
    for (unsigned int index = 0; index < joiners.size(); ++index)
    {
        const Joiner& joiner = joiners[index];
        NetworkPlayerLan* player = new NetworkPlayerLan(this, NetworkPlayerLan::eType_Remote, link->SystemId(),
                                                      joiner.userIndex, AllocateSmallId(), joiner.uid,
                                                      joiner.onlineName, joiner.displayName);
        m_roomPlayers.push_back(player);
        m_displayNames[joiner.onlineName] = joiner.displayName;
        added.push_back(player);
        NET_LOG_INFO("%s joined from %s", Narrow(joiner.onlineName).c_str(), link->Peer().Text().c_str());
    }
    for (unsigned int index = 0; index < added.size(); ++index)
        HandlePlayerJoined(added[index]);
    ResyncSessionPlayers();
    BroadcastRoomSync();
}

void CPlatformNetworkManagerLan::HostHandleLeave(lcenet::Link* link, const std::vector<unsigned char>& payload)
{
    lcenet::ByteReader reader(payload);
    int mask = (int)reader.U8() & cAllUsersMask;
    if (!reader.Ok())
        return;
    unsigned int systemId = link->SystemId();
    RemoveSystemPlayers(systemId, mask);
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
        if (m_roomPlayers[index]->GetType() == NetworkPlayerLan::eType_Remote
            && m_roomPlayers[index]->GetSystemId() == systemId)
            return;
    for (unsigned int index = 0; index < m_systems.size(); ++index)
    {
        if (m_systems[index].link == link)
        {
            m_systems.erase(m_systems.begin() + index);
            RetireLink(link, 0);
            return;
        }
    }
}

void CPlatformNetworkManagerLan::HostHandleQuery(lcenet::Link* link)
{
    lcenet::ByteWriter writer;
    lcenet::WriteSession(writer, DescribeSession(), true);
    link->SendControl(lcenet::kChannelSessionInfo, writer.Data());
    for (unsigned int index = 0; index < m_systems.size(); ++index)
    {
        if (m_systems[index].link == link && !m_systems[index].joined)
        {
            m_systems.erase(m_systems.begin() + index);
            RetireLink(link, cRetireGraceMs);
            return;
        }
    }
}

void CPlatformNetworkManagerLan::ClientHandleRoomSync(const std::vector<unsigned char>& payload)
{
    struct Entry
    {
        unsigned int systemId;
        int userIndex;
        unsigned char smallId;
        PlayerUID uid;
        std::wstring onlineName;
        std::wstring displayName;
    };

    lcenet::ByteReader reader(payload);
    unsigned int ownSystemId = reader.U32();
    unsigned int count = reader.U8();
    std::vector<Entry> entries;
    for (unsigned int index = 0; index < count; ++index)
    {
        Entry entry;
        entry.systemId = reader.U32();
        entry.userIndex = (int)reader.U8();
        entry.smallId = (unsigned char)reader.U8();
        entry.uid = reader.Uid();
        entry.onlineName = reader.Text();
        entry.displayName = reader.Text();
        entries.push_back(entry);
    }
    if (!reader.Ok() || entries.empty() || entries[0].systemId != 0)
    {
        NET_LOG_WARN("Ignored a player list the host sent that couldn't be read");
        return;
    }
    m_ownHostSystemId = ownSystemId;

    std::vector<NetworkPlayerLan*> previous = m_roomPlayers;
    std::vector<NetworkPlayerLan*> next;
    std::vector<NetworkPlayerLan*> added;
    for (unsigned int index = 0; index < entries.size(); ++index)
    {
        const Entry& entry = entries[index];
        if (entry.userIndex < 0 || entry.userIndex >= cMaxLocalPlayers)
            continue;
        unsigned int systemId = entry.systemId == ownSystemId ? cLocalSystemId : entry.systemId + 1;
        NetworkPlayerLan::EType type = index == 0 ? NetworkPlayerLan::eType_Host
                                                 : systemId == cLocalSystemId ? NetworkPlayerLan::eType_Local
                                                                              : NetworkPlayerLan::eType_Remote;
        if (type == NetworkPlayerLan::eType_Local
            && ((m_joinMask & (1 << entry.userIndex)) == 0 || !gProfile->IsSignedIn(entry.userIndex)))
            continue;

        NetworkPlayerLan* player = 0;
        for (unsigned int known = 0; known < previous.size(); ++known)
        {
            NetworkPlayerLan* candidate = previous[known];
            if (candidate->GetType() == type && candidate->GetSystemId() == systemId
                && candidate->GetUserIndex() == entry.userIndex && candidate->GetSmallId() == entry.smallId)
            {
                player = candidate;
                previous.erase(previous.begin() + known);
                break;
            }
        }
        if (player == 0)
        {
            player = new NetworkPlayerLan(this, type, systemId, entry.userIndex, entry.smallId, entry.uid,
                                         entry.onlineName, entry.displayName);
            added.push_back(player);
        }
        player->SetUID(entry.uid);
        m_displayNames[entry.onlineName] = entry.displayName;
        next.push_back(player);
    }

    m_roomPlayers = next;
    for (unsigned int index = 0; index < added.size(); ++index)
        HandlePlayerJoined(added[index]);
    for (unsigned int index = 0; index < previous.size(); ++index)
    {
        HandlePlayerLeaving(previous[index]);
        delete previous[index];
    }
    ResyncSessionPlayers();
    UpdateLocalMultiplayer();

    if ((m_internalState == eState_Starting || m_internalState == eState_Playing)
        && FindLocalPlayer(CGameNetworkManager::sInstance.GetPrimaryPad()) == 0)
        ClientLostHost(DisconnectPacket::eDisconnectReason_ConnectionClosed);
}

void CPlatformNetworkManagerLan::ClientHandleJoinFailed(const std::vector<unsigned char>& payload)
{
    lcenet::ByteReader reader(payload);
    int mask = (int)reader.U8() & cAllUsersMask;
    int reason = (int)reader.U8();
    if (!reader.Ok())
        return;
    NET_LOG_WARN("The host turned the join down (reason %d)", reason);
    if (m_internalState == eState_Joining)
    {
        ClientLostHost(reason);
        return;
    }
    for (int userIndex = 0; userIndex < cMaxLocalPlayers; ++userIndex)
    {
        if ((mask & (1 << userIndex)) == 0)
            continue;
        m_joinMask &= ~(1 << userIndex);
        CGameNetworkManager::sInstance.AddLocalPlayerFailed(userIndex, reason);
    }
}

void CPlatformNetworkManagerLan::ClientLostHost(int reason)
{
    if (m_hostLink != 0)
    {
        RetireLink(m_hostLink, 0);
        m_hostLink = 0;
    }
    if (m_internalState == eState_Joining)
    {
        ResetToIdle(reason);
    }
    else if (m_internalState == eState_Starting || m_internalState == eState_Playing)
    {
        if (gApp.GetDisconnectReason() == 0)
            gApp.SetDisconnectReason((DisconnectPacket::eDisconnectReason)reason);
        SetState(eState_Ending);
    }
}

void CPlatformNetworkManagerLan::SendJoin(int userIndexMask)
{
    if (m_hostLink == 0)
        return;
    lcenet::ByteWriter writer;
    writer.U16(lcenet::kProtocolVersion);
    writer.U16(cSessionDataVersion);
    writer.U8(userIndexMask);
    for (int userIndex = 0; userIndex < cMaxLocalPlayers; ++userIndex)
    {
        if ((userIndexMask & (1 << userIndex)) == 0)
            continue;
        PlayerUID uid(0);
        gProfile->GetXUID(userIndex, &uid, true);
        if (uid == PlayerUID(0))
            gProfile->GetXUID(userIndex, &uid, false);
        writer.Uid(uid);
        writer.Text(gProfile->GetDisplayName(userIndex));
        writer.Text(std::wstring(gProfile->GetMiiName(userIndex)));
    }
    m_hostLink->SendControl(lcenet::kChannelJoin, writer.Data());
}

void CPlatformNetworkManagerLan::SendJoinFailed(lcenet::Link* link, int userIndexMask, int reason)
{
    lcenet::ByteWriter writer;
    writer.U8(userIndexMask);
    writer.U8(reason);
    link->SendControl(lcenet::kChannelJoinFailed, writer.Data());
}

void CPlatformNetworkManagerLan::BroadcastRoomSync()
{
    for (unsigned int index = 0; index < m_systems.size(); ++index)
    {
        if (!m_systems[index].joined)
            continue;
        lcenet::ByteWriter writer;
        writer.U32(m_systems[index].link->SystemId());
        writer.U8((unsigned int)m_roomPlayers.size());
        for (unsigned int player = 0; player < m_roomPlayers.size(); ++player)
        {
            NetworkPlayerLan* entry = m_roomPlayers[player];
            writer.U32(entry->IsLocal() ? 0 : entry->GetSystemId());
            writer.U8(entry->GetUserIndex());
            writer.U8(entry->GetSmallId());
            writer.Uid(entry->GetUID());
            writer.Text(entry->GetOnlineName());
            writer.Text(entry->GetDisplayName());
        }
        m_systems[index].link->SendControl(lcenet::kChannelRoomSync, writer.Data());
    }
}

void CPlatformNetworkManagerLan::RemoveSystemPlayers(unsigned int systemId, int userIndexMask)
{
    std::vector<NetworkPlayerLan*> removed;
    for (std::vector<NetworkPlayerLan*>::iterator it = m_roomPlayers.begin(); it != m_roomPlayers.end();)
    {
        NetworkPlayerLan* player = *it;
        if (player->GetType() == NetworkPlayerLan::eType_Remote && player->GetSystemId() == systemId
            && (userIndexMask & (1 << player->GetUserIndex())) != 0)
        {
            it = m_roomPlayers.erase(it);
            removed.push_back(player);
        }
        else
        {
            ++it;
        }
    }
    if (removed.empty())
        return;
    for (unsigned int index = 0; index < removed.size(); ++index)
    {
        NET_LOG_INFO("%s left the game", Narrow(removed[index]->GetOnlineName()).c_str());
        HandlePlayerLeaving(removed[index]);
        delete removed[index];
    }
    ResyncSessionPlayers();
    BroadcastRoomSync();
}

void CPlatformNetworkManagerLan::RetireLink(lcenet::Link* link, int graceMs)
{
    if (link == 0)
        return;
    if (graceMs > 0)
        link->CloseAfterFlush();
    else
        link->Close();
    RetiredLink retired;
    retired.link = link;
    retired.deadline = lcenet::NowMs() + graceMs;
    m_retiredLinks.push_back(retired);
}

void CPlatformNetworkManagerLan::DeleteRetiredLinks()
{
    std::vector<lcenet::Link*> finished;
    EnterCriticalSection(&m_lock);
    int64_t now = lcenet::NowMs();
    for (unsigned int index = 0; index < m_retiredLinks.size();)
    {
        if (m_retiredLinks[index].link->IsClosed() || now >= m_retiredLinks[index].deadline)
        {
            finished.push_back(m_retiredLinks[index].link);
            m_retiredLinks.erase(m_retiredLinks.begin() + index);
        }
        else
        {
            ++index;
        }
    }
    LeaveCriticalSection(&m_lock);
    for (unsigned int index = 0; index < finished.size(); ++index)
        delete finished[index];
}

void CPlatformNetworkManagerLan::FinishJoining()
{
    EnterCriticalSection(&m_lock);
    if (m_internalState == eState_Joining && !m_host && m_hostLink != 0 && !m_roomPlayers.empty())
    {
        bool everyone = true;
        for (int userIndex = 0; userIndex < cMaxLocalPlayers; ++userIndex)
            if ((m_joinMask & (1 << userIndex)) != 0 && FindLocalPlayer(userIndex) == 0)
                everyone = false;
        if (everyone)
        {
            NET_LOG_INFO("Joined %s", m_hostLink->Peer().Text().c_str());
            SetState(eState_Starting);
            SetState(eState_Playing);
        }
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::UpdateBeacon()
{
    EnterCriticalSection(&m_lock);
    bool advertise = getenv("LCE_LIVE_DIR") == 0 && m_host && m_listening && !m_localGame && m_internalState == eState_Playing
                     && m_sessionData.m_bPublicGame && m_sessionData.m_bGameStarted;
    if (advertise)
    {
        int64_t now = lcenet::NowMs();
        if (!m_beaconRunning || now - m_beaconUpdated > cBeaconRefreshMs)
        {
            m_beacon.SetSession(DescribeSession());
            m_beaconUpdated = now;
        }
        if (!m_beaconRunning)
        {
            m_beacon.Start();
            m_beaconRunning = true;
        }
    }
    else if (m_beaconRunning)
    {
        m_beacon.Stop();
        m_beaconRunning = false;
    }
    LeaveCriticalSection(&m_lock);
}

void CPlatformNetworkManagerLan::UpdateSessionQueries()
{
    std::vector<SessionQuery> finished;
    EnterCriticalSection(&m_lock);
    int64_t now = lcenet::NowMs();
    for (unsigned int index = 0; index < m_sessionQueries.size();)
    {
        SessionQuery& query = m_sessionQueries[index];
        if (!query.finished)
        {
            lcenet::Frame frame;
            while (!query.finished && query.link->TakeControl(frame))
            {
                if (frame.channel != lcenet::kChannelSessionInfo)
                    continue;
                lcenet::ByteReader reader(frame.payload);
                lcenet::SessionDescription session;
                unsigned int version = 0;
                KnownSession* known = FindKnownSession(query.knownId);
                if (lcenet::ReadSession(reader, session, &version))
                {
                    if (query.info != 0)
                        CopySessionDetails(query.info, session);
                    if (known != 0)
                    {
                        lcenet::ByteWriter before;
                        lcenet::ByteWriter after;
                        lcenet::WriteSession(before, known->session, true);
                        lcenet::WriteSession(after, session, true);
                        const int64_t connectedAt = query.link->ConnectedAt();
                        const int pingMs = connectedAt != 0 && frame.receivedAt >= connectedAt
                                               ? static_cast<int>(frame.receivedAt - connectedAt)
                                               : -1;
                        if (!known->answered || before.Data() != after.Data()
                            || known->status != FriendSessionInfo::eServerStatus_Online
                            || PingLevel(known->pingMs) != PingLevel(pingMs))
                            ++m_probeRevision;
                        known->session = session;
                        known->answered = true;
                        known->status = FriendSessionInfo::eServerStatus_Online;
                        known->pingMs = pingMs;
                    }
                    query.found = true;
                }
                else if (known != 0 && version != 0)
                {
                    const int status = version > lcenet::kProtocolVersion
                                           ? FriendSessionInfo::eServerStatus_ClientOutdated
                                           : FriendSessionInfo::eServerStatus_ServerOutdated;
                    if (known->status != status)
                        ++m_probeRevision;
                    known->status = status;
                    known->pingMs = -1;
                }
                query.finished = true;
            }
            if (!query.finished && (query.link->IsClosed() || now - query.started > cQueryTimeoutMs))
                query.finished = true;
        }
        if (query.finished)
        {
            KnownSession* known = query.link != 0 ? FindKnownSession(query.knownId) : 0;
            if (known != 0)
            {
                if (!known->lan && !query.found)
                {
                    const bool incompatible = known->status == FriendSessionInfo::eServerStatus_ClientOutdated
                                              || known->status == FriendSessionInfo::eServerStatus_ServerOutdated;
                    if (known->answered || (!incompatible && known->status != FriendSessionInfo::eServerStatus_Offline))
                        ++m_probeRevision;
                    known->answered = false;
                    if (!incompatible)
                    {
                        known->status = FriendSessionInfo::eServerStatus_Offline;
                        known->pingMs = -1;
                    }
                }
                known->probing = false;
                known->lastProbe = now;
            }
            RetireLink(query.link, 0);
            finished.push_back(query);
            m_sessionQueries.erase(m_sessionQueries.begin() + index);
        }
        else
        {
            ++index;
        }
    }
    LeaveCriticalSection(&m_lock);
    for (unsigned int index = 0; index < finished.size(); ++index)
        if (finished[index].callback != 0)
            finished[index].callback(finished[index].found, finished[index].callbackData);
}

void CPlatformNetworkManagerLan::UpdateLanSessions()
{
    if (!m_scanner.IsRunning())
        return;
    ProbeSavedServers();
    unsigned int revision = m_scanner.Revision();
    EnterCriticalSection(&m_lock);
    bool changed = revision != m_scannerRevision || m_probeRevision != m_reportedProbeRevision
                   || g_liveRevision != g_liveReportedRevision;
    g_liveReportedRevision = g_liveRevision;
    m_scannerRevision = revision;
    m_reportedProbeRevision = m_probeRevision;
    void (*callback)(void*) = m_sessionsUpdatedCallback;
    void* callbackData = m_sessionsUpdatedCallbackData;
    LeaveCriticalSection(&m_lock);
    if (changed && callback != 0)
        callback(callbackData);
}

lcenet::SessionDescription CPlatformNetworkManagerLan::DescribeSession()
{
    lcenet::SessionDescription session;
    session.port = m_listenPort;
    session.nonce = m_sessionNonce;
    if (!m_roomPlayers.empty())
        session.hostName = m_roomPlayers[0]->GetOnlineName();
    session.data = m_sessionData;
    CopyGamertag(session.data.m_hostGamertag, session.hostName.c_str());
    session.motd = m_motd;
    session.icon = m_serverIcon;
    session.detailsHash = lcenet::DetailsHash(session.motd, session.icon);
    session.hasDetails = true;
    session.maxPlayers = static_cast<unsigned char>(CGameNetworkManager::sInstance.GetMaxPlayers());
    session.dedicated = CMinecraftApp::IsDedicatedServer();
    return session;
}

CPlatformNetworkManagerLan::KnownSession* CPlatformNetworkManagerLan::FindKnownSession(unsigned int id)
{
    for (unsigned int index = 0; index < m_knownSessions.size(); ++index)
        if (m_knownSessions[index].id == id)
            return &m_knownSessions[index];
    return 0;
}

unsigned int CPlatformNetworkManagerLan::RememberLanSession(const lcenet::LanSession& session)
{
    for (unsigned int index = 0; index < m_knownSessions.size(); ++index)
    {
        KnownSession& known = m_knownSessions[index];
        if (known.lan && known.session.nonce == session.session.nonce && known.address.port == session.address.port)
        {
            known.address = session.address;
            known.addressText = session.address.Text();
            MergeBeaconSession(known.session, session.session);
            return known.id;
        }
    }
    KnownSession known;
    known.id = m_nextKnownSessionId++;
    known.lan = true;
    known.address = session.address;
    known.addressText = session.address.Text();
    known.session = session.session;
    known.status = FriendSessionInfo::eServerStatus_Online;
    m_knownSessions.push_back(known);
    return known.id;
}

unsigned int CPlatformNetworkManagerLan::RememberServerAddress(const std::string& address)
{
    EnterCriticalSection(&m_lock);
    unsigned int id = RememberServer(address);
    LeaveCriticalSection(&m_lock);
    return id;
}

unsigned int CPlatformNetworkManagerLan::RememberServer(const std::string& address)
{
    for (unsigned int index = 0; index < m_knownSessions.size(); ++index)
        if (!m_knownSessions[index].lan && m_knownSessions[index].addressText == address)
            return m_knownSessions[index].id;
    KnownSession known;
    known.id = m_nextKnownSessionId++;
    known.lan = false;
    known.addressText = address;
    m_knownSessions.push_back(known);
    return known.id;
}

CPlatformNetworkManagerLan::RemoteSystem* CPlatformNetworkManagerLan::FindSystem(lcenet::Link* link)
{
    for (unsigned int index = 0; index < m_systems.size(); ++index)
        if (m_systems[index].link == link)
            return &m_systems[index];
    return 0;
}

lcenet::Link* CPlatformNetworkManagerLan::FindSystemLink(unsigned int systemId)
{
    for (unsigned int index = 0; index < m_systems.size(); ++index)
        if (m_systems[index].link->SystemId() == systemId)
            return m_systems[index].link;
    return 0;
}

NetworkPlayerLan* CPlatformNetworkManagerLan::FindSystemPlayer(unsigned int systemId, int userIndex)
{
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
    {
        NetworkPlayerLan* player = m_roomPlayers[index];
        if (player->GetType() == NetworkPlayerLan::eType_Remote && player->GetSystemId() == systemId
            && player->GetUserIndex() == userIndex)
            return player;
    }
    return 0;
}

unsigned char CPlatformNetworkManagerLan::AllocateSmallId()
{
    for (int attempt = 0; attempt < 256; ++attempt)
    {
        unsigned char id = m_nextSmallId++;
        bool used = false;
        for (unsigned int index = 0; index < m_roomPlayers.size() && !used; ++index)
            used = m_roomPlayers[index]->GetSmallId() == id;
        if (!used)
            return id;
    }
    return m_nextSmallId++;
}

int CPlatformNetworkManagerLan::LocalPlayerMask()
{
    int mask = 0;
    for (unsigned int index = 0; index < m_roomPlayers.size(); ++index)
        if (m_roomPlayers[index]->GetType() == NetworkPlayerLan::eType_Local)
            mask |= 1 << m_roomPlayers[index]->GetUserIndex();
    return mask;
}
