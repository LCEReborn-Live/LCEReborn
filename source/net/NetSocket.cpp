#include "net/NetSocket.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/core/CriticalSection.hpp"
#include "platform/Platform.hpp"

#if LCE_PLATFORM_WIIU
#include <nn/ac/ac_cpp.h>

#include "platform/wiiu/C4JThreadImpl.hpp"
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lcenet
{
namespace
{
#if LCE_PLATFORM_WIIU
struct NsnSockaddr
{
    uint16_t family;
    uint16_t port;
    uint32_t ip;
    unsigned char zero[8];
};

struct NsnTimeval
{
    long seconds;
    long microseconds;
};

struct NsnHostent
{
    char* name;
    char** aliases;
    int type;
    int length;
    char** addresses;
};

extern "C" {
int NsnSocket(int domain, int type, int protocol) __asm__("socket");
int NsnConnect(int socket, const NsnSockaddr* address, int length) __asm__("connect");
int NsnBind(int socket, const NsnSockaddr* address, int length) __asm__("bind");
int NsnListen(int socket, int backlog) __asm__("listen");
int NsnAccept(int socket, NsnSockaddr* address, int* length) __asm__("accept");
int NsnSend(int socket, const void* data, int size, int flags) __asm__("send");
int NsnRecv(int socket, void* data, int size, int flags) __asm__("recv");
int NsnSendTo(int socket, const void* data, int size, int flags, const NsnSockaddr* address, int length)
    __asm__("sendto");
int NsnRecvFrom(int socket, void* data, int size, int flags, NsnSockaddr* address, int* length)
    __asm__("recvfrom");
int NsnSetSockOpt(int socket, int level, int name, const void* value, int length) __asm__("setsockopt");
int NsnGetPeerName(int socket, NsnSockaddr* address, int* length) __asm__("getpeername");
int NsnShutdown(int socket, int how) __asm__("shutdown");
int NsnSocketClose(int socket) __asm__("socketclose");
int NsnSelect(int count, uint32_t* read, uint32_t* write, uint32_t* error, NsnTimeval* timeout) __asm__("select");
int NsnLastError() __asm__("socketlasterr");
void NsnLibInit() __asm__("socket_lib_init");
int NsnSetMulticastState(int enabled) __asm__("set_multicast_state");
NsnHostent* NsnGetHostByName(const char* name) __asm__("gethostbyname");
}

typedef int NativeSocket;
typedef NsnSockaddr NativeAddress;
const int kAfInet = 2;
const int kSockStream = 1;
const int kSockDgram = 2;
const int kProtoTcp = 6;
const int kProtoUdp = 17;
const int kSolSocket = -1;
const int kSoReuseAddr = 0x0004;
const int kSoBroadcast = 0x0020;
const int kSoNonBlocking = 0x1014;
const int kSoBlocking = 0x1015;
const int kTcpNoDelay = 0x2004;
const int kShutBoth = 2;
const int kTimedOut = 2;
const int kWouldBlock = 6;
const int kRefused = 7;
const int kAddressInUse = 20;
const int kInProgress = 22;
const int kNetUnreachable = 30;
const int kHostUnreachable = 30;

int LastError()
{
    return NsnLastError();
}

void CloseNative(NativeSocket socket)
{
    NsnSocketClose(socket);
}

void SetBlocking(NativeSocket socket, bool blocking)
{
    int enabled = 1;
    NsnSetSockOpt(socket, kSolSocket, blocking ? kSoBlocking : kSoNonBlocking, &enabled, sizeof(enabled));
}

NativeAddress ToNative(const Address& address)
{
    NativeAddress result;
    memset(&result, 0, sizeof(result));
    result.family = kAfInet;
    result.port = address.port;
    result.ip = address.ip;
    return result;
}

Address FromNative(const NativeAddress& address)
{
    Address result;
    result.ip = address.ip;
    result.port = address.port;
    return result;
}

bool WaitFor(NativeSocket socket, bool write, int timeoutMs)
{
    uint32_t set = 1u << socket;
    uint32_t errors = 1u << socket;
    NsnTimeval timeout;
    timeout.seconds = timeoutMs / 1000;
    timeout.microseconds = (timeoutMs % 1000) * 1000;
    int ready = NsnSelect(socket + 1, write ? 0 : &set, write ? &set : 0, &errors, &timeout);
    return ready > 0;
}

int SendNative(NativeSocket socket, const void* data, int size)
{
    return NsnSend(socket, data, size, 0);
}

int RecvNative(NativeSocket socket, void* data, int size)
{
    return NsnRecv(socket, data, size, 0);
}
#elif defined(_WIN32)
typedef SOCKET NativeSocket;
typedef sockaddr_in NativeAddress;
const int kAfInet = AF_INET;
const int kSockStream = SOCK_STREAM;
const int kSockDgram = SOCK_DGRAM;
const int kProtoTcp = IPPROTO_TCP;
const int kProtoUdp = IPPROTO_UDP;
const int kTimedOut = WSAETIMEDOUT;
const int kWouldBlock = WSAEWOULDBLOCK;
const int kRefused = WSAECONNREFUSED;
const int kAddressInUse = WSAEADDRINUSE;
const int kInProgress = WSAEINPROGRESS;
const int kNetUnreachable = WSAENETUNREACH;
const int kHostUnreachable = WSAEHOSTUNREACH;

int LastError()
{
    return WSAGetLastError();
}

void CloseNative(NativeSocket socket)
{
    closesocket(socket);
}

void SetBlocking(NativeSocket socket, bool blocking)
{
    u_long nonBlocking = blocking ? 0 : 1;
    ioctlsocket(socket, FIONBIO, &nonBlocking);
}
#else
typedef int NativeSocket;
typedef sockaddr_in NativeAddress;
const int kAfInet = AF_INET;
const int kSockStream = SOCK_STREAM;
const int kSockDgram = SOCK_DGRAM;
const int kProtoTcp = IPPROTO_TCP;
const int kProtoUdp = IPPROTO_UDP;
const int kTimedOut = ETIMEDOUT;
const int kWouldBlock = EWOULDBLOCK;
const int kRefused = ECONNREFUSED;
const int kAddressInUse = EADDRINUSE;
const int kInProgress = EINPROGRESS;
const int kNetUnreachable = ENETUNREACH;
const int kHostUnreachable = EHOSTUNREACH;

int LastError()
{
    return errno;
}

void CloseNative(NativeSocket socket)
{
    close(socket);
}

void SetBlocking(NativeSocket socket, bool blocking)
{
    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
}
#endif

#if !LCE_PLATFORM_WIIU
const int kSolSocket = SOL_SOCKET;
const int kSoReuseAddr = SO_REUSEADDR;
const int kSoBroadcast = SO_BROADCAST;
const int kSoError = SO_ERROR;
const int kTcpNoDelay = TCP_NODELAY;
#ifdef _WIN32
const int kShutBoth = SD_BOTH;
#else
const int kShutBoth = SHUT_RDWR;
#endif

NativeAddress ToNative(const Address& address)
{
    NativeAddress result;
    memset(&result, 0, sizeof(result));
    result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(address.ip);
    result.sin_port = htons(address.port);
    return result;
}

Address FromNative(const NativeAddress& address)
{
    Address result;
    result.ip = ntohl(address.sin_addr.s_addr);
    result.port = ntohs(address.sin_port);
    return result;
}

bool WaitFor(NativeSocket socket, bool write, int timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    fd_set errors;
    FD_ZERO(&errors);
    FD_SET(socket, &errors);
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;
    timeout.tv_usec = (timeoutMs % 1000) * 1000;
    int ready = select((int)socket + 1, write ? 0 : &set, write ? &set : 0, &errors, &timeout);
    return ready > 0;
}

int SendNative(NativeSocket socket, const void* data, int size)
{
#ifdef MSG_NOSIGNAL
    return send(socket, static_cast<const char*>(data), size, MSG_NOSIGNAL);
#else
    return send(socket, static_cast<const char*>(data), size, 0);
#endif
}

int RecvNative(NativeSocket socket, void* data, int size)
{
    return recv(socket, static_cast<char*>(data), size, 0);
}
#endif

NativeSocket OpenNative(bool stream)
{
#if LCE_PLATFORM_WIIU
    return NsnSocket(kAfInet, stream ? kSockStream : kSockDgram, stream ? kProtoTcp : kProtoUdp);
#else
    return socket(kAfInet, stream ? kSockStream : kSockDgram, stream ? kProtoTcp : kProtoUdp);
#endif
}

bool IsValid(NativeSocket socket)
{
    return (intptr_t)socket >= 0;
}

void SetOption(NativeSocket socket, int level, int name, int value)
{
#if LCE_PLATFORM_WIIU
    NsnSetSockOpt(socket, level, name, &value, sizeof(value));
#else
    setsockopt(socket, level, name, reinterpret_cast<const char*>(&value), sizeof(value));
#endif
}

int PendingError(NativeSocket socket)
{
    int result = 0;
#if LCE_PLATFORM_WIIU
    NsnSockaddr peer;
    int length = sizeof(peer);
    if (NsnGetPeerName(socket, &peer, &length) != 0)
        result = kRefused;
#else
    socklen_t length = sizeof(result);
    getsockopt(socket, kSolSocket, kSoError, reinterpret_cast<char*>(&result), &length);
#endif
    return result;
}

int ConnectNative(NativeSocket socket, const NativeAddress& address)
{
#if LCE_PLATFORM_WIIU
    return NsnConnect(socket, &address, sizeof(address));
#else
    return connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#endif
}

int BindNative(NativeSocket socket, const NativeAddress& address)
{
#if LCE_PLATFORM_WIIU
    return NsnBind(socket, &address, sizeof(address));
#else
    return bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
#endif
}

int ListenNative(NativeSocket socket)
{
#if LCE_PLATFORM_WIIU
    return NsnListen(socket, 8);
#else
    return listen(socket, 8);
#endif
}

NativeSocket AcceptNative(NativeSocket socket, NativeAddress& address)
{
#if LCE_PLATFORM_WIIU
    int length = sizeof(address);
    return NsnAccept(socket, &address, &length);
#else
    socklen_t length = sizeof(address);
    return accept(socket, reinterpret_cast<sockaddr*>(&address), &length);
#endif
}

int SendToNative(NativeSocket socket, const void* data, int size, const NativeAddress& address)
{
#if LCE_PLATFORM_WIIU
    return NsnSendTo(socket, data, size, 0, &address, sizeof(address));
#else
    return sendto(socket, static_cast<const char*>(data), size, 0, reinterpret_cast<const sockaddr*>(&address),
                  sizeof(address));
#endif
}

int RecvFromNative(NativeSocket socket, void* data, int size, NativeAddress& address)
{
#if LCE_PLATFORM_WIIU
    int length = sizeof(address);
    return NsnRecvFrom(socket, data, size, 0, &address, &length);
#else
    socklen_t length = sizeof(address);
    return recvfrom(socket, static_cast<char*>(data), size, 0, reinterpret_cast<sockaddr*>(&address), &length);
#endif
}

void ShutdownNative(NativeSocket socket)
{
#if LCE_PLATFORM_WIIU
    NsnShutdown(socket, kShutBoth);
#else
    shutdown(socket, kShutBoth);
#endif
}

bool ResolveHost(const std::string& host, uint32_t& ip)
{
#if LCE_PLATFORM_WIIU
    unsigned int parts[4];
    char extra;
    if (sscanf(host.c_str(), "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &extra) == 4
        && parts[0] < 256 && parts[1] < 256 && parts[2] < 256 && parts[3] < 256)
    {
        ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        return true;
    }
    NsnHostent* entry = NsnGetHostByName(host.c_str());
    if (entry == 0 || entry->addresses == 0 || entry->addresses[0] == 0 || entry->length != 4)
        return false;
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(entry->addresses[0]);
    ip = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
    return true;
#else
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = 0;
    if (getaddrinfo(host.c_str(), 0, &hints, &results) != 0 || results == 0)
        return false;
    ip = ntohl(reinterpret_cast<const sockaddr_in*>(results->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(results);
    return true;
#endif
}

std::string ConnectError(int code)
{
    if (code == kRefused)
        return "Nothing is accepting games at that address.";
    if (code == kTimedOut)
        return "The server didn't answer.";
    if (code == kNetUnreachable || code == kHostUnreachable)
        return "That address can't be reached from this network.";
    char text[64];
    snprintf(text, sizeof(text), "Couldn't connect (error %d).", code);
    return text;
}

std::string Trim(const std::string& text)
{
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

C4JCriticalSection gStartupLock;
bool gStarted = false;
bool gConnected = false;
}

std::string Address::Text() const
{
    char text[32];
    snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", (unsigned int)((ip >> 24) & 0xff),
             (unsigned int)((ip >> 16) & 0xff), (unsigned int)((ip >> 8) & 0xff), (unsigned int)(ip & 0xff),
             (unsigned int)port);
    return text;
}

void Startup()
{
    if (gStarted)
        return;
    InitializeCriticalSection(&gStartupLock);
#if LCE_PLATFORM_WIIU
    NsnLibInit();
    NsnSetMulticastState(1);
    nn::ac::ConnectAsync();
    gStarted = true;
#elif defined(_WIN32)
    WSADATA data;
    gStarted = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    gConnected = gStarted;
#else
    gStarted = true;
    gConnected = true;
#endif
}

bool EnsureConnected()
{
    if (!gStarted)
        return false;
    EnterCriticalSection(&gStartupLock);
#if LCE_PLATFORM_WIIU
    if (!gConnected)
    {
        bool connected = false;
        nn::ac::IsApplicationConnected(&connected);
        if (!connected)
            connected = nn::ac::Connect().IsSuccess();
        gConnected = connected;
    }
#endif
    bool connected = gConnected;
    LeaveCriticalSection(&gStartupLock);
    return connected;
}

bool Resolve(const std::string& input, Address& address, std::string& error)
{
    std::string text = Trim(input);
    if (text.empty())
    {
        error = "Enter a server address.";
        return false;
    }
    if (text[0] == '[' || text.find(':') != text.rfind(':'))
    {
        error = "IPv6 addresses aren't supported.";
        return false;
    }

    std::string host = text;
    unsigned long port = kDefaultPort;
    size_t colon = text.find(':');
    if (colon != std::string::npos)
    {
        host = text.substr(0, colon);
        std::string portText = text.substr(colon + 1);
        char* end = 0;
        port = strtoul(portText.c_str(), &end, 10);
        if (portText.empty() || *end != 0 || port == 0 || port > 65535)
        {
            error = "That port isn't valid.";
            return false;
        }
    }
    if (host.empty())
    {
        error = "Enter a server address.";
        return false;
    }
    if (!EnsureConnected())
    {
        error = "This system isn't connected to a network.";
        return false;
    }
    uint32_t ip = 0;
    if (!ResolveHost(host, ip))
    {
        error = "Couldn't find " + host + ".";
        return false;
    }
    address.ip = ip;
    address.port = (unsigned short)port;
    return true;
}

std::vector<uint32_t> BroadcastAddresses()
{
    std::vector<uint32_t> addresses;
    addresses.push_back(0xffffffffu);
#if LCE_PLATFORM_WIIU
    uint32_t ip = 0;
    uint32_t mask = 0;
    if (nn::ac::GetAssignedAddress(&ip).IsSuccess() && nn::ac::GetAssignedSubnet(&mask).IsSuccess() && ip != 0)
        addresses.push_back(ip | ~mask);
#elif defined(_WIN32)
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe != INVALID_SOCKET)
    {
        INTERFACE_INFO interfaces[32];
        DWORD bytes = 0;
        if (WSAIoctl(probe, SIO_GET_INTERFACE_LIST, 0, 0, interfaces, sizeof(interfaces), &bytes, 0, 0) == 0)
        {
            for (DWORD index = 0; index < bytes / sizeof(INTERFACE_INFO); ++index)
            {
                const INTERFACE_INFO& entry = interfaces[index];
                if ((entry.iiFlags & IFF_UP) == 0 || (entry.iiFlags & IFF_BROADCAST) == 0
                    || (entry.iiFlags & IFF_LOOPBACK) != 0)
                    continue;
                uint32_t ip = ntohl(entry.iiAddress.AddressIn.sin_addr.s_addr);
                uint32_t mask = ntohl(entry.iiNetmask.AddressIn.sin_addr.s_addr);
                addresses.push_back(ip | ~mask);
            }
        }
        closesocket(probe);
    }
#else
    ifaddrs* list = 0;
    if (getifaddrs(&list) == 0)
    {
        for (ifaddrs* entry = list; entry != 0; entry = entry->ifa_next)
        {
            if (entry->ifa_addr == 0 || entry->ifa_addr->sa_family != AF_INET || (entry->ifa_flags & IFF_UP) == 0
                || (entry->ifa_flags & IFF_BROADCAST) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0
                || entry->ifa_broadaddr == 0)
                continue;
            addresses.push_back(ntohl(reinterpret_cast<sockaddr_in*>(entry->ifa_broadaddr)->sin_addr.s_addr));
        }
        freeifaddrs(list);
    }
#endif
    std::vector<uint32_t> unique;
    for (size_t index = 0; index < addresses.size(); ++index)
    {
        bool seen = false;
        for (size_t other = 0; other < unique.size(); ++other)
            seen = seen || unique[other] == addresses[index];
        if (!seen)
            unique.push_back(addresses[index]);
    }
    return unique;
}

namespace
{
bool IsPrivateAddress(uint32_t ip)
{
    return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8;
}

#if !LCE_PLATFORM_WIIU
uint32_t RoutedAddress()
{
    uint32_t ip = 0;
#if defined(_WIN32)
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET)
        return 0;
#else
    int probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe < 0)
        return 0;
#endif
    sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    target.sin_addr.s_addr = htonl(0x01010101u);
    if (connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0)
    {
        sockaddr_in local;
        memset(&local, 0, sizeof(local));
        socklen_t length = sizeof(local);
        if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length) == 0)
            ip = ntohl(local.sin_addr.s_addr);
    }
#if defined(_WIN32)
    closesocket(probe);
#else
    close(probe);
#endif
    return ip;
}

