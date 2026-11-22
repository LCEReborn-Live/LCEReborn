#include "platform/wiiu/WiiURelayTls.hpp"

#include <algorithm>
#include <cstring>
#include "game/core/System.hpp"
#include "net/NetLink.hpp"

namespace lcenet
{
namespace
{
const unsigned char kIsrgRootX1[] = {
#include "wiiu_live_root_ca.inc"
};
const unsigned char kIsrgRootX2[] = {
#include "wiiu_live_root_x2_ca.inc"
};
}

std::string RelayTlsSeed(const std::string& secret, const std::string& nonce)
{
    const char label[] = "LCEReborn Wii U relay seed v1";
    br_hmac_key_context key;
    br_hmac_context hmac;
    br_hmac_key_init(&key, &br_sha256_vtable, secret.data(), secret.size());
    br_hmac_init(&hmac, &key, 32);
    br_hmac_update(&hmac, label, sizeof(label));
    br_hmac_update(&hmac, nonce.data(), nonce.size());
    unsigned char seed[32];
    br_hmac_out(&hmac, seed);
    return std::string(reinterpret_cast<const char*>(seed), sizeof(seed));
}

WiiURelayTls::WiiURelayTls()
{
    InitializeCriticalSection(&m_lock);
}

WiiURelayTls::~WiiURelayTls()
{
    DeleteCriticalSection(&m_lock);
}

void WiiURelayTls::AppendName(void* context, const void* data, size_t size)
{
    Root& root = *static_cast<Root*>(context);
    if (size > sizeof(root.dn) - root.dnSize)
    {
        root.valid = false;
        return;
    }
    std::memcpy(root.dn + root.dnSize, data, size);
    root.dnSize += size;
}

bool WiiURelayTls::InitRoot(size_t index, const unsigned char* der, size_t size)
{
    Root& root = m_roots[index];
    root.valid = true;
    root.dnSize = 0;
    br_x509_decoder_init(&root.decoder, AppendName, &root);
    br_x509_decoder_push(&root.decoder, der, size);
    const br_x509_pkey* key = br_x509_decoder_get_pkey(&root.decoder);
    if (!root.valid || key == NULL || !br_x509_decoder_isCA(&root.decoder))
        return false;
    m_anchors[index].dn.data = root.dn;
    m_anchors[index].dn.len = root.dnSize;
    m_anchors[index].flags = BR_X509_TA_CA;
    m_anchors[index].pkey = *key;
    return true;
}

bool WiiURelayTls::Connect(TcpSocket& socket, const std::string& hostname, const std::string& seed,
                          int timeoutMs, const CancelFlag& cancel)
{
    if (hostname.empty() || seed.size() != 32
        || !InitRoot(0, kIsrgRootX1, sizeof(kIsrgRootX1)) || !InitRoot(1, kIsrgRootX2, sizeof(kIsrgRootX2)))
        return false;
    br_ssl_client_init_full(&m_client, &m_validation, m_anchors, 2);
    br_ssl_engine_set_versions(&m_client.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_add_flags(&m_client.eng, BR_OPT_NO_RENEGOTIATION);
    // Cafe's real-time counter starts at 2000-01-01. BearSSL counts days from 0000-01-01.
    const uint64_t seconds = System::currentRealTimeMillis() / 1000;
    br_x509_minimal_set_time(&m_validation, static_cast<uint32_t>(seconds / 86400) + 730485,
                            static_cast<uint32_t>(seconds % 86400));
    br_ssl_engine_set_buffer(&m_client.eng, m_buffer, sizeof(m_buffer), 1);
    br_ssl_engine_inject_entropy(&m_client.eng, seed.data(), seed.size());
    if (!br_ssl_client_reset(&m_client, hostname.c_str(), 0))
        return false;
    m_socket.Swap(socket);
    const int64_t deadline = NowMs() + timeoutMs;
    while (!cancel.Cancelled() && NowMs() < deadline)
    {
        if (!FlushRecords())
            return false;
        if (br_ssl_engine_current_state(&m_client.eng) & BR_SSL_SENDAPP)
            return true;
        if (ReceiveRecords(100) < 0)
            return false;
    }
    return false;
}

bool WiiURelayTls::FlushRecords()
{
    while (br_ssl_engine_current_state(&m_client.eng) & BR_SSL_SENDREC)
    {
        size_t size;
        const unsigned char* bytes = br_ssl_engine_sendrec_buf(&m_client.eng, &size);
        if (!m_socket.SendAll(bytes, size))
            return false;
        br_ssl_engine_sendrec_ack(&m_client.eng, size);
    }
    return !(br_ssl_engine_current_state(&m_client.eng) & BR_SSL_CLOSED);
}

int WiiURelayTls::ReceiveRecords(int timeoutMs)
{
    size_t size;
    unsigned char* buffer = br_ssl_engine_recvrec_buf(&m_client.eng, &size);
    if (buffer == NULL)
        return -1;
    const int received = m_socket.Receive(buffer, size, timeoutMs);
    if (received > 0)
        br_ssl_engine_recvrec_ack(&m_client.eng, static_cast<size_t>(received));
    return received;
}

int WiiURelayTls::SendPart(const unsigned char* data, size_t size)
{
    if (!FlushRecords())
        return -1;
    size_t capacity;
    unsigned char* buffer = br_ssl_engine_sendapp_buf(&m_client.eng, &capacity);
    if (buffer == NULL)
        return -1;
    size = std::min(size, capacity);
    std::memcpy(buffer, data, size);
    br_ssl_engine_sendapp_ack(&m_client.eng, size);
    br_ssl_engine_flush(&m_client.eng, 0);
    return FlushRecords() ? static_cast<int>(size) : -1;
}

bool WiiURelayTls::SendAll(const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    while (size)
    {
        EnterCriticalSection(&m_lock);
        const int sent = SendPart(bytes, size);
        LeaveCriticalSection(&m_lock);
        if (sent <= 0)
            return false;
        bytes += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

int WiiURelayTls::Receive(void* data, size_t size, int timeoutMs)
{
    EnterCriticalSection(&m_lock);
    int received = -1;
    if (FlushRecords())
    {
        size_t available;
        const unsigned char* buffer = br_ssl_engine_recvapp_buf(&m_client.eng, &available);
        if (buffer == NULL)
        {
            received = ReceiveRecords(timeoutMs);
            if (received >= 0 && FlushRecords())
            {
                buffer = br_ssl_engine_recvapp_buf(&m_client.eng, &available);
                received = 0;
            }
            else
                received = -1;
        }
        if (buffer != NULL)
        {
            size = std::min(size, available);
            std::memcpy(data, buffer, size);
            br_ssl_engine_recvapp_ack(&m_client.eng, size);
            received = static_cast<int>(size);
        }
    }
    LeaveCriticalSection(&m_lock);
    return received;
}

void WiiURelayTls::Shutdown()
{
    // Interrupt a reader or writer in the underlying socket before joining it.
    m_socket.Shutdown();
}
}
