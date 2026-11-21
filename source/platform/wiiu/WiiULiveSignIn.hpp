#ifndef PLATFORM_WIIU_LIVESIGNIN_HPP
#define PLATFORM_WIIU_LIVESIGNIN_HPP

#include <functional>
#include <string>

namespace WiiULiveSignIn
{
enum class Status { Idle, Starting, Waiting, SignedIn, Denied, Expired, Failed };

void Init();
void Start();
void Cancel();
Status Update();
void TickPresence(bool playing);
const std::string& UserCode();
const std::string& Username();
std::wstring ErrorDetail();
bool IsSignedIn();
std::string PlayerUuid();
// The callback runs on the game thread. A null body selects GET.
bool RequestApi(const char* path, const char* body,
                const std::function<void(long, const std::string&)>& complete);
}

#endif
