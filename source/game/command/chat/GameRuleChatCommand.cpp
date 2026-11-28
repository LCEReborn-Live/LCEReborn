#include "game/command/chat/GameRuleChatCommand.hpp"
#include "game/command/CommandArguments.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/world/generation/Raids.hpp"
GameRuleChatCommand::GameRuleChatCommand()
    : ChatCommand(L"gamerule", L"disableRaids [true|false]", L"disableRaids true|false", ePermission_Operator) {}
bool GameRuleChatCommand::run(const CommandSource& source, const Words& words)
{
    if (words.size()<2 || words.size()>3 || CommandArguments::Lower(words[1])!=L"disableraids") return false;
    auto* raids=gMinecraftServer->getLevel(0)->getRaids();
    if (!raids) return false;
    if (words.size()==3)
    {
        const auto value=CommandArguments::Lower(words[2]);
        if (value!=L"true" && value!=L"false") return false;
        raids->setDisabled(value==L"true");
    }
    Tell(source, raids->isDisabled()?L"disableRaids = true":L"disableRaids = false");
    return true;
}
