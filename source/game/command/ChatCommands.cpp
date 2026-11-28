#include "game/command/ChatCommands.hpp"

#include <wctype.h>

#include "game/command/ChatCommand.hpp"
#include "game/command/CommandArguments.hpp"
#include "game/command/CommandSource.hpp"
#include "game/command/chat/ClearChatCommand.hpp"
#include "game/command/chat/DefaultGameModeChatCommand.hpp"
#include "game/command/chat/DifficultyChatCommand.hpp"
#include "game/command/chat/GameRuleChatCommand.hpp"
#include "game/command/chat/EffectChatCommand.hpp"
#include "game/command/chat/EnchantChatCommand.hpp"
#include "game/command/chat/ExecuteChatCommand.hpp"
#include "game/command/chat/ExperienceChatCommand.hpp"
#include "game/command/chat/FillChatCommand.hpp"
#include "game/command/chat/GameModeChatCommand.hpp"
#include "game/command/chat/GiveChatCommand.hpp"
#include "game/command/chat/HelpChatCommand.hpp"
#include "game/command/chat/KickChatCommand.hpp"
#include "game/command/chat/KillChatCommand.hpp"
#include "game/command/chat/ListChatCommand.hpp"
#include "game/command/chat/MeChatCommand.hpp"
#include "game/command/chat/MessageChatCommand.hpp"
#include "game/command/chat/OperatorChatCommand.hpp"
#include "game/command/chat/SayChatCommand.hpp"
#include "game/command/chat/SeedChatCommand.hpp"
#include "game/command/chat/SetBlockChatCommand.hpp"
#include "game/command/chat/SetWorldSpawnChatCommand.hpp"
#include "game/command/chat/SpawnpointChatCommand.hpp"
#include "game/command/chat/SummonChatCommand.hpp"
#include "game/command/chat/TeleportChatCommand.hpp"
#include "game/command/chat/TimeChatCommand.hpp"
#include "game/command/chat/ToggleDownfallChatCommand.hpp"
#include "game/command/chat/WeatherChatCommand.hpp"
#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/network/ClientboundChatPacket.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/ServerPlayer.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/generation/PlayerList.hpp"

namespace
{
struct Registry
{
    Registry() : ready(false), commands() {}

    ~Registry()
    {
        for (size_t index = 0; index < commands.size(); ++index)
            delete commands[index];
    }

    bool ready;
    std::vector<ChatCommand*> commands;
};

std::vector<ChatCommand*>& Storage()
{
    static Registry registry;
    if (!registry.ready)
    {
        registry.ready = true;
        ChatCommand* const defaults[] = {new HelpChatCommand(),
                                         new ExecuteChatCommand(),
                                         new TimeChatCommand(),
                                         new WeatherChatCommand(),
                                         new GameModeChatCommand(),
                                         new TeleportChatCommand(),
                                         new KillChatCommand(),
                                         new GiveChatCommand(),
                                         new ClearChatCommand(),
                                         new EffectChatCommand(),
                                         new EnchantChatCommand(),
                                         new ExperienceChatCommand(),
                                         new SpawnpointChatCommand(),
                                         new DifficultyChatCommand(),
                                         new GameRuleChatCommand(),
                                         new DefaultGameModeChatCommand(),
                                         new SetWorldSpawnChatCommand(),
                                         new ToggleDownfallChatCommand(),
                                         new SeedChatCommand(),
                                         new SummonChatCommand(),
                                         new SetBlockChatCommand(),
                                         new FillChatCommand(),
                                         new SayChatCommand(),
                                         new MeChatCommand(),
                                         new MessageChatCommand(),
                                         new ListChatCommand(),
                                         new KickChatCommand(),
                                         new OperatorChatCommand(true),
                                         new OperatorChatCommand(false)};
        registry.commands.assign(defaults, defaults + sizeof(defaults) / sizeof(defaults[0]));
    }
    return registry.commands;
}

bool Names(const ChatCommand* command, const std::wstring& name)
{
    if (command->getName() == name)
        return true;
    const std::vector<std::wstring>& aliases = command->getAliases();
    for (size_t index = 0; index < aliases.size(); ++index)
    {
        if (aliases[index] == name)
            return true;
    }
    return false;
}

void WriteUsage(DataOutputStream& output, const std::wstring& name, const ChatCommand* command)
{
    output.writeUTF(name);
    output.writeUTF(command->getUsage());
    output.writeUTF(command->getArguments());
}

void (*sConsoleOutput)(const std::wstring& text) = NULL;

void Dispatch(const CommandSource& source, const ChatCommand::Words& words, const wchar_t* prefix)
{
    const std::wstring name = CommandArguments::Lower(words[0]);
    ChatCommand* command = ChatCommands::Find(name);
    if (command == NULL)
    {
        const std::wstring unknown = L"Unknown command " + std::wstring(prefix) + name + L". Type " + prefix
                                     + L"help for the commands.";
        if (source.output)
            ChatCommands::Error(source.output, unknown);
        else
            ChatCommands::Echo(L"§c" + unknown);
        return;
    }
    if (!command->canUse(source.permission))
    {
        if (source.output)
            ChatCommands::Error(source.output, L"You don't have permission to use this command.");
        return;
    }
    if (!command->run(source, words))
    {
        if (source.output)
            ChatCommands::Error(source.output, command->usageError());
        else
            ChatCommands::Echo(L"§c" + command->usageError());
    }
}
}

