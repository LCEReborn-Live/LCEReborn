#pragma once
#include "game/command/ChatCommand.hpp"
class GameRuleChatCommand : public ChatCommand
{
public:
    GameRuleChatCommand();
    bool run(const CommandSource&, const Words&) override;
};