uint32_t FirstPrivateInterface()
{
#if defined(_WIN32)
    uint32_t found = 0;
    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET)
        return 0;
    INTERFACE_INFO interfaces[32];
    DWORD bytes = 0;
    if (WSAIoctl(probe, SIO_GET_INTERFACE_LIST, 0, 0, interfaces, sizeof(interfaces), &bytes, 0, 0) == 0)
    {
        for (DWORD index = 0; index < bytes / sizeof(INTERFACE_INFO) && found == 0; ++index)
        {
            const INTERFACE_INFO& entry = interfaces[index];
            uint32_t ip = ntohl(entry.iiAddress.AddressIn.sin_addr.s_addr);
            if ((entry.iiFlags & IFF_UP) != 0 && (entry.iiFlags & IFF_LOOPBACK) == 0 && IsPrivateAddress(ip))
                found = ip;
        }
    }
    closesocket(probe);
    return found;
#else
    uint32_t found = 0;
    ifaddrs* list = 0;
    if (getifaddrs(&list) != 0)
        return 0;
    for (ifaddrs* entry = list; entry != 0 && found == 0; entry = entry->ifa_next)
    {
        if (entry->ifa_addr == 0 || entry->ifa_addr->sa_family != AF_INET || (entry->ifa_flags & IFF_UP) == 0
            || (entry->ifa_flags & IFF_LOOPBACK) != 0)
            continue;
        uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
        if (IsPrivateAddress(ip))
            found = ip;
    }
    freeifaddrs(list);
    return found;
