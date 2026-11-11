#ifndef NET_CPLATFORMNETWORKMANAGERLAN_HPP
#define NET_CPLATFORMNETWORKMANAGERLAN_HPP

#include <deque>
#include <map>
#include <string>
#include <vector>

#include <boost/unordered_set.hpp>

#include "game/core/CriticalSection.hpp"
#include "game/core/GameSessionData.hpp"
#include "game/network/CPlatformNetworkManager.hpp"
#include "game/world/generation/PlayerUID.hpp"
#include "net/NetDiscovery.hpp"
#include "net/NetLink.hpp"

class C4JThreadImpl;
class CGameNetworkManager;
class NetworkPlayerLan;
class Socket;

class CPlatformNetworkManagerLan : public CPlatformNetworkManager, private lcenet::LinkHandler
{
public:
    static const unsigned int cLocalSystemId = 0;
    static const unsigned int cHostSystemId = 1;

    enum EState
    {
        eState_Uninitialised,
        eState_Idle,
        eState_Hosting,
        eState_Joining,
        eState_Starting,
        eState_Playing,
        eState_Leaving,
        eState_Ending
    };

    CPlatformNetworkManagerLan();

    virtual void Initialise(CGameNetworkManager* manager, int maxSystemFlags);
    virtual void Terminate();
    virtual int GetJoiningReadyPercentage();
    virtual int CorrectErrorIDS(int errorIds);
    virtual void DoWork();
    virtual int GetPlayerCount();
    virtual int GetOnlinePlayerCount();
    virtual int GetLocalPlayerMask(int userIndex);
    virtual bool AddLocalPlayerByUserIndex(int userIndex);
    virtual bool RemoveLocalPlayerByUserIndex(int userIndex);
    virtual bool AddLocalPlayerByPlayerUID(PlayerUID uid);
    virtual INetworkPlayer* GetLocalPlayerByUserIndex(int userIndex);
    virtual INetworkPlayer* GetPlayerByIndex(int index);
    virtual INetworkPlayer* GetPlayerByXuid(PlayerUID uid);
    virtual INetworkPlayer* GetPlayerBySmallId(unsigned char smallId);
    virtual bool ShouldMessageForFullSession();
    virtual INetworkPlayer* GetHostPlayer();
    virtual bool IsHost();
    virtual bool JoinGameFromInviteInfo(int userIndex, int userIndexMask, const nn::fp::FriendPresence* presence);
    virtual bool LeaveGame(bool error);
    virtual bool IsInSession();
    virtual bool IsInGameplay();
    virtual bool IsReadyToPlayOrIdle();
    virtual bool IsInStatsEnabledSession();
    virtual bool IsMatchmakingEnabled();
    virtual bool IsHandlingBootInvite();
    virtual bool SessionHasSpace(unsigned int extraPlayers);
    virtual bool SessionHasMii(int userIndex);
    virtual void SendInviteGUI(int userIndex);
    virtual bool IsAddingPlayer();
    virtual void HostGame(int userIndexMask, bool onlineGame, bool privateGame, unsigned char gameMode,
                          EMiniGameId miniGameId, unsigned int worldId, unsigned int* attributes);
    virtual int JoinGame(FriendSessionInfo* sessionInfo, int userIndex, int userIndexMask, bool matchmaking);
    virtual int FindAndJoinGame(int userIndexMask, int searchFlags, EMiniGameId miniGameId, unsigned int attributeCount,
                                unsigned int* attributeValues, unsigned int* attributeIds);
    virtual bool SetLocalGame(bool localGame);
    virtual bool IsLocalGame();
    virtual unsigned short GetListenPort();
    virtual void SetListenPort(unsigned short port);
    virtual void SetMotd(const std::wstring& motd);
    virtual void SetServerIcon(const std::vector<unsigned char>& icon, bool fixed);
    virtual bool OpenToNetwork();
    virtual void CloseToNetwork();
    virtual void SetVisibleOnNetwork(bool visible);
    virtual void SetPrivateGame(bool privateGame);
    virtual bool IsPrivateGame();
    virtual bool IsLeavingGame();
    virtual void ResetLeavingGame();
    virtual void RegisterPlayerChangedCallback(int userIndex, void (*callback)(void*, INetworkPlayer*, bool),
                                               void* callbackData);
    virtual void UnRegisterPlayerChangedCallback(int userIndex, void (*callback)(void*, INetworkPlayer*, bool),
                                                 void* callbackData);
    virtual void HandleSignInChange();
    virtual bool _RunNetworkGame();
    virtual bool _LeaveGame(bool error, bool endSession);
    virtual void _HostGame(int userIndexMask, unsigned char gameMode, EMiniGameId miniGameId, unsigned int worldId,
                           unsigned int* attributes);
    virtual bool _StartGame();
    virtual void UpdateAndSetGameSessionData(INetworkPlayer* player);
    virtual bool RemoveLocalPlayer(INetworkPlayer* player);
    virtual void SystemFlagAddPlayer(INetworkPlayer* player);
    virtual void SystemFlagSet(INetworkPlayer* player, int flag);
    virtual bool SystemFlagGet(INetworkPlayer* player, int flag);
    virtual void SystemFlagClear(INetworkPlayer* player, int flag);
    virtual void SystemFlagClearAll(INetworkPlayer* player);
    virtual std::wstring GatherStats();
    virtual std::wstring GatherRTTStats();
    virtual void SetSessionTexturePackParentId(int id);
    virtual void SetSessionSubTexturePackId(int id);
    virtual void SetSessionGameModeId(int id);
    virtual void Notify(int notification, unsigned long param);
    virtual std::vector<FriendSessionInfo*>* GetSessionList(int userIndex, int maxSessions, bool friendsOnly);
    virtual bool GetGameSessionInfo(int userIndex, unsigned int sessionId, FriendSessionInfo* info);
    virtual void SetSessionsUpdatedCallback(void (*callback)(void*), void* callbackData);
    virtual unsigned int RememberServerAddress(const std::string& address);
    virtual void GetFullFriendSessionInfo(FriendSessionInfo* info, void (*callback)(bool, void*), void* callbackData);
    virtual void ForceFriendsSessionRefresh();
    virtual void ClearFriendsSessions();
    virtual bool AlreadyInSession(const nn::fp::FriendPresence* presence);
    virtual bool GetAgeRestricted();
    virtual bool usingAdhocMode();
    virtual bool setAdhocMode(bool mode);
    virtual void startAdhocMatching();
    virtual bool isAdhocConnected();
    virtual bool connectToAdhoc();
    virtual bool isAdhocModeChangeInProgress();
    virtual void AttemptAdhocSignIn(int (*callback)(void*, bool, int), void* callbackData, bool showProgress);
    virtual bool setLanMode(bool mode);
    virtual bool usingLanMode();
    virtual bool isLanConnected();
    virtual bool isLanModeChangeInProgress();
    virtual bool checkValidInviteData(const nn::fp::FriendPresence* presence);
    virtual void SetExitAndJoinFromInviteInProgress(bool inProgress);
    virtual void CancelDisconnectFromExitAndJoin();
    virtual std::wstring GetDisplayNameByGamertag(std::wstring gamertag);
    virtual int GetOutstandingAckCount(INetworkPlayer* player);
    virtual void TryToConnectOnline(int (*callback)(void*, bool, int), void* callbackData, bool showProgress);
    virtual void AttemptPSNSignIn(int (*callback)(void*, bool, int), void* callbackData, bool showProgress,
                                  int userIndex);
    virtual void RecvInviteGUI();
    virtual bool UpdateInviteData(void* inviteData);
    virtual void CheckFriendsCanPlay(int userIndex, int& friendCount,
                                     boost::shared_ptr<ClientboundPreLoginPacket> packet, int titleVersion,
                                     int& canPlayCount, int& cannotPlayCount, int& blockedCount, int& unknownCount);

