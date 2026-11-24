#pragma once
#include <string>
class UIScene;
std::wstring RegisterLiveFaceIcon(UIScene& scene, unsigned int id, const std::string& revision);
void PublishLiveGameAvatar(int profile);
