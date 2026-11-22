#ifndef PLATFORM_WIIU_LIVE_RELAY_HPP
#define PLATFORM_WIIU_LIVE_RELAY_HPP

namespace lcenet
{
struct Address;
class TcpSocket;
}

namespace WiiULiveRelay
{
void TickHost(bool hosting, bool privateGame, unsigned int miniGame, unsigned int firstFilter, unsigned int secondFilter);
void StopHosting();
#if LCE_PLATFORM_SWITCH
void Shutdown();
#endif
bool TakeGuest(lcenet::TcpSocket& socket, lcenet::Address& peer);
}

#endif
