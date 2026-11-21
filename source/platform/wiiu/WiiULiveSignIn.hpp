#ifndef PLATFORM_WIIU_LIVESIGNIN_HPP
#define PLATFORM_WIIU_LIVESIGNIN_HPP

#include <string>

namespace WiiULiveSignIn
{
enum class Status { Idle, Starting, Waiting, SignedIn, Denied, Expired, Failed };

void Start();
void Cancel();
Status Update();
void TickPresence(bool playing);
const std::string& UserCode();
const std::string& Username();
std::wstring ErrorDetail();
}

#endif
