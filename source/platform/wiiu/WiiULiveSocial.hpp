#ifndef PLATFORM_WIIU_LIVE_SOCIAL_HPP
#define PLATFORM_WIIU_LIVE_SOCIAL_HPP

#include <string>

namespace WiiULiveSocial
{
void Tick();
bool Connected();
const std::string& Snapshot();
bool TakeNotification(std::wstring& title, std::wstring& message);
bool SendAction(const std::string& request, char action, const std::string& target);
const std::string& ActionResult();
void SelectProfile(unsigned int userId);
const std::string& ProfileResult();
}

#endif
