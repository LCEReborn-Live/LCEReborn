#ifndef NET_NETLINK_HPP
#define NET_NETLINK_HPP

#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <string>
#include <vector>

#include "game/core/CriticalSection.hpp"
#include "net/NetSocket.hpp"
#if LCE_PLATFORM_WIIU
#include <memory>
#endif

class C4JEventImpl;
class C4JThreadImpl;

namespace lcenet
{
const unsigned char kFirstControlChannel = 0xf0;
const unsigned char kFlagAckRequest = 1;
const unsigned char kFlagAck = 2;

class Link;

class LinkHandler
{
public:
    virtual ~LinkHandler() {}
    virtual void OnLinkData(Link& link, unsigned char channel, unsigned char flags, const unsigned char* data,
                            size_t size) = 0;
};

struct Frame
{
    Frame() : channel(0), receivedAt(0) {}

    unsigned char channel;
    std::vector<unsigned char> payload;
    int64_t receivedAt;
};

int64_t NowMs();

class Link : private CancelFlag
{
public:
    Link(unsigned int systemId, LinkHandler* handler);
    ~Link();

    void Connect(const Address& address);
    void Connect(const std::string& address);
    void Adopt(TcpSocket& socket, const Address& peer);

    void Send(unsigned char channel, unsigned char flags, const void* data, size_t size);
    void SendControl(unsigned char channel, const std::vector<unsigned char>& payload);
    bool TakeControl(Frame& frame);
    void CloseAfterFlush();
    void Close();

    bool IsClosed();
    std::string Error();
    unsigned int SystemId() const { return m_systemId; }
    Address Peer();
    int64_t OpenedAt() const { return m_openedAt; }
    int64_t ConnectedAt();
    size_t QueuedFrames();
    size_t QueuedBytes();

private:
    Link(const Link&);
    Link& operator=(const Link&);

    static int ReadEntry(void* link);
    static int WriteEntry(void* link);
    virtual bool Cancelled() const;
    void Start();
    void ReadLoop();
    void WriteLoop();
    bool Stopped();
    bool ReadExact(unsigned char* data, size_t size);
    void Fail(const std::string& error);

    unsigned int m_systemId;
    LinkHandler* m_handler;
    mutable C4JCriticalSection m_lock;
    C4JEventImpl* m_wake;
    C4JThreadImpl* m_reader;
    C4JThreadImpl* m_writer;
    TcpSocket m_socket;
    Address m_peer;
    Address m_target;
    std::string m_targetText;
#if LCE_PLATFORM_WIIU
    struct LiveJoinTicket;
    std::shared_ptr<LiveJoinTicket> m_liveJoinTicket;
#endif
    bool m_connecting;
    bool m_connected;
    bool m_stop;
    bool m_closed;
    bool m_flushThenClose;
    int64_t m_openedAt;
    int64_t m_connectedAt;
    size_t m_queuedBytes;
    std::deque<std::vector<unsigned char> > m_outbox;
    std::deque<Frame> m_inbox;
    std::string m_error;
};
}

#endif