namespace ChatCommands
{
void Register(ChatCommand* command)
{
    if (command == NULL)
        return;
    std::vector<ChatCommand*>& commands = Storage();
    for (size_t index = 0; index < commands.size(); ++index)
    {
        if (commands[index]->getName() == command->getName())
        {
            delete commands[index];
            commands[index] = command;
            return;
        }
    }
    commands.push_back(command);
}

ChatCommand* Find(const std::wstring& name)
{
    const std::vector<ChatCommand*>& commands = Storage();
    for (size_t index = 0; index < commands.size(); ++index)
    {
        if (Names(commands[index], name))
            return commands[index];
    }
    return NULL;
}

const std::vector<ChatCommand*>& Commands()
{
    return Storage();
}

int PermissionOf(boost::shared_ptr<ServerPlayer> player)
{
    if (!player || !player->mConnection)
        return ChatCommand::ePermission_Everyone;
    return gMinecraftServer->getPlayers()->isOp(player) ? ChatCommand::ePermission_Operator
                                                        : ChatCommand::ePermission_Everyone;
}

arrayWithLength<unsigned char> WriteUsages(int permission)
{
    const std::vector<ChatCommand*>& commands = Storage();
    short count = 0;
    for (size_t index = 0; index < commands.size(); ++index)
    {
        if (commands[index]->canUse(permission))
            count = static_cast<short>(count + 1 + commands[index]->getAliases().size());
    }
    ByteArrayOutputStream bytes;
    DataOutputStream output(&bytes);
    output.writeShort(count);
    for (size_t index = 0; index < commands.size(); ++index)
    {
        const ChatCommand* command = commands[index];
        if (!command->canUse(permission))
            continue;
        WriteUsage(output, command->getName(), command);
        const std::vector<std::wstring>& aliases = command->getAliases();
        for (size_t alias = 0; alias < aliases.size(); ++alias)
            WriteUsage(output, aliases[alias], command);
    }
    return bytes.toByteArray();
}

std::vector<CommandUsage> ReadUsages(arrayWithLength<unsigned char> data)
{
    std::vector<CommandUsage> usages;
    ByteArrayInputStream bytes(data);
    DataInputStream input(&bytes);
    const int count = input.readShort();
    for (int index = 0; index < count; ++index)
    {
        CommandUsage usage;
        usage.name = input.readUTF();
        usage.usage = input.readUTF();
        usage.arguments = input.readUTF();
        if (!usage.name.empty())
            usages.push_back(usage);
    }
    bytes.reset();
    input.close();
    return usages;
}

std::wstring CleanMessage(const std::wstring& message)
{
    std::wstring clean;
    for (size_t index = 0; index < message.size() && clean.size() < kMaxMessageLength; ++index)
    {
        const wchar_t character = message[index];
        if (character < 0x20 || (character >= 0x7f && character < 0xa0) || character == 0xa7)
            continue;
        if (character == L' ' && (clean.empty() || clean[clean.size() - 1] == L' '))
            continue;
        clean.push_back(character);
    }
    while (!clean.empty() && clean[clean.size() - 1] == L' ')
        clean.erase(clean.size() - 1);
    return clean;
}

std::wstring FormatMessage(const std::wstring& message)
{
    std::wstring formatted;
    bool visible = false;
    for (size_t index = 0; index < message.size(); ++index)
    {
        const wchar_t code = index + 1 < message.size() ? static_cast<wchar_t>(towlower(message[index + 1])) : 0;
        if (message[index] == L'&'
            && ((code >= L'0' && code <= L'9') || (code >= L'a' && code <= L'f') || (code >= L'k' && code <= L'o')
                || code == L'r'))
        {
            formatted.push_back(static_cast<wchar_t>(0xA7));
            formatted.push_back(code);
            ++index;
            continue;
        }
        if (message[index] != L' ')
            visible = true;
        formatted.push_back(message[index]);
    }
    return visible ? formatted : std::wstring();
}

void Reply(boost::shared_ptr<ServerPlayer> player, const std::wstring& text)
{
    std::wstring strings[2] = {std::wstring(), text};
    player->sendMessage(ClientboundChatPacket::eChatPacketMessage_PlayerChat, NULL, 0, strings, 2);
}

void Error(boost::shared_ptr<ServerPlayer> player, const std::wstring& text)
{
    Reply(player, L"§c" + text);
}

void Run(boost::shared_ptr<ServerPlayer> player, const std::wstring& line)
{
    const ChatCommand::Words words = CommandArguments::Split(line.substr(1));
    if (words.empty())
        return;
    CommandSource source;
    source.player = player;
    source.output = player;
    source.entity = player;
    source.level = player->mLevel;
    source.x = player->mX;
    source.y = player->mY;
    source.z = player->mZ;
    source.yRot = player->mYRot;
    source.xRot = player->mXRot;
    source.console = false;
    source.permission = PermissionOf(player);
    Dispatch(source, words, L"/");
}

void RunConsole(const std::wstring& line)
{
    const ChatCommand::Words words = CommandArguments::Split(line.size() > 0 && line[0] == L'/' ? line.substr(1)
                                                                                                : line);
    if (words.empty())
        return;
    CommandSource source;
    source.level = gMinecraftServer->getLevel(0);
    const BlockPos spawn = source.level->getSharedSpawnPos();
    source.x = spawn.x + 0.5;
    source.y = spawn.y;
    source.z = spawn.z + 0.5;
    source.yRot = 0.0f;
    source.xRot = 0.0f;
    source.console = true;
    source.permission = ChatCommand::ePermission_Console;
    Dispatch(source, words, L"");
}

void SetConsoleOutput(void (*output)(const std::wstring& text))
{
    sConsoleOutput = output;
}

void Echo(const std::wstring& text)
{
    if (sConsoleOutput != NULL)
        sConsoleOutput(text);
}
}