#endif
}
#endif
}

bool LocalAddress(uint32_t& ip)
{
    ip = 0;
#if LCE_PLATFORM_WIIU
    uint32_t assigned = 0;
    if (nn::ac::GetAssignedAddress(&assigned).IsSuccess())
        ip = assigned;
#else
    if (!gStarted)
        return false;
    uint32_t routed = RoutedAddress();
    ip = IsPrivateAddress(routed) ? routed : FirstPrivateInterface();
    if (ip == 0 && routed != 0 && (routed >> 24) != 127)
        ip = routed;
#endif
    return ip != 0;
}

TcpSocket::TcpSocket() : m_handle(kInvalid)
{
}

TcpSocket::~TcpSocket()
{
    Close();
}

void TcpSocket::Swap(TcpSocket& other)
{
    intptr_t handle = m_handle;
    m_handle = other.m_handle;
    other.m_handle = handle;
}

bool TcpSocket::Connect(const Address& address, int timeoutMs, const CancelFlag& cancel, std::string& error)
{
    Close();
    if (!EnsureConnected())
    {
        error = "This system isn't connected to a network.";
        return false;
    }
    NativeSocket socket = OpenNative(true);
    if (!IsValid(socket))
    {
        error = ConnectError(LastError());
        return false;
    }
    SetBlocking(socket, false);
    NativeAddress target = ToNative(address);
    if (ConnectNative(socket, target) != 0)
    {
        int code = LastError();
        if (code != kWouldBlock && code != kInProgress)
        {
            CloseNative(socket);
            error = ConnectError(code);
            return false;
        }
        int waited = 0;
        while (!WaitFor(socket, true, 100))
        {
            waited += 100;
            if (cancel.Cancelled())
            {
                CloseNative(socket);
                error = "Cancelled.";
                return false;
            }
            if (waited >= timeoutMs)
            {
                CloseNative(socket);
                error = ConnectError(kTimedOut);
                return false;
            }
        }
        int result = PendingError(socket);
        if (result != 0)
        {
            CloseNative(socket);
            error = ConnectError(result);
            return false;
        }
    }
    SetBlocking(socket, true);
    SetOption(socket, kProtoTcp, kTcpNoDelay, 1);
    m_handle = (intptr_t)socket;
    return true;
}