    void LocalDataSend(NetworkPlayerLan* from, NetworkPlayerLan* to, const void* data, unsigned int size);
    void RemoteDataSend(NetworkPlayerLan* from, NetworkPlayerLan* to, const void* data, unsigned int size,
                        bool requiresAck);
    int GetSendQueueSize(NetworkPlayerLan* from, INetworkPlayer* to, bool bytes);
    int GetSessionIndex(NetworkPlayerLan* player);

private:
    struct StateChange
    {
        EState oldState;
        EState newState;
        int reason;
    };

    struct KnownSession
    {
        KnownSession()
            : id(0), lan(false), answered(false), probing(false), lastProbe(0),
              status(FriendSessionInfo::eServerStatus_Pinging), pingMs(-1)
        {
        }

        unsigned int id;
        bool lan;
        lcenet::Address address;
        std::string addressText;
        lcenet::SessionDescription session;
        bool answered;
        bool probing;
        int64_t lastProbe;
        int status;
        int pingMs;
    };

    struct SessionQuery
    {
        lcenet::Link* link;
        unsigned int knownId;
        FriendSessionInfo* info;
        void (*callback)(bool, void*);
        void* callbackData;
        int64_t started;
        bool finished;
        bool found;
    };

    struct RemoteSystem
    {
        lcenet::Link* link;
        bool joined;
        bool query;
    };

    struct RetiredLink
    {
        lcenet::Link* link;
        int64_t deadline;
    };

    struct AcceptedSocket
    {
        lcenet::TcpSocket* socket;
        lcenet::Address peer;
    };

    struct PlayerFlags
    {
        INetworkPlayer* player;
        boost::unordered_set<int> flags;
    };

    struct PendingRemoval
    {
        CPlatformNetworkManagerLan* manager;
        NetworkPlayerLan* player;
        Socket* socket;
    };

    static int RemovePlayerOnSocketClosedThreadProc(void* data);

    virtual void OnLinkData(lcenet::Link& link, unsigned char channel, unsigned char flags, const unsigned char* data,
                            size_t size);

