#include "platform/wiiu/WiiULiveGames.hpp"

#include "game/core/System.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "platform/pc/PcJson.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"

#include <cstdlib>

namespace WiiULiveGames
{
namespace
{
std::vector<Listing> directory;
long long refreshAt = 0;
long long updatedAt = 0;
bool refreshing = false;

const JsonValue& Field(const JsonValue& item, const wchar_t* name)
{
    static const JsonValue empty;
    const auto found = item.members.find(name);
    return found == item.members.end() ? empty : found->second;
}

unsigned int Number(const JsonValue& value)
{
    if (value.type != JsonValue::Number || value.text.empty() || value.text.size() > 10
        || value.text.find_first_not_of(L"0123456789") != std::wstring::npos)
        return 0;
    const std::string ascii(value.text.begin(), value.text.end());
    const unsigned long long value64 = std::strtoull(ascii.c_str(), NULL, 10);
    return value64 <= 0xffffffffULL ? static_cast<unsigned int>(value64) : 0;
}

std::string TcpEndpoint(const JsonValue& endpoint, bool allowTls)
{
    const JsonValue& tls = Field(endpoint, L"tls");
    if (endpoint.type != JsonValue::Object
        || (tls.type != JsonValue::Null && (tls.type != JsonValue::Boolean || (!allowTls && tls.text == L"true"))))
        return std::string();
    const JsonValue& host = Field(endpoint, L"address");
    const unsigned int port = Number(Field(endpoint, L"port"));
    if (port == 0 || port > 65535 || host.type != JsonValue::String || host.text.empty() || host.text.size() > 253
        || host.text.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::wstring::npos)
        return std::string();
    return std::string(host.text.begin(), host.text.end()) + ':' + std::to_string(port);
}

void ReadDirectory(long status, const std::string& body)
{
    refreshing = false;
    const long long now = System::processTimeInMilliSecs();
    refreshAt = now + 6000;
    JsonValue root;
    if (status != 200 || !PcJsonParse(body, root))
        return;
    const JsonValue& listings = Field(root, L"servers");
    if (listings.type != JsonValue::Array)
        return;
    std::vector<Listing> fresh;
    for (const JsonValue& row : listings.items)
    {
        const unsigned int owner = Number(Field(row, L"owner_id"));
        const std::string& username = WiiULiveSignIn::Username();
        if (!owner || Field(row, L"owner").text == std::wstring(username.begin(), username.end()))
            continue;

        Listing listing = {};
        listing.owner = owner;
        const JsonValue& relayEndpoint = Field(row, L"relay");
        const std::string relay = TcpEndpoint(relayEndpoint, true);
        if (!relay.empty())
        {
            const char* prefix = Field(relayEndpoint, L"tls").text == L"true" ? "live-relay-tls:" : "live-relay:";
            listing.address = prefix + std::to_string(owner) + '@' + relay;
        }
        else
        {
            const JsonValue& ticketed = Field(row, L"direct_ticketed");
            listing.address = TcpEndpoint(Field(row, L"direct"), false);
            if (listing.address.empty() || ticketed.type != JsonValue::Number
                || (ticketed.text != L"0" && ticketed.text != L"1"))
                continue;
            if (ticketed.text == L"1")
                listing.address = "live-direct:" + std::to_string(owner) + '@' + listing.address;
        }
        listing.name = Field(row, L"name").text.substr(0, 31);
        const JsonValue& match = Field(row, L"matchmaking");
        listing.miniGame = Number(Field(match, L"mini_game_id"));
        const JsonValue& filters = Field(match, L"filters");
        if (listing.miniGame > 4 || filters.type != JsonValue::Array || filters.items.size() > 2)
            listing.miniGame = 0;
        else
        {
            for (const JsonValue& filter : filters.items)
            {
                const unsigned int id = Number(Field(filter, L"id"));
                if (id < 2)
                    listing.filters[id] = Number(Field(filter, L"value"));
            }
        }
        fresh.push_back(listing);
        if (fresh.size() == 128)
            break;
    }
    directory.swap(fresh);
    updatedAt = now;
}
}

void Tick()
{
    if (!WiiULiveSignIn::IsSignedIn())
    {
        directory.clear();
        refreshAt = 0;
        return;
    }
    const long long now = System::processTimeInMilliSecs();
    if (now - updatedAt > 20000)
        directory.clear();
    if (!refreshing && now >= refreshAt)
        refreshing = WiiULiveSignIn::RequestApi("/servers", NULL, ReadDirectory);
}

const std::vector<Listing>& Directory() { return directory; }

bool CanJoin(unsigned int owner)
{
    for (const Listing& listing : directory)
        if (listing.owner == owner)
            return true;
    return false;
}

bool Join(unsigned int owner)
{
    for (const Listing& listing : directory)
        if (listing.owner == owner)
            return CGameNetworkManager::sInstance.JoinServerAddress(listing.address) == 0;
    return false;
}

unsigned int TicketOwner(const std::string& address)
{
    for (const Listing& listing : directory)
        if (listing.address == address)
            return listing.owner;
    return 0;
}
}
