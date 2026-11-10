#ifndef NET_NETSOCKET_HPP
#define NET_NETSOCKET_HPP

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace lcenet
{
const unsigned short kDefaultPort = 25570;

struct Address
{
    Address() : ip(0), port(0) {}

    uint32_t ip;
    unsigned short port;

    bool IsLoopback() const { return (ip >> 24) == 127; }
    std::string Text() const;
    bool operator==(const Address& other) const { return ip == other.ip && port == other.port; }
};

class CancelFlag
{
public:
    virtual ~CancelFlag() {}
    virtual bool Cancelled() const = 0;
};

void Startup();
bool EnsureConnected();
bool Resolve(const std::string& text, Address& address, std::string& error);
std::vector<uint32_t> BroadcastAddresses();
bool LocalAddress(uint32_t& ip);

class TcpSocket
{
public:
    TcpSocket();
    ~TcpSocket();

    bool Connect(const Address& address, int timeoutMs, const CancelFlag& cancel, std::string& error);
    bool Listen(unsigned short port, std::string& error, bool loopbackOnly = false);
    bool Accept(int timeoutMs, TcpSocket& accepted, Address& peer);
    bool SendAll(const void* data, size_t size);
    int Receive(void* data, size_t size, int timeoutMs);
    void Shutdown();
    void Close();
    bool IsOpen() const { return m_handle != kInvalid; }
    void Swap(TcpSocket& other);

private:
    TcpSocket(const TcpSocket&);
    TcpSocket& operator=(const TcpSocket&);

    static const intptr_t kInvalid = -1;
    intptr_t m_handle;
};

class UdpSocket
{
public:
    UdpSocket();
    ~UdpSocket();

    bool Open(unsigned short bindPort, std::string& error);
    bool SendTo(const Address& address, const void* data, size_t size);
    int ReceiveFrom(void* data, size_t size, Address& from, int timeoutMs);
    void Close();
    bool IsOpen() const { return m_handle != kInvalid; }

private:
    UdpSocket(const UdpSocket&);
    UdpSocket& operator=(const UdpSocket&);

    static const intptr_t kInvalid = -1;
    intptr_t m_handle;
};
}

#endif
