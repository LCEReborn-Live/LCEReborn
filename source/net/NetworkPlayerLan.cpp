#include "net/NetworkPlayerLan.hpp"

#include <climits>

#include "game/core/System.hpp"
#include "net/CPlatformNetworkManagerLan.hpp"

NetworkPlayerLan::NetworkPlayerLan(CPlatformNetworkManagerLan* manager, EType type, unsigned int systemId, int userIndex,
                                 unsigned char smallId, const PlayerUID& uid, const std::wstring& onlineName,
                                 const std::wstring& displayName)
    : m_manager(manager),
      m_type(type),
      m_systemId(systemId),
      m_userIndex(userIndex),
      m_smallId(smallId),
      m_uid(uid),
      m_onlineName(onlineName),
      m_displayName(displayName),
      m_socket(0),
      m_lastChunkPacketTime(0),
      m_rtt(0),
      m_hasVoice(false),
      m_talking(false)
{
    InitializeCriticalSection(&m_ackLock);
}

NetworkPlayerLan::~NetworkPlayerLan()
{
    DeleteCriticalSection(&m_ackLock);
}

unsigned char NetworkPlayerLan::GetSmallId()
{
    return m_smallId;
}

void NetworkPlayerLan::SendData(INetworkPlayer* to, const void* data, int size, bool, bool requiresAck)
{
    NetworkPlayerLan* target = static_cast<NetworkPlayerLan*>(to);
    if (target == 0 || size <= 0)
        return;
    if (!IsSameSystem(target))
    {
        m_manager->RemoteDataSend(this, target, data, (unsigned int)size, requiresAck);
        return;
    }
    if ((m_type == eType_Host && target->m_type == eType_Local) || (m_type == eType_Local && target->m_type == eType_Host))
        m_manager->LocalDataSend(this, target, data, (unsigned int)size);
}

bool NetworkPlayerLan::IsSameSystem(INetworkPlayer* other)
{
    return other != 0 && m_systemId == static_cast<NetworkPlayerLan*>(other)->m_systemId;
}

int NetworkPlayerLan::GetOutstandingAckCount()
{
    return m_manager->GetOutstandingAckCount(this);
}

int NetworkPlayerLan::GetSinglePlayerOutstandingAckCount()
{
    return 0;
}

void NetworkPlayerLan::DecrementAcksOutstanding()
{
}

int NetworkPlayerLan::GetSendQueueSizeBytes(INetworkPlayer* to, bool)
{
    return m_manager->GetSendQueueSize(this, to, true);
}

int NetworkPlayerLan::GetSendQueueSizeMessages(INetworkPlayer* to, bool)
{
    return m_manager->GetSendQueueSize(this, to, false);
}

int NetworkPlayerLan::GetCurrentRtt()
{
    EnterCriticalSection(&m_ackLock);
    int rtt = m_rtt;
    LeaveCriticalSection(&m_ackLock);
    return rtt;
}

bool NetworkPlayerLan::IsHost()
{
    return m_type == eType_Host;
}

bool NetworkPlayerLan::IsGuest()
{
    return (m_uid.flags & PlayerUID::FlagMiiPrincipal) != 0;
}

bool NetworkPlayerLan::IsLocal()
{
    return m_systemId == CPlatformNetworkManagerLan::cLocalSystemId;
}

int NetworkPlayerLan::GetSessionIndex()
{
    return m_manager->GetSessionIndex(this);
}

bool NetworkPlayerLan::IsTalking()
{
    EnterCriticalSection(&m_ackLock);
    const bool talking = m_talking;
    LeaveCriticalSection(&m_ackLock);
    return talking;
}

bool NetworkPlayerLan::IsMutedByLocalUser(int)
{
    return false;
}

bool NetworkPlayerLan::HasVoice()
{
    EnterCriticalSection(&m_ackLock);
    const bool hasVoice = m_hasVoice;
    LeaveCriticalSection(&m_ackLock);
    return hasVoice;
}

void NetworkPlayerLan::SetVoiceStatus(bool hasVoice, bool talking)
{
    EnterCriticalSection(&m_ackLock);
    m_hasVoice = hasVoice;
    m_talking = hasVoice && talking;
    LeaveCriticalSection(&m_ackLock);
}

bool NetworkPlayerLan::HasCamera()
{
    return false;
}

int NetworkPlayerLan::GetUserIndex()
{
    return m_userIndex;
}

void NetworkPlayerLan::SetSocket(Socket* socket)
{
    m_socket = socket;
}

Socket* NetworkPlayerLan::GetSocket()
{
    return m_socket;
}

const wchar_t* NetworkPlayerLan::GetOnlineName()
{
    return m_onlineName.c_str();
}

std::wstring NetworkPlayerLan::GetDisplayName()
{
    if (IsLocal())
        return m_displayName;
    return m_onlineName;
}

PlayerUID NetworkPlayerLan::GetUID()
{
    return m_uid;
}

void NetworkPlayerLan::SetUID(PlayerUID uid)
{
    m_uid = uid;
}

void NetworkPlayerLan::SentChunkPacket()
{
    m_lastChunkPacketTime = System::processTimeInMilliSecs();
}

int NetworkPlayerLan::GetTimeSinceLastChunkPacket_ms()
{
    if (m_lastChunkPacketTime == 0)
        return INT_MAX;
    int64_t now = System::processTimeInMilliSecs();
    return (int)(now - m_lastChunkPacketTime);
}

bool NetworkPlayerLan::isFake()
{
    return false;
}

IQNetPlayer* NetworkPlayerLan::GetQNetPlayer()
{
    return 0;
}

NetworkPlayerLan::EType NetworkPlayerLan::GetType() const
{
    return m_type;
}

unsigned int NetworkPlayerLan::GetSystemId() const
{
    return m_systemId;
}

void NetworkPlayerLan::AckRequested()
{
    EnterCriticalSection(&m_ackLock);
    m_ackSendTimes.push_back(System::processTimeInMilliSecs());
    LeaveCriticalSection(&m_ackLock);
}

void NetworkPlayerLan::AckReceived()
{
    EnterCriticalSection(&m_ackLock);
    if (!m_ackSendTimes.empty())
    {
        int sample = (int)(System::processTimeInMilliSecs() - m_ackSendTimes.front());
        m_ackSendTimes.pop_front();
        m_rtt = m_rtt == 0 ? sample : (m_rtt * 7 + sample) / 8;
    }
    LeaveCriticalSection(&m_ackLock);
}

int NetworkPlayerLan::PendingAcks()
{
    EnterCriticalSection(&m_ackLock);
    int count = (int)m_ackSendTimes.size();
    LeaveCriticalSection(&m_ackLock);
    return count;
}