bool TcpSocket::Listen(unsigned short port, std::string& error, bool loopbackOnly)
{
    Close();
    if (!EnsureConnected())
    {
        error = "This system isn't connected to a network.";
        return false;
    }
    NativeSocket socket = OpenNative(true);
    if (!IsValid(socket))
    {
        error = "Couldn't open a socket.";
        return false;
    }
#ifdef _WIN32
    SetOption(socket, kSolSocket, SO_EXCLUSIVEADDRUSE, 1);
#else
    SetOption(socket, kSolSocket, kSoReuseAddr, 1);
#endif
    Address any;
    any.port = port;
    if (loopbackOnly) any.ip = 0x7f000001;
    if (BindNative(socket, ToNative(any)) != 0)
    {
        int code = LastError();
        CloseNative(socket);
        char text[96];
        if (code == kAddressInUse)
            snprintf(text, sizeof(text), "Port %u is already in use.", (unsigned int)port);
        else
            snprintf(text, sizeof(text), "Couldn't use port %u (error %d).", (unsigned int)port, code);
        error = text;
        return false;
    }
    if (ListenNative(socket) != 0)
    {
        CloseNative(socket);
        error = "Couldn't listen for connections.";
        return false;
    }
#if LCE_PLATFORM_WIIU
    SetBlocking(socket, false);
#endif
    m_handle = (intptr_t)socket;
    return true;
}

