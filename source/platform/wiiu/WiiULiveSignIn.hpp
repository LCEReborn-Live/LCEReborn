#ifndef PLATFORM_WIIU_LIVESIGNIN_HPP
#define PLATFORM_WIIU_LIVESIGNIN_HPP

#include <functional>
#include <string>

namespace WiiULiveSignIn
{
enum class Status { Idle, Starting, Waiting, SignedIn, Denied, Expired, Failed, Restoring };

void Init();
#if LCE_PLATFORM_SWITCH
void Shutdown();
std::string AccountName();
#endif
void RestoreSavedSession();
void Start();
void Cancel();
Status Update();
void TickPresence(bool playing, unsigned int miniGame = 0);
const std::string& UserCode();
const std::string& Username();
std::wstring ErrorDetail();
bool IsSignedIn();
std::string PlayerUuid();
// Derive TLS entropy from the private session token and a freshly issued ticket or room secret.
std::string RelaySeed(const std::string& nonce);
// The callback runs on the game thread. A null body selects GET.
bool RequestApi(const char* path, const char* body,
                const std::function<void(long, const std::string&)>& complete);
}

#endif
