#ifndef PLATFORM_WIIU_RELAY_TLS_HPP
#define PLATFORM_WIIU_RELAY_TLS_HPP

#include <bearssl.h>
#include "game/core/CriticalSection.hpp"
#include "net/NetSocket.hpp"

namespace lcenet
{
std::string RelayTlsSeed(const std::string& secret, const std::string& nonce);

class WiiURelayTls
{
public:
    WiiURelayTls();
    ~WiiURelayTls();
    bool Connect(TcpSocket& socket, const std::string& hostname, const std::string& seed,
                 int timeoutMs, const CancelFlag& cancel);
    bool SendAll(const void* data, size_t size);
    int Receive(void* data, size_t size, int timeoutMs);
    void Shutdown();

private:
    struct Root
    {
        br_x509_decoder_context decoder;
        unsigned char dn[512];
        size_t dnSize;
        bool valid;
    };
    static void AppendName(void* context, const void* data, size_t size);
    bool InitRoot(size_t index, const unsigned char* der, size_t size);
    bool FlushRecords();
    int ReceiveRecords(int timeoutMs);
    int SendPart(const unsigned char* data, size_t size);

    TcpSocket m_socket;
    C4JCriticalSection m_lock;
    br_ssl_client_context m_client;
    br_x509_minimal_context m_validation;
    Root m_roots[2];
    br_x509_trust_anchor m_anchors[2];
    unsigned char m_buffer[BR_SSL_BUFSIZE_BIDI];
};
}

#endif