bool TcpSocket::Accept(int timeoutMs, TcpSocket& accepted, Address& peer)
{
#if LCE_PLATFORM_WIIU
    if (!IsOpen())
        return false;
    NativeAddress from;
    memset(&from, 0, sizeof(from));
    NativeSocket socket = AcceptNative((NativeSocket)m_handle, from);
    if (!IsValid(socket))
    {
        ::Sleep(timeoutMs);
        return false;
    }
    SetBlocking(socket, true);
#else
    if (!IsOpen() || !WaitFor((NativeSocket)m_handle, false, timeoutMs))
        return false;
    NativeAddress from;
    memset(&from, 0, sizeof(from));
    NativeSocket socket = AcceptNative((NativeSocket)m_handle, from);
    if (!IsValid(socket))
        return false;
#endif
    SetOption(socket, kProtoTcp, kTcpNoDelay, 1);
    accepted.Close();
    accepted.m_handle = (intptr_t)socket;
    peer = FromNative(from);
    return true;
}

bool TcpSocket::SendAll(const void* data, size_t size)
{
    const char* bytes = static_cast<const char*>(data);
    while (size > 0)
    {
        if (!IsOpen())
            return false;
        int chunk = size > 0x10000 ? 0x10000 : (int)size;
        int sent = SendNative((NativeSocket)m_handle, bytes, chunk);
        if (sent <= 0)
            return false;
        bytes += sent;
        size -= (size_t)sent;
    }
    return true;
}