    void StartListening();
    void StopListening();
    static int AcceptEntry(void* manager);
    void AcceptLoop();
    bool AcceptStopped();
    void AdoptAcceptedSockets();
    void PumpLinks();
    void HandleControl(lcenet::Link* link, lcenet::Frame& frame);
    void HandleLinkClosed(lcenet::Link* link);
    void HostHandleJoin(lcenet::Link* link, const std::vector<unsigned char>& payload);
    void HostHandleLeave(lcenet::Link* link, const std::vector<unsigned char>& payload);
    void HostHandleQuery(lcenet::Link* link);
    void ClientHandleRoomSync(const std::vector<unsigned char>& payload);
    void ClientHandleJoinFailed(const std::vector<unsigned char>& payload);
    void ClientLostHost(int reason);
    void SendJoin(int userIndexMask);
    void SendJoinFailed(lcenet::Link* link, int userIndexMask, int reason);
    void BroadcastRoomSync();
    void RemoveSystemPlayers(unsigned int systemId, int userIndexMask);
    void RetireLink(lcenet::Link* link, int graceMs);
    void DeleteRetiredLinks();
    void FinishJoining();
    void UpdateBeacon();
    void UpdateSessionQueries();
    void UpdateLanSessions();
    void ProbeSavedServers();
    void StartSessionQuery(KnownSession& known, FriendSessionInfo* info, void (*callback)(bool, void*),
                           void* callbackData);
    lcenet::SessionDescription DescribeSession();
    KnownSession* FindKnownSession(unsigned int id);
    unsigned int RememberLanSession(const lcenet::LanSession& session);
    unsigned int RememberServer(const std::string& address);
    RemoteSystem* FindSystem(lcenet::Link* link);
    lcenet::Link* FindSystemLink(unsigned int systemId);
    NetworkPlayerLan* FindSystemPlayer(unsigned int systemId, int userIndex);
    unsigned char AllocateSmallId();
    int LocalPlayerMask();

    void SetState(EState state, int reason = 0);
    void ProcessStateChanges();
    void HandleStateChange(EState oldState, EState newState, int reason);
    void CreateRoom(int primaryUserIndex, int userIndexMask);
    NetworkPlayerLan* AddRoomPlayer(int userIndex, bool host);
    bool RemoveRoomPlayer(int userIndex);
    void DestroyRoomPlayers();
    void StartGame();
    void LeaveRoom();
    void ResetToIdle(int reason = 0);
    void HandlePlayerJoined(NetworkPlayerLan* player);
    void HandlePlayerLeaving(NetworkPlayerLan* player);
    void ResyncSessionPlayers();
    void UpdateLocalMultiplayer();
    void SystemFlagRemovePlayer(INetworkPlayer* player);
    void SystemFlagReset();
    PlayerUID GetPlayerUID(int userIndex);
    bool ContainsPlayer(NetworkPlayerLan* player);
    NetworkPlayerLan* FindLocalPlayer(int userIndex);

    C4JCriticalSection m_lock;
    CGameNetworkManager* m_gameNetworkManager;
    EState m_internalState;
    EState m_state;
    std::deque<StateChange> m_stateChanges;
    std::vector<NetworkPlayerLan*> m_roomPlayers;
    std::map<std::wstring, std::wstring> m_displayNames;
    std::vector<PlayerFlags> m_playerFlags;
    unsigned char m_nextSmallId;
    bool m_host;
    bool m_inSession;
    bool m_localGame;
    bool m_privateGame;
    bool m_matchmakingSession;
    bool m_matchmaking;
    unsigned int m_matchmakingAttributes[2];
    bool m_leavingGame;
    bool m_leavePending;
    GameSessionData m_sessionData;
    void (*m_playerChangedCallbacks[4])(void*, INetworkPlayer*, bool);
    void* m_playerChangedCallbackData[4];
    void (*m_sessionsUpdatedCallback)(void*);
    void* m_sessionsUpdatedCallbackData;
    bool m_connectPending;
    bool m_connectShowProgress;
    int (*m_connectCallback)(void*, bool, int);
    void* m_connectCallbackData;
    std::vector<SessionQuery> m_sessionQueries;
    std::vector<KnownSession> m_knownSessions;
    unsigned int m_nextKnownSessionId;

    lcenet::TcpSocket m_listenSocket;
    C4JThreadImpl* m_acceptThread;
    bool m_acceptStop;
    C4JCriticalSection m_acceptLock;
    std::vector<AcceptedSocket> m_acceptedSockets;
    bool m_listening;
    unsigned short m_listenPort;
    std::wstring m_motd;
    std::vector<unsigned char> m_serverIcon;
    bool m_serverIconFixed;
    unsigned int m_nextSystemId;
    uint32_t m_sessionNonce;
    std::vector<RemoteSystem> m_systems;
    std::vector<RetiredLink> m_retiredLinks;

    lcenet::Link* m_hostLink;
    unsigned int m_ownHostSystemId;
    int m_joinMask;

    lcenet::Beacon m_beacon;
    bool m_beaconRunning;
    int64_t m_beaconUpdated;
    lcenet::Scanner m_scanner;
    unsigned int m_scannerRevision;
    unsigned int m_probeRevision;
    unsigned int m_reportedProbeRevision;
};

#endif
