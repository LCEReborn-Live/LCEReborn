#include "platform/wiiu/WiiULiveRelay.hpp"

#include <coreinit/thread.h>
#include <deque>
#include <memory>

#include "game/core/CriticalSection.hpp"
#include "net/NetLink.hpp"
#include "net/NetSocket.hpp"
#include "platform/pc/PcJson.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"
#include "platform/wiiu/WiiURelayTls.hpp"

namespace WiiULiveRelay
{
namespace
{
constexpr int kConnectTimeout = 5000;
constexpr int kRetryDelay = 10000;
constexpr size_t kMaxGuestsWaiting = 8;
constexpr size_t kMaxControlMessage = 4096;

struct Room
{
    std::string id;
    std::string secret;
    std::string address;
    std::string hostname;
    std::string tlsSeed;
    bool tls = false;
};

struct Guest
{
    std::unique_ptr<lcenet::TcpSocket> socket;
    lcenet::Address peer;
};

alignas(32) OSThread worker;
alignas(32) unsigned char stack[128 * 1024];
C4JCriticalSection lock;
bool initialised = false;
bool workerActive = false;
bool cancelled = false;
bool creating = false;
long long nextAttempt = 0;
Room room;
std::string listing;
std::string roomToRemove;
std::deque<Guest> guests;

class StopFlag : public lcenet::CancelFlag
{
public:
    bool Cancelled() const override
    {
        EnterCriticalSection(&lock);
        const bool stop = cancelled;
        LeaveCriticalSection(&lock);
        return stop;
    }
};

const JsonValue& Field(const JsonValue& root, const wchar_t* name)
{
    static const JsonValue empty;
    const auto found = root.members.find(name);
    return found == root.members.end() ? empty : found->second;
}

std::string Hex(const JsonValue& root, const wchar_t* name, size_t size)
{
    const JsonValue& field = Field(root, name);
    if (field.type != JsonValue::String || field.text.size() != size
        || field.text.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
        return std::string();
    return std::string(field.text.begin(), field.text.end());
}

void AcceptGuest(const lcenet::Address& address, const StopFlag& stop, const std::string& join)
{
    EnterCriticalSection(&lock);
    const bool full = guests.size() >= kMaxGuestsWaiting;
    LeaveCriticalSection(&lock);
    if (full)
        return;

    Guest guest;
    guest.socket.reset(new lcenet::TcpSocket);
    guest.peer = address;
    std::string error;
    if (!guest.socket->Connect(address, kConnectTimeout, stop, error))
        return;
    if (room.tls && !guest.socket->EnableRelayTls(room.hostname, lcenet::RelayTlsSeed(room.tlsSeed, join),
                                                  kConnectTimeout, stop))
        return;
    const std::string handshake = "{\"role\":\"data\",\"room\":\"" + room.id + "\",\"secret\":\"" + room.secret
        + "\",\"join\":\"" + join + "\"}\n";
    if (!guest.socket->SendAll(handshake.data(), handshake.size()))
        return;
    EnterCriticalSection(&lock);
    if (!cancelled)
        guests.push_back(std::move(guest));
    LeaveCriticalSection(&lock);
}

int ServeRoom(int, const char**)
{
    lcenet::Address address;
    lcenet::TcpSocket control;
    StopFlag stop;
    std::string error;
    if (stop.Cancelled() || !lcenet::Resolve(room.address, address, error)
        || !control.Connect(address, kConnectTimeout, stop, error))
        return 0;
    if (room.tls && !control.EnableRelayTls(room.hostname, room.tlsSeed, kConnectTimeout, stop))
        return 0;

    const std::string hello = "{\"role\":\"host\",\"room\":\"" + room.id + "\",\"secret\":\"" + room.secret + "\"}\n";
    if (!control.SendAll(hello.data(), hello.size()))
        return 0;

    std::string pending;
    const long long handshakeDeadline = lcenet::NowMs() + kConnectTimeout;
    bool ready = false;
    while (!stop.Cancelled() && (ready || lcenet::NowMs() < handshakeDeadline))
    {
        char bytes[512];
        const int count = control.Receive(bytes, sizeof(bytes), 100);
        if (count < 0)
            break;
        if (count == 0)
            continue;

        pending.append(bytes, count);
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos)
        {
            JsonValue message;
            if (newline > kMaxControlMessage || !PcJsonParse(pending.substr(0, newline), message))
                return 0;
            pending.erase(0, newline + 1);
            const JsonValue& type = Field(message, L"type");
            if (type.text == L"ready")
                ready = true;
            else if (ready && type.text == L"join" && !stop.Cancelled())
            {
                const std::string join = Hex(message, L"id", 24);
                if (!join.empty())
                    AcceptGuest(address, stop, join);
            }
        }
        if (pending.size() > kMaxControlMessage)
            break;
    }
    return 0;
}

void RoomCreated(const std::string& requestedListing, long status, const std::string& response)
{
    creating = false;
    if (status != 201)
        return;

    JsonValue body;
    if (!PcJsonParse(response, body))
        return;
    room.id = Hex(body, L"id", 24);
    if (room.id.empty())
        return;
    room.secret = Hex(body, L"secret", 64);
    const JsonValue& host = Field(body, L"address");
    const JsonValue& port = Field(body, L"control_port");
    const JsonValue& tls = Field(body, L"tls");
    if (listing != requestedListing || room.secret.empty() || host.type != JsonValue::String || host.text.empty()
        || host.text.size() > 253 || host.text.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::wstring::npos
        || port.type != JsonValue::Number || port.text.empty() || port.text.size() > 5
        || port.text.find_first_not_of(L"0123456789") != std::wstring::npos
        || tls.type != JsonValue::Boolean)
        return;

    room.hostname.assign(host.text.begin(), host.text.end());
    room.address = room.hostname + ':' + std::string(port.text.begin(), port.text.end());
    room.tls = tls.text == L"true";
    if (room.tls)
    {
        room.tlsSeed = WiiULiveSignIn::RelaySeed(room.secret);
        if (room.tlsSeed.empty())
            return;
    }
    EnterCriticalSection(&lock);
    cancelled = false;
    LeaveCriticalSection(&lock);
    workerActive = OSCreateThread(&worker, ServeRoom, 0, NULL, stack + sizeof(stack), sizeof(stack), 16, 0);
    if (workerActive)
        OSResumeThread(&worker);
}

std::string ListingPayload(bool privateGame, unsigned int miniGame, unsigned int firstFilter, unsigned int secondFilter)
{
    const std::string& username = WiiULiveSignIn::Username();
    const std::wstring name = std::wstring(username.begin(), username.end()) + L"'s game";
    const bool matchmaking = !privateGame && miniGame >= 1 && miniGame <= 4;
    const char* visibility = privateGame ? "invite_only" : (matchmaking ? "public" : "friends");
    std::string payload = "{\"name\":" + PcJsonQuote(name) + ",\"visibility\":\"" + visibility + "\"";
    if (matchmaking)
    {
        payload += ",\"matchmaking\":{\"enabled\":true,\"mini_game_id\":" + std::to_string(miniGame) + ",\"filters\":[";
        const unsigned int filters[] = {firstFilter, secondFilter};
        bool haveFilter = false;
        for (unsigned int index = 0; index < 2; ++index)
        {
            if (filters[index] < 1 || filters[index] > 32)
                continue;
            if (haveFilter)
                payload += ',';
            payload += "{\"id\":" + std::to_string(index) + ",\"value\":" + std::to_string(filters[index]) + '}';
            haveFilter = true;
        }
        payload += "]}";
    }
    return payload + '}';
}
}

void TickHost(bool hosting, bool privateGame, unsigned int miniGame, unsigned int firstFilter, unsigned int secondFilter)
{
    if (!initialised)
    {
        InitializeCriticalSection(&lock);
        initialised = true;
    }
    const bool signedIn = WiiULiveSignIn::IsSignedIn();
    const std::string desired = hosting && signedIn ? ListingPayload(privateGame, miniGame, firstFilter, secondFilter) : std::string();
    if (listing != desired)
    {
        StopHosting();
        listing = desired;
        nextAttempt = 0;
    }
    if (workerActive)
    {
        if (!OSIsThreadTerminated(&worker))
            return;
        OSJoinThread(&worker, NULL);
        workerActive = false;
    }
    if (!room.id.empty())
    {
        roomToRemove = room.id;
        room = Room();
    }
    if (!signedIn)
        return;
    if (!roomToRemove.empty())
    {
        const std::string path = "/wiiu/relay/rooms/" + roomToRemove + "/remove";
        if (!WiiULiveSignIn::RequestApi(path.c_str(), "{}", [](long, const std::string&) {}))
            return;
        roomToRemove.clear();
    }
    const long long now = lcenet::NowMs();
    if (listing.empty() || creating || now < nextAttempt)
        return;
    const std::string requestedListing = listing;
    creating = WiiULiveSignIn::RequestApi("/relay/rooms", requestedListing.c_str(),
        [requestedListing](long status, const std::string& response) { RoomCreated(requestedListing, status, response); });
    if (creating)
        nextAttempt = now + kRetryDelay;
}

void StopHosting()
{
    if (!initialised)
        return;
    listing.clear();
    EnterCriticalSection(&lock);
    cancelled = true;
    guests.clear();
    LeaveCriticalSection(&lock);
}

bool TakeGuest(lcenet::TcpSocket& socket, lcenet::Address& peer)
{
    if (!initialised)
        return false;
    EnterCriticalSection(&lock);
    const bool available = !guests.empty();
    if (available)
    {
        socket.Swap(*guests.front().socket);
        peer = guests.front().peer;
        guests.pop_front();
    }
    LeaveCriticalSection(&lock);
    return available;
}
}