int TcpSocket::Receive(void* data, size_t size, int timeoutMs)
{
    if (!IsOpen())
        return -1;
    if (!WaitFor((NativeSocket)m_handle, false, timeoutMs))
        return 0;
    int chunk = size > 0x10000 ? 0x10000 : (int)size;
    int received = RecvNative((NativeSocket)m_handle, data, chunk);
    return received > 0 ? received : -1;
}

void TcpSocket::Shutdown()
{
    if (IsOpen())
        ShutdownNative((NativeSocket)m_handle);
}

void TcpSocket::Close()
{
    if (!IsOpen())
        return;
    CloseNative((NativeSocket)m_handle);
    m_handle = kInvalid;
}

UdpSocket::UdpSocket() : m_handle(kInvalid)
{
}

UdpSocket::~UdpSocket()
{
    Close();
}

bool UdpSocket::Open(unsigned short bindPort, std::string& error)
{
    Close();
    if (!EnsureConnected())
    {
        error = "This system isn't connected to a network.";
        return false;
    }
    NativeSocket socket = OpenNative(false);
    if (!IsValid(socket))
    {
        error = "Couldn't open a socket.";
        return false;
    }
    SetOption(socket, kSolSocket, kSoBroadcast, 1);
    if (bindPort != 0)
    {
        SetOption(socket, kSolSocket, kSoReuseAddr, 1);
#if defined(SO_REUSEPORT) && !LCE_PLATFORM_WIIU
        SetOption(socket, kSolSocket, SO_REUSEPORT, 1);
#endif
        Address any;
        any.port = bindPort;
        if (BindNative(socket, ToNative(any)) != 0)
        {
            char text[96];
            snprintf(text, sizeof(text), "Couldn't use port %u (error %d).", (unsigned int)bindPort, LastError());
            error = text;
            CloseNative(socket);
            return false;
        }
    }
    m_handle = (intptr_t)socket;
    return true;
}

bool UdpSocket::SendTo(const Address& address, const void* data, size_t size)
{
    if (!IsOpen())
        return false;
    return SendToNative((NativeSocket)m_handle, data, (int)size, ToNative(address)) == (int)size;
}

int UdpSocket::ReceiveFrom(void* data, size_t size, Address& from, int timeoutMs)
{
    if (!IsOpen())
        return -1;
    if (!WaitFor((NativeSocket)m_handle, false, timeoutMs))
        return 0;
    NativeAddress source;
    memset(&source, 0, sizeof(source));
    int received = RecvFromNative((NativeSocket)m_handle, data, (int)size, source);
    if (received <= 0)
        return received < 0 ? -1 : 0;
    from = FromNative(source);
    return received;
}

void UdpSocket::Close()
{
    if (!IsOpen())
        return;
    CloseNative((NativeSocket)m_handle);
    m_handle = kInvalid;
}
}
