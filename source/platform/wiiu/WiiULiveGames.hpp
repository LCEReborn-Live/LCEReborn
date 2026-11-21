#ifndef PLATFORM_WIIU_LIVE_GAMES_HPP
#define PLATFORM_WIIU_LIVE_GAMES_HPP

#include <string>
#include <vector>

namespace WiiULiveGames
{
struct Listing
{
    unsigned int owner;
    std::string address;
    std::wstring name;
    unsigned int miniGame;
    unsigned int filters[2];
};

void Tick();
const std::vector<Listing>& Directory();
bool CanJoin(unsigned int owner);
bool Join(unsigned int owner);
unsigned int TicketOwner(const std::string& address);
}

#endif
