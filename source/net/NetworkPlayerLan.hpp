#ifndef NET_NETWORKPLAYERLAN_HPP
#define NET_NETWORKPLAYERLAN_HPP

#include <stdint.h>
#include <deque>
#include <string>

#include "game/core/CriticalSection.hpp"
#include "game/network/INetworkPlayer.hpp"
#include "game/world/generation/PlayerUID.hpp"

class CPlatformNetworkManagerLan;
class IQNetPlayer;
class Socket;

class NetworkPlayerLan : public INetworkPlayer
{
public:
    enum EType
    {
        eType_Host,
        eType_Local,
        eType_Remote
    };

    NetworkPlayerLan(CPlatformNetworkManagerLan* manager, EType type, unsigned int systemId, int userIndex,
                    unsigned char smallId, const PlayerUID& uid, const std::wstring& onlineName,
                    const std::wstring& displayName);
    virtual ~NetworkPlayerLan();

    virtual unsigned char GetSmallId();
    virtual void SendData(INetworkPlayer* to, const void* data, int size, bool reliable, bool requiresAck);
    virtual bool IsSameSystem(INetworkPlayer* other);
    virtual int GetOutstandingAckCount();
    virtual int GetSinglePlayerOutstandingAckCount();
    virtual void DecrementAcksOutstanding();
    virtual int GetSendQueueSizeBytes(INetworkPlayer* to, bool reliable);
    virtual int GetSendQueueSizeMessages(INetworkPlayer* to, bool reliable);
    virtual int GetCurrentRtt();
    virtual bool IsHost();
    virtual bool IsGuest();
    virtual bool IsLocal();
    virtual int GetSessionIndex();
    virtual bool IsTalking();
    virtual bool IsMutedByLocalUser(int userIndex);
    virtual bool HasVoice();
    void SetVoiceStatus(bool hasVoice, bool talking);
    virtual bool HasCamera();
    virtual int GetUserIndex();
    virtual void SetSocket(Socket* socket);
    virtual Socket* GetSocket();
    virtual const wchar_t* GetOnlineName();
    virtual std::wstring GetDisplayName();
    virtual PlayerUID GetUID();
    virtual void SetUID(PlayerUID uid);
    virtual void SentChunkPacket();
    virtual int GetTimeSinceLastChunkPacket_ms();
    virtual bool isFake();
    virtual IQNetPlayer* GetQNetPlayer();

    EType GetType() const;
    unsigned int GetSystemId() const;
    void AckRequested();
    void AckReceived();
    int PendingAcks();

private:
    CPlatformNetworkManagerLan* m_manager;
    EType m_type;
    unsigned int m_systemId;
    int m_userIndex;
    unsigned char m_smallId;
    PlayerUID m_uid;
    std::wstring m_onlineName;
    std::wstring m_displayName;
    Socket* m_socket;
    int64_t m_lastChunkPacketTime;
    C4JCriticalSection m_ackLock;
    std::deque<int64_t> m_ackSendTimes;
    int m_rtt;
    bool m_hasVoice;
    bool m_talking;
};

#endif
