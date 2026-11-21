#include "platform/wiiu/WiiULiveSocial.hpp"

#include "game/core/System.hpp"
#include "game/ui/UIScene_AchievementsMenu.hpp"
#include "platform/CProfile.hpp"
#include "platform/pc/PcJson.hpp"
#include "platform/wiiu/WiiULiveGames.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"

#include <cstdlib>
#include <deque>
#include <set>
#include <utility>

namespace WiiULiveSocial
{
namespace
{
const char* const paths[] = {"/friends", "/friends/requests", "/friends/requests/sent", "/game-invites"};
const wchar_t* const arrays[] = {L"friends", L"incoming", L"sent", L"invitations"};
const char kinds[] = {'F', 'R', 'S', 'I'};

long long refreshAt = 0;
long long achievementsAt = 0;
long long lastRefresh = 0;
unsigned int group = 0;
bool refreshing = false;
bool haveSnapshot = false;
std::string staged;
std::string snapshot;
std::string actionResult;
unsigned int selectedProfile = 0;
std::string profileResult;
std::set<std::string> stagedEvents;
std::set<std::string> seenEvents;
std::deque<std::pair<std::wstring, std::wstring> > notifications;

const JsonValue& Get(const JsonValue& value, const wchar_t* name)
{
    static const JsonValue empty;
    const auto found = value.members.find(name);
    return found == value.members.end() ? empty : found->second;
}

std::string Ascii(const JsonValue& value)
{
    std::string text;
    for (wchar_t character : value.text)
        text += character >= 32 && character < 127 ? static_cast<char>(character) : ' ';
    return text;
}

bool ReadGroup(const std::string& response)
{
    JsonValue root;
    if (!PcJsonParse(response, root))
        return false;
    const JsonValue& list = Get(root, arrays[group]);
    if (list.type != JsonValue::Array)
        return false;
    const char kind = kinds[group];
    for (size_t index = 0; index < list.items.size() && index < 50; ++index)
    {
        const JsonValue& item = list.items[index];
        const std::string id = Ascii(Get(item, kind == 'I' ? L"owner_id" : L"id"));
        const std::string name = Ascii(Get(item, kind == 'I' ? L"owner" : L"username"));
        if (id.empty() || id.size() > 10 || id.find_first_not_of("0123456789") != std::string::npos
            || name.size() < 3 || name.size() > 20
            || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            continue;
        const std::string presence = Ascii(Get(item, L"presence"));
        const bool online = kind == 'I' || (!presence.empty() && presence != "offline");
        const unsigned long long owner = std::strtoull(id.c_str(), NULL, 10);
        if (owner == 0 || owner > 0xffffffffULL)
            continue;
        const bool joinable = WiiULiveGames::CanJoin(static_cast<unsigned int>(owner));
        staged += kind + ("\t" + id + "\t" + name + (online ? "\t1\t" : "\t0\t"))
            + presence + '\t' + Ascii(Get(item, L"activity"))
            + (joinable ? "\t0\t1\t" : "\t0\t0\t") + Ascii(Get(item, L"name"))
            + '\t' + Ascii(Get(item, L"platform")) + '\n';
        if (kind == 'R' || kind == 'I')
            stagedEvents.insert(kind + (":" + id + ':' + Ascii(Get(item, L"expires_at")) + ':' + name));
    }
    return true;
}

void FinishRefresh()
{
    if (haveSnapshot)
    {
        for (const std::string& event : stagedEvents)
        {
            if (seenEvents.count(event) || notifications.size() >= 8)
                continue;
            const std::string name = event.substr(event.rfind(':') + 1);
            const bool invite = event[0] == 'I';
            notifications.emplace_back(invite ? L"Game invitation" : L"Friend request",
                std::wstring(name.begin(), name.end()) + (invite ? L" invited you to play" : L" wants to add you"));
        }
    }
    seenEvents.swap(stagedEvents);
    snapshot.swap(staged);
    haveSnapshot = true;
    lastRefresh = System::processTimeInMilliSecs();
    refreshAt = lastRefresh + 10000;
}

void SyncAchievements(long status, const std::string& response)
{
    JsonValue root;
    if (status != 200 || !PcJsonParse(response, root) || Get(root, L"catalog").text != L"lce-wiiu-v1")
        return;
    const JsonValue& unlocked = Get(root, L"unlocked");
    if (unlocked.type != JsonValue::Array || unlocked.items.size() > 93 || !gProfile->IsSignedIn(0))
        return;
    std::set<int> cloud;
    for (const JsonValue& entry : unlocked.items)
    {
        const std::string id = Ascii(Get(entry, L"id"));
        if (!id.empty() && id.size() <= 2 && id.find_first_not_of("0123456789") == std::string::npos)
            cloud.insert(std::atoi(id.c_str()));
    }
    std::string upload;
    for (int index = 0; index < UIScene_AchievementsMenu::DISPLAY_COUNT; ++index)
    {
        const int id = UIScene_AchievementsMenu::sDisplayList[index];
        const bool local = gProfile->IsAwardsFlagSet(0, id);
        if (cloud.count(id) && !local)
            gProfile->Award(0, id, false);
        if (!cloud.count(id) && local)
        {
            if (!upload.empty()) upload += ',';
            upload += std::to_string(id);
        }
    }
    if (!upload.empty())
    {
        const std::string body = "{\"catalog\":\"lce-wiiu-v1\",\"unlocked\":[" + upload + "]}";
        WiiULiveSignIn::RequestApi("/wiiu/me/achievements", body.c_str(), [](long, const std::string&) {});
    }
}
}

void Tick()
{
    if (!WiiULiveSignIn::IsSignedIn())
    {
        if (haveSnapshot) snapshot.clear();
        haveSnapshot = false;
        refreshAt = 0;
        achievementsAt = 0;
        group = 0;
        notifications.clear();
        return;
    }
    const long long now = System::processTimeInMilliSecs();
    if (now >= achievementsAt && WiiULiveSignIn::RequestApi("/me/achievements", NULL, SyncAchievements))
        achievementsAt = now + 30000;
    if (refreshing || now < refreshAt)
        return;
    if (group == 0)
    {
        staged.clear();
        stagedEvents.clear();
    }
    refreshing = WiiULiveSignIn::RequestApi(paths[group], NULL, [](long status, const std::string& response) {
        refreshing = false;
        if (status != 200 || !ReadGroup(response))
        {
            group = 0;
            refreshAt = System::processTimeInMilliSecs() + 10000;
            return;
        }
        if (++group == sizeof(kinds))
        {
            group = 0;
            FinishRefresh();
        }
    });
}

bool Connected()
{
    return WiiULiveSignIn::IsSignedIn() && haveSnapshot && System::processTimeInMilliSecs() - lastRefresh < 30000;
}

bool ReadSnapshot(std::string& value)
{
    if (!haveSnapshot)
        return false;
    value = snapshot;
    return true;
}
const std::string& ActionResult() { return actionResult; }
const std::string& ProfileResult() { return profileResult; }

void SelectProfile(unsigned int userId)
{
    selectedProfile = userId;
    profileResult.clear();
    if (!userId)
        return;
    const std::string id = std::to_string(userId);
    const std::string path = "/users/" + id + "/achievements";
    WiiULiveSignIn::RequestApi(path.c_str(), NULL, [userId, id](long status, const std::string& response) {
        if (selectedProfile != userId)
            return;
        JsonValue root;
        if (status != 200 || !PcJsonParse(response, root) || Get(root, L"catalog").text != L"lce-wiiu-v1")
            return;
        const JsonValue& unlocked = Get(root, L"unlocked");
        if (unlocked.type != JsonValue::Array || unlocked.items.size() > 93)
            return;
        profileResult = id + "\tok\t";
        for (const JsonValue& entry : unlocked.items)
        {
            const std::string achievement = Ascii(Get(entry, L"id"));
            if (!achievement.empty() && achievement.size() <= 2 && achievement.find_first_not_of("0123456789") == std::string::npos)
                profileResult += achievement + ',';
        }
    });
}

bool TakeNotification(std::wstring& title, std::wstring& message)
{
    if (notifications.empty())
        return false;
    title.swap(notifications.front().first);
    message.swap(notifications.front().second);
    notifications.pop_front();
    return true;
}

bool SendAction(const std::string& request, char action, const std::string& target)
{
    std::string path;
    std::string body = "{}";
    switch (action)
    {
    case 'N':
    case 'V':
        if (target.size() < 3 || target.size() > 20
            || target.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
            return false;
        path = action == 'N' ? "/friends/requests" : "/servers/mine/invites";
        body = "{\"username\":\"" + target + "\"}";
        break;
    case 'A': path = "/friends/requests/" + target + "/accept"; break;
    case 'D': path = "/wiiu/friends/requests/" + target + "/remove"; break;
    case 'C': path = "/wiiu/friends/requests/sent/" + target + "/remove"; break;
    case 'X': path = "/wiiu/game-invites/" + target + "/remove"; break;
    default: return false;
    }
    return WiiULiveSignIn::RequestApi(path.c_str(), body.c_str(), [request, action](long status, const std::string&) {
        const bool ok = status >= 200 && status < 300;
        actionResult = request + (ok ? (action == 'V' ? "\tok\tInvitation sent" : "\tok\tDone")
                                    : "\terror\tLive request failed (" + std::to_string(status) + ')');
        refreshAt = 0;
    });
}
}
