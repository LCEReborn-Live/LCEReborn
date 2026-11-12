#include "game/core/ChatScreen.hpp"

#include <stdint.h>
#include <string.h>
#include <wctype.h>

#include <algorithm>
#include <vector>

#include <boost/unordered_set.hpp>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/render/Font.hpp"
#include "game/command/ChatCommands.hpp"
#include "game/client/render/Renderer.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/DebugOverlay.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/GameSettings.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/Gui.hpp"
#include "game/core/GuiMessage.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/StringConversion.hpp"
#include "game/core/StringIds.hpp"
#include "game/core/System.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/MobEffect.hpp"
#include "game/item/Enchantment.hpp"
#include "game/item/Item.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientPacketListener.hpp"
#include "game/network/INetworkPlayer.hpp"
#include "game/ui/Tooltips.hpp"
#include "game/util/ResourceLocation.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/MappedRegistry.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "platform/CInput.hpp"

extern ConsoleUIController gConsoleUIController;

namespace
{
const size_t kMaxLength = ChatCommands::kMaxMessageLength;
const size_t kSentHistory = 50;
const size_t kMaxSuggestions = 100;
const size_t kVisibleSuggestions = 10;
const int kHistoryLines = 10;
const int kPageLines = 10;
const int kLineHeight = 9;
const int kFieldHeight = 12;
const int kRowHeight = 10;
const int kButtonHeight = 18;
const int kButtonGap = 3;
const int kPanelWidth = 270;
const int kPanelPadding = 6;
const int kTabHeight = 18;
const int kTabWidth = 100;
const float kPixelsPerUnit = 2.0f;
const int kRepeatDelayMs = 133;
const int kRepeatRateMs = 33;
const int kPendingTicks = 40;
const int kKeyColumns = 10;
const int kKeyRows = 5;

const unsigned int kWhite = 0xFFFFFF;
const unsigned int kGrey = 0xAAAAAA;
const unsigned int kDarkGrey = 0x808080;
const unsigned int kYellow = 0xFFFF55;
const unsigned int kButtonText = 0xE0E0E0;
const unsigned int kFieldFill = 0x80000000;
const unsigned int kSuggestionFill = 0xD0000000;
const unsigned int kPanelFill = 0xC0000000;
const unsigned int kTabFill = 0x80000000;
const unsigned int kKeyFill = 0x14FFFFFF;
const unsigned int kKeyFocusFill = 0x47FFFFFF;

const wchar_t* const kSelectors[] = {L"@p", L"@a", L"@r", L"@s", L"@e"};
const wchar_t* const kSelectorKeys[] = {L"type=", L"name=", L"r=",  L"rm=", L"c=",  L"m=",  L"l=",
                                        L"lm=",   L"x=",    L"y=",  L"z=",  L"dx=", L"dy=", L"dz="};
const wchar_t* const kGameModes[] = {L"survival", L"creative", L"adventure"};

const wchar_t* const kQuickPhrases[] = {L"Hello!",      L"Follow me", L"Come here", L"Wait for me",
                                        L"Help!",       L"Thanks!",   L"Good game"};
const int kQuickPhraseCount = sizeof(kQuickPhrases) / sizeof(kQuickPhrases[0]);

const wchar_t* const kLetterRows[4] = {L"1234567890", L"qwertyuiop", L"asdfghjkl'", L"zxcvbnm,.?"};
const wchar_t* const kSymbolRows[4] = {L"!@#$%^&*()", L"-_=+[]{}<>", L":;\"'/\\|~`^", L",.?!-+*=%&"};

enum ETab
{
    ETab_QuickChat,
    ETab_Keyboard
};

enum EQuickItem
{
    EQuickItem_Phrase,
    EQuickItem_Position,
    EQuickItem_Reply
};

struct Suggestion
{
    std::wstring insert;
    std::wstring usage;
};

struct State
{
    int pad;
    bool open;
    bool controller;
    bool justOpened;
    bool systemKeyboard;
    bool fullHistory;
    int tab;
    std::wstring text;
    size_t caret;
    std::vector<std::wstring> sent;
    int recall;
    std::wstring draft;
    int scroll;
    std::vector<Suggestion> suggestions;
    size_t replaceFrom;
    int selected;
    std::wstring usage;
    int quickIndex;
    int keyRow;
    int keyColumn;
    bool shift;
    bool symbols;
    std::wstring lastSender;
    int ticks;
    int heldAction;
    long long nextRepeatTime;
    long long openedTime;
    int pendingTicks;
    int pendingMode;
    int tooltips[16];
};

State sStates[4];
bool sDrawnOverHud[4];
wchar_t sKeyboardBuffer[kMaxLength + 1];

std::wstring Lower(const std::wstring& text)
{
    std::wstring result(text);
    for (size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<wchar_t>(towlower(result[index]));
    return result;
}

bool StartsWithIgnoringCase(const std::wstring& text, const std::wstring& prefix)
{
    return prefix.size() <= text.size() && Lower(text.substr(0, prefix.size())) == Lower(prefix);
}

std::vector<std::wstring> Words(const std::wstring& text)
{
    std::vector<std::wstring> words;
    size_t index = 0;
    while (index < text.size())
    {
        while (index < text.size() && text[index] == L' ')
            ++index;
        size_t end = index;
        while (end < text.size() && text[end] != L' ')
            ++end;
        if (end > index)
            words.push_back(text.substr(index, end - index));
        index = end;
    }
    return words;
}

std::wstring Trim(const std::wstring& text)
{
    const size_t first = text.find_first_not_of(L' ');
    if (first == std::wstring::npos)
        return std::wstring();
    return text.substr(first, text.find_last_not_of(L' ') - first + 1);
}

std::vector<std::wstring> PlayerNames()
{
    std::vector<std::wstring> names;
    const int count = CGameNetworkManager::sInstance.GetPlayerCount();
    for (int index = 0; index < count; ++index)
    {
        INetworkPlayer* player = CGameNetworkManager::sInstance.GetPlayerByIndex(index);
        if (player != NULL && player->GetOnlineName() != NULL && player->GetOnlineName()[0] != 0)
            names.push_back(player->GetOnlineName());
    }
    return names;
}

const std::vector<ChatCommands::CommandUsage>& Commands(const State& s)
{
    static const std::vector<ChatCommands::CommandUsage> kNone;
    ClientPacketListener* connection = Minecraft::GetInstance()->getConnection(s.pad);
    return connection != NULL ? connection->getCommands() : kNone;
}

const ChatCommands::CommandUsage* FindCommand(const State& s, const std::wstring& name)
{
    const std::vector<ChatCommands::CommandUsage>& usages = Commands(s);
    for (size_t index = 0; index < usages.size(); ++index)
    {
        if (name == usages[index].name)
            return &usages[index];
    }
    return NULL;
}

template <class Map>
std::vector<std::wstring> RegistryNames(const Map& keys)
{
    std::vector<std::wstring> names;
    for (typename Map::const_iterator it = keys.begin(); it != keys.end(); ++it)
    {
        if (it->first.getPath() != L"air")
            names.push_back(it->first.getPath());
    }
    std::sort(names.begin(), names.end());
    return names;
}

const std::vector<std::wstring>& ItemNames()
{
    static std::vector<std::wstring> names;
    if (names.empty())
        names = RegistryNames(Item::itemRegistry.keySet());
    return names;
}

const std::vector<std::wstring>& BlockNames()
{
    static std::vector<std::wstring> names;
    if (names.empty())
        names = RegistryNames(Block::blockRegistry->keySet());
    return names;
}

const std::vector<std::wstring>& EffectNames()
{
    static std::vector<std::wstring> names;
    if (names.empty())
        names = RegistryNames(MobEffect::mobEffectRegistry->keySet());
    return names;
}

const std::vector<std::wstring>& EnchantmentNames()
{
    static std::vector<std::wstring> names;
    if (names.empty())
        names = RegistryNames(Enchantment::enchantmentRegistry->keySet());
    return names;
}

const std::vector<std::wstring>& EntityNames()
{
    static std::vector<std::wstring> names;
    if (names.empty())
    {
        boost::unordered_set<ResourceLocation>* types = EntityIO::getAllEntityTypes();
        for (boost::unordered_set<ResourceLocation>::const_iterator it = types->begin(); it != types->end(); ++it)
        {
            if (it->getPath() != L"player" && it->getPath() != L"LightningBolt")
                names.push_back(it->getPath());
        }
        if (std::find(names.begin(), names.end(), L"lightning_bolt") == names.end())
            names.push_back(L"lightning_bolt");
        std::sort(names.begin(), names.end());
    }
    return names;
}

std::vector<std::wstring> SplitOptions(const std::wstring& kind)
{
    std::vector<std::wstring> options;
    size_t start = 0;
    while (start <= kind.size())
    {
        size_t end = kind.find(L'|', start);
        if (end == std::wstring::npos)
            end = kind.size();
        options.push_back(kind.substr(start, end - start));
        start = end + 1;
    }
    return options;
}

void AddSuggestion(State& s, const std::wstring& insert, const std::wstring& usage)
{
    if (s.suggestions.size() >= kMaxSuggestions)
        return;
    Suggestion suggestion;
    suggestion.insert = insert;
    suggestion.usage = usage;
    s.suggestions.push_back(suggestion);
}

bool SuggestSelector(State& s, const std::wstring& word)
{
    if (word.size() < 3 || word[0] != L'@' || word[2] != L'[' || word.find(L']') != std::wstring::npos)
        return false;
    const size_t pairStart = word.find_last_of(L",[") + 1;
    const std::wstring pair = word.substr(pairStart);
    const size_t equals = pair.find(L'=');
    if (equals == std::wstring::npos)
    {
        for (size_t index = 0; index < sizeof(kSelectorKeys) / sizeof(kSelectorKeys[0]); ++index)
        {
            if (StartsWithIgnoringCase(kSelectorKeys[index], pair))
                AddSuggestion(s, word.substr(0, pairStart) + kSelectorKeys[index], std::wstring());
        }
        return true;
    }

    const std::wstring key = Lower(pair.substr(0, equals));
    size_t valueStart = pairStart + equals + 1;
    if (valueStart < word.size() && word[valueStart] == L'!')
        ++valueStart;
    const std::wstring value = word.substr(valueStart);
    std::vector<std::wstring> options;
    if (key == L"type")
    {
        options = EntityNames();
        options.insert(std::lower_bound(options.begin(), options.end(), std::wstring(L"player")), L"player");
    }
    else if (key == L"m")
        options.assign(kGameModes, kGameModes + 3);
    else if (key == L"name")
        options = PlayerNames();
    for (size_t index = 0; index < options.size(); ++index)
    {
        if (StartsWithIgnoringCase(options[index], value) && Lower(options[index]) != Lower(value))
            AddSuggestion(s, word.substr(0, valueStart) + options[index], std::wstring());
    }
    return true;
}

std::vector<std::wstring> ArgumentOptions(const State& s, const std::wstring& kind,
                                          const std::vector<std::wstring>& before)
{
    std::vector<std::wstring> options;
    if (kind == L"targets")
    {
        options = PlayerNames();
        options.insert(options.end(), kSelectors, kSelectors + sizeof(kSelectors) / sizeof(kSelectors[0]));
    }
    else if (kind == L"player")
        options = PlayerNames();
    else if (kind == L"item")
        options = ItemNames();
    else if (kind == L"block")
        options = BlockNames();
    else if (kind == L"effect")
    {
        options.push_back(L"clear");
        const std::vector<std::wstring>& effects = EffectNames();
        options.insert(options.end(), effects.begin(), effects.end());
    }
    else if (kind == L"enchantment")
        options = EnchantmentNames();
    else if (kind == L"entity")
        options = EntityNames();
    else if (kind == L"mode")
        options.assign(kGameModes, kGameModes + 3);
    else if (kind == L"coord")
    {
        options.push_back(L"~");
        options.push_back(L"^");
    }
    else if (kind == L"command")
    {
        const std::vector<ChatCommands::CommandUsage>& usages = Commands(s);
        for (size_t index = 0; index < usages.size(); ++index)
        {
            if (iswalpha(usages[index].name[0]))
                options.push_back(usages[index].name);
        }
    }
    else if (kind == L"xpvalue")
    {
        if (before.size() > 1 && Lower(before[1]) == L"query")
        {
            options.push_back(L"points");
            options.push_back(L"levels");
        }
    }
    else if (kind == L"execute")
    {
        const wchar_t* const parts[] = {L"as", L"at", L"positioned", L"if", L"unless", L"run"};
        options.assign(parts, parts + 6);
    }
    else if (kind == L"execute-positioned")
    {
        options.push_back(L"as");
        options.push_back(L"~");
        options.push_back(L"^");
    }
    else if (kind == L"execute-entity")
        options.push_back(L"entity");
    else if (kind == L"time")
    {
        const std::wstring operation = before.size() > 1 ? Lower(before[1]) : std::wstring();
        if (operation == L"set")
        {
            const wchar_t* const times[] = {L"day", L"noon", L"night", L"midnight"};
            options.assign(times, times + 4);
        }
        else if (operation == L"query")
        {
            const wchar_t* const queries[] = {L"daytime", L"gametime", L"day"};
            options.assign(queries, queries + 3);
        }
    }
    else if (kind.find(L'|') != std::wstring::npos)
        options = SplitOptions(kind);
    return options;
}

std::wstring ExecuteKind(const std::vector<std::wstring>& args, size_t* runAt)
{
    size_t index = 0;
    while (index < args.size())
    {
        const std::wstring part = Lower(args[index]);
        const size_t left = args.size() - index - 1;
        if (part == L"run")
        {
            *runAt = index + 1;
            return std::wstring();
        }
        if (part == L"as" || part == L"at")
        {
            if (left == 0)
                return L"targets";
            index += 2;
        }
        else if (part == L"positioned")
        {
            if (left == 0)
                return L"execute-positioned";
            if (Lower(args[index + 1]) == L"as")
            {
                if (left == 1)
                    return L"targets";
                index += 3;
            }
            else
            {
                if (left < 3)
                    return L"coord";
                index += 4;
            }
        }
        else if (part == L"if" || part == L"unless")
        {
            if (left == 0)
                return L"execute-entity";
            if (left == 1)
                return Lower(args[index + 1]) == L"entity" ? L"targets" : std::wstring();
            index += 3;
        }
        else
            return std::wstring();
    }
    return L"execute";
}

void UpdateSuggestions(State& s)
{
    s.suggestions.clear();
    s.selected = 0;
    s.usage.clear();
    s.replaceFrom = s.text.size();
    if (s.caret != s.text.size())
        return;

    const size_t space = s.text.find_last_of(L' ');
    const size_t wordStart = space == std::wstring::npos ? 0 : space + 1;
    const std::wstring word = s.text.substr(wordStart);
    s.replaceFrom = wordStart;

    std::vector<std::wstring> options;
    const bool command = !s.text.empty() && s.text[0] == L'/';
    if (command && wordStart == 0)
    {
        const std::vector<ChatCommands::CommandUsage>& usages = Commands(s);
        for (size_t index = 0; index < usages.size(); ++index)
        {
            const std::wstring name = L"/" + usages[index].name;
            if (!StartsWithIgnoringCase(name, word) || Lower(name) == Lower(word))
                continue;
            AddSuggestion(s, name + (usages[index].usage.empty() ? L"" : L" "), usages[index].usage);
        }
        return;
    }

    if (command)
    {
        std::vector<std::wstring> line = Words(s.text.substr(1, wordStart - 1));
        if (line.empty())
            return;
        std::wstring kind;
        bool resolved = false;
        while (!resolved && Lower(line[0]) == L"execute")
        {
            const std::vector<std::wstring> args(line.begin() + 1, line.end());
            size_t runAt = std::wstring::npos;
            kind = ExecuteKind(args, &runAt);
            if (runAt == std::wstring::npos || runAt == args.size())
            {
                if (runAt == args.size())
                    kind = L"command";
                resolved = true;
                break;
            }
            line.assign(args.begin() + runAt, args.end());
            if (line.empty())
                return;
        }
        if (!resolved)
        {
            const ChatCommands::CommandUsage* info = FindCommand(s, Lower(line[0]));
            if (info == NULL)
                return;
            const size_t argument = line.size() - 1;
            const std::vector<std::wstring> kinds = Words(info->arguments);
            if (argument < kinds.size())
                kind = kinds[argument];
            else if (!kinds.empty() && kinds.back() == L"text")
                kind = L"text";
            if (word.empty() && !info->usage.empty() && !kind.empty())
                s.usage = L"/" + info->name + L" " + info->usage;
        }
        if (kind == L"targets" && SuggestSelector(s, word))
            return;
        options = ArgumentOptions(s, kind, line);
    }
    else if (!word.empty())
    {
        options = PlayerNames();
    }

    for (size_t index = 0; index < options.size() && s.suggestions.size() < kMaxSuggestions; ++index)
    {
        if (!StartsWithIgnoringCase(options[index], word) || Lower(options[index]) == Lower(word))
            continue;
        Suggestion suggestion;
        suggestion.insert = options[index] + L" ";
        s.suggestions.push_back(suggestion);
    }
}

void SetText(State& s, const std::wstring& text)
{
    s.text = text.size() > kMaxLength ? text.substr(0, kMaxLength) : text;
    s.caret = s.text.size();
    UpdateSuggestions(s);
}

void Insert(State& s, const std::wstring& typed)
{
    for (size_t index = 0; index < typed.size() && s.text.size() < kMaxLength; ++index)
    {
        if (typed[index] < 0x20 || typed[index] == 0xA7)
            continue;
        s.text.insert(s.caret, 1, typed[index]);
        ++s.caret;
    }
    s.recall = -1;
    UpdateSuggestions(s);
}

void Erase(State& s, size_t from, size_t to)
{
    if (to <= from || to > s.text.size())
        return;
    s.text.erase(from, to - from);
    s.caret = from;
    s.recall = -1;
    UpdateSuggestions(s);
}

size_t WordLeft(const State& s)
{
    size_t index = s.caret;
    while (index > 0 && s.text[index - 1] == L' ')
        --index;
    while (index > 0 && s.text[index - 1] != L' ')
        --index;
    return index;
}

size_t WordRight(const State& s)
{
    size_t index = s.caret;
    while (index < s.text.size() && s.text[index] != L' ')
        ++index;
    while (index < s.text.size() && s.text[index] == L' ')
        ++index;
    return index;
}

void MoveCaret(State& s, size_t caret)
{
    s.caret = caret > s.text.size() ? s.text.size() : caret;
    UpdateSuggestions(s);
}

bool Accept(State& s)
{
    if (s.suggestions.empty())
        return false;
    SetText(s, s.text.substr(0, s.replaceFrom) + s.suggestions[s.selected].insert);
    return true;
}

void Recall(State& s, bool older)
{
    if (s.sent.empty())
        return;
    if (older)
    {
        if (s.recall == -1)
        {
            s.draft = s.text;
            s.recall = static_cast<int>(s.sent.size()) - 1;
        }
        else if (s.recall > 0)
        {
            --s.recall;
        }
        SetText(s, s.sent[s.recall]);
        return;
    }
    if (s.recall == -1)
        return;
    ++s.recall;
    if (s.recall >= static_cast<int>(s.sent.size()))
    {
        s.recall = -1;
        SetText(s, s.draft);
        return;
    }
    SetText(s, s.sent[s.recall]);
}

int QuickItemCount(const State& s)
{
    return kQuickPhraseCount + 1 + (s.lastSender.empty() ? 0 : 1);
}

EQuickItem QuickItemKind(int index)
{
    if (index < kQuickPhraseCount)
        return EQuickItem_Phrase;
    return index == kQuickPhraseCount ? EQuickItem_Position : EQuickItem_Reply;
}

std::wstring PositionText(int pad)
{
    boost::shared_ptr<MultiplayerLocalPlayer> player = Minecraft::GetInstance()->mLocalPlayers[pad];
    if (!player)
        return std::wstring();
    return L"My position: " + _toString<int>(Mth::floor(player->mX)) + L", " + _toString<int>(Mth::floor(player->mY))
           + L", " + _toString<int>(Mth::floor(player->mZ));
}

std::wstring QuickItemLabel(int pad, const State& s, int index)
{
    switch (QuickItemKind(index))
    {
    case EQuickItem_Phrase:
        return kQuickPhrases[index];
    case EQuickItem_Position:
        return PositionText(pad);
    default:
        return L"Reply to " + s.lastSender;
    }
}

void UpdateTooltips(int pad)
{
    State& s = sStates[pad];
    int tooltips[16];
    for (int index = 0; index < 16; ++index)
        tooltips[index] = -1;
    if (!s.controller)
    {
        tooltips[0] = IDS_LCE_CHAT_SEND;
        tooltips[1] = IDS_LCE_CHAT_CLOSE;
        if (!s.suggestions.empty())
        {
            tooltips[10] = IDS_LCE_CHAT_COMPLETE;
            if (s.suggestions.size() > 1)
                tooltips[13] = IDS_LCE_CHAT_CHOOSE;
        }
    }
    else if (s.fullHistory)
    {
        tooltips[1] = IDS_TOOLTIPS_BACK;
        tooltips[13] = IDS_LCE_CHAT_SCROLL;
    }
    else if (s.tab == ETab_QuickChat)
    {
        tooltips[0] = IDS_LCE_CHAT_SEND;
        tooltips[1] = IDS_LCE_CHAT_CLOSE;
        tooltips[9] = IDS_LCE_CHAT_HISTORY;
        tooltips[10] = IDS_LCE_CHAT_KEYBOARD;
    }
    else
    {
        tooltips[0] = IDS_LCE_CHAT_TYPE;
        tooltips[1] = IDS_LCE_CHAT_CLOSE;
        tooltips[2] = IDS_LCE_CHAT_DELETE;
        tooltips[3] = IDS_LCE_CHAT_SPACE;
        tooltips[4] = IDS_LCE_CHAT_SHIFT;
        if (!s.suggestions.empty())
            tooltips[5] = IDS_LCE_CHAT_COMPLETE;
        if (s.suggestions.size() > 1)
            tooltips[7] = IDS_LCE_CHAT_CHOOSE;
        tooltips[9] = IDS_LCE_CHAT_HISTORY;
        tooltips[10] = IDS_LCE_CHAT_QUICK;
    }
    if (memcmp(tooltips, s.tooltips, sizeof(tooltips)) == 0)
        return;
    memcpy(s.tooltips, tooltips, sizeof(tooltips));
    gConsoleUIController.SetTooltips(pad, tooltips[0], tooltips[1], tooltips[2], tooltips[3], tooltips[4], tooltips[5],
                                     tooltips[6], tooltips[7], tooltips[8], tooltips[9], tooltips[10], tooltips[11],
                                     tooltips[12], tooltips[13], tooltips[14], tooltips[15], false, false);
}

void Send(int pad)
{
    State& s = sStates[pad];
    const std::wstring message = Trim(s.text);
    boost::shared_ptr<MultiplayerLocalPlayer> player = Minecraft::GetInstance()->mLocalPlayers[pad];
    if (!message.empty() && player)
    {
        if (s.sent.empty() || s.sent.back() != message)
            s.sent.push_back(message);
        if (s.sent.size() > kSentHistory)
            s.sent.erase(s.sent.begin());
        player->chat(message);
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
    }
    ChatScreen::Close(pad);
}

int SystemKeyboardDone(void* context, bool accepted)
{
    const int pad = static_cast<int>(reinterpret_cast<intptr_t>(context));
    State& s = sStates[pad];
    if (!s.systemKeyboard)
        return 0;
    s.systemKeyboard = false;
    if (!s.open)
        return 0;
    if (!accepted)
    {
        s.tab = ETab_QuickChat;
        return 0;
    }
    wchar_t text[kMaxLength + 1];
    memset(text, 0, sizeof(text));
    g_pInput->GetText(text, static_cast<int>(kMaxLength));
    text[kMaxLength] = 0;
    SetText(s, text);
    Send(pad);
    return 0;
}

void ShowKeyboard(int pad, State& s)
{
    s.tab = ETab_Keyboard;
    if (!g_pInput->IsPadDRCController(pad))
        return;
    memset(sKeyboardBuffer, 0, sizeof(sKeyboardBuffer));
    for (size_t index = 0; index < s.text.size() && index < kMaxLength; ++index)
        sKeyboardBuffer[index] = s.text[index];
    if (g_pInput->RequestKeyboard(L"Chat", sKeyboardBuffer, static_cast<unsigned int>(pad),
                                  static_cast<unsigned int>(kMaxLength), SystemKeyboardDone,
                                  reinterpret_cast<void*>(static_cast<intptr_t>(pad)), C_4JInput::EKeyboardMode_Chat)
        == 0)
        s.systemKeyboard = true;
    else
        s.tab = ETab_QuickChat;
}

bool Pressed(int pad, int action)
{
    return g_pInput->ButtonPressed(pad, static_cast<unsigned char>(action), 0);
}

int DirectionPressed(int pad, State& s)
{
    const int directions[4] = {EControllerActions_MenuUp, EControllerActions_MenuDown, EControllerActions_MenuLeft,
                               EControllerActions_MenuRight};
    for (int index = 0; index < 4; ++index)
    {
        if (Pressed(pad, directions[index]))
        {
            s.heldAction = directions[index];
            s.nextRepeatTime = System::processTimeInMilliSecs() + kRepeatDelayMs;
            return directions[index];
        }
    }
    if (s.heldAction >= 0 && g_pInput->ButtonDown(pad, static_cast<unsigned char>(s.heldAction)))
    {
        const long long now = System::processTimeInMilliSecs();
        if (now >= s.nextRepeatTime)
        {
            s.nextRepeatTime = now + kRepeatRateMs;
            return s.heldAction;
        }
        return -1;
    }
    s.heldAction = -1;
    return -1;
}

int KeySpanStart(int column)
{
    if (column < 2)
        return 0;
    if (column < 4)
        return 2;
    if (column < 8)
        return 4;
    return 8;
}

void PressKey(int pad, State& s)
{
    if (s.keyRow < 4)
    {
        wchar_t character = (s.symbols ? kSymbolRows : kLetterRows)[s.keyRow][s.keyColumn];
        if (s.shift && !s.symbols)
        {
            character = static_cast<wchar_t>(towupper(character));
            s.shift = false;
        }
        Insert(s, std::wstring(1, character));
        return;
    }
    switch (KeySpanStart(s.keyColumn))
    {
    case 0:
        s.shift = !s.shift;
        break;
    case 2:
        s.symbols = !s.symbols;
        break;
    case 4:
        Insert(s, L" ");
        break;
    default:
        Send(pad);
        break;
    }
}

void TickController(int pad, State& s)
{
    if (s.fullHistory)
    {
        if (Pressed(pad, EControllerActions_MenuCancel) || Pressed(pad, EControllerActions_MenuOtherStickPress))
        {
            gConsoleUIController.PlayUISFX(SoundEvents::UI_BACK);
            s.fullHistory = false;
            s.scroll = 0;
            return;
        }
        const int direction = DirectionPressed(pad, s);
        if (direction == EControllerActions_MenuUp || Pressed(pad, EControllerActions_MenuOtherStickUp))
            ++s.scroll;
        else if ((direction == EControllerActions_MenuDown || Pressed(pad, EControllerActions_MenuOtherStickDown))
                 && s.scroll > 0)
            --s.scroll;
        if (Pressed(pad, EControllerActions_MenuPageUp))
            s.scroll += kPageLines;
        if (Pressed(pad, EControllerActions_MenuPageDown))
            s.scroll = s.scroll > kPageLines ? s.scroll - kPageLines : 0;
        return;
    }
    if (Pressed(pad, EControllerActions_MenuCancel))
    {
        gConsoleUIController.PlayUISFX(SoundEvents::UI_BACK);
        ChatScreen::Close(pad);
        return;
    }
    if (Pressed(pad, EControllerActions_MenuOtherStickPress))
    {
        gConsoleUIController.PlayUISFX(SoundEvents::UI_PRESS);
        s.fullHistory = true;
        s.scroll = 0;
        return;
    }
    const int direction = DirectionPressed(pad, s);
    if (s.tab == ETab_QuickChat)
    {
        const int count = QuickItemCount(s);
        if (direction == EControllerActions_MenuUp)
            s.quickIndex = (s.quickIndex + count - 1) % count;
        else if (direction == EControllerActions_MenuDown)
            s.quickIndex = (s.quickIndex + 1) % count;
        if (s.quickIndex >= count)
            s.quickIndex = count - 1;
        if ((Pressed(pad, EControllerActions_MenuOk) || Pressed(pad, EControllerActions_MenuA)))
        {
            switch (QuickItemKind(s.quickIndex))
            {
            case EQuickItem_Phrase:
                SetText(s, kQuickPhrases[s.quickIndex]);
                Send(pad);
                return;
            case EQuickItem_Position:
                SetText(s, PositionText(pad));
                Send(pad);
                return;
            default:
                SetText(s, s.lastSender + L" ");
                ShowKeyboard(pad, s);
                return;
            }
        }
        if (Pressed(pad, EControllerActions_MenuBack))
            ShowKeyboard(pad, s);
        return;
    }

    if (Pressed(pad, EControllerActions_MenuBack))
    {
        s.tab = ETab_QuickChat;
        return;
    }
    if (direction == EControllerActions_MenuUp)
        s.keyRow = (s.keyRow + kKeyRows - 1) % kKeyRows;
    else if (direction == EControllerActions_MenuDown)
        s.keyRow = (s.keyRow + 1) % kKeyRows;
    else if (direction == EControllerActions_MenuLeft)
    {
        if (s.keyRow == 4)
            s.keyColumn = KeySpanStart(s.keyColumn) == 0 ? 8 : KeySpanStart(KeySpanStart(s.keyColumn) - 1);
        else
            s.keyColumn = (s.keyColumn + kKeyColumns - 1) % kKeyColumns;
    }
    else if (direction == EControllerActions_MenuRight)
    {
        if (s.keyRow == 4)
        {
            const int start = KeySpanStart(s.keyColumn);
            s.keyColumn = start == 8 ? 0 : (start == 0 ? 2 : (start == 2 ? 4 : 8));
        }
        else
        {
            s.keyColumn = (s.keyColumn + 1) % kKeyColumns;
        }
    }
    if ((Pressed(pad, EControllerActions_MenuOk) || Pressed(pad, EControllerActions_MenuA)))
    {
        PressKey(pad, s);
        if (!s.open)
            return;
    }
    if (Pressed(pad, EControllerActions_MenuX) && s.caret > 0)
        Erase(s, s.caret - 1, s.caret);
    if (Pressed(pad, EControllerActions_MenuY))
        Insert(s, L" ");
    if (Pressed(pad, EControllerActions_MenuPageUp))
        s.shift = !s.shift;
    if (Pressed(pad, EControllerActions_MenuPageDown))
        Accept(s);
    if (!s.suggestions.empty())
    {
        const int count = static_cast<int>(s.suggestions.size());
        if (Pressed(pad, EControllerActions_MenuLeftScroll))
            s.selected = (s.selected + count - 1) % count;
        if (Pressed(pad, EControllerActions_MenuRightScroll))
            s.selected = (s.selected + 1) % count;
    }
    if (Pressed(pad, EControllerActions_MenuStart))
        Send(pad);
}

#if LCE_PLATFORM_PC
void TickKeyboard(int pad, State& s)
{
    std::wstring typed;
    if (g_pInput->ConsumeCapturedText(&typed))
        Insert(s, typed);
    for (int key = g_pInput->ConsumeTextKey(); key != C_4JInput::ETextKey_None; key = g_pInput->ConsumeTextKey())
    {
        switch (key)
        {
        case C_4JInput::ETextKey_Backspace:
            if (s.caret > 0)
                Erase(s, s.caret - 1, s.caret);
            break;
        case C_4JInput::ETextKey_WordBackspace:
            Erase(s, WordLeft(s), s.caret);
            break;
        case C_4JInput::ETextKey_Delete:
            Erase(s, s.caret, s.caret + 1);
            break;
        case C_4JInput::ETextKey_Left:
            MoveCaret(s, s.caret > 0 ? s.caret - 1 : 0);
            break;
        case C_4JInput::ETextKey_Right:
            MoveCaret(s, s.caret + 1);
            break;
        case C_4JInput::ETextKey_WordLeft:
            MoveCaret(s, WordLeft(s));
            break;
        case C_4JInput::ETextKey_WordRight:
            MoveCaret(s, WordRight(s));
            break;
        case C_4JInput::ETextKey_Home:
            MoveCaret(s, 0);
            break;
        case C_4JInput::ETextKey_End:
            MoveCaret(s, s.text.size());
            break;
        case C_4JInput::ETextKey_Enter:
            Send(pad);
            return;
        case C_4JInput::ETextKey_Escape:
            ChatScreen::Close(pad);
            return;
        case C_4JInput::ETextKey_Tab:
            Accept(s);
            break;
        case C_4JInput::ETextKey_Up:
            if (s.suggestions.size() > 1)
                s.selected = (s.selected + static_cast<int>(s.suggestions.size()) - 1)
                             % static_cast<int>(s.suggestions.size());
            else
                Recall(s, true);
            break;
        case C_4JInput::ETextKey_Down:
            if (s.suggestions.size() > 1)
                s.selected = (s.selected + 1) % static_cast<int>(s.suggestions.size());
            else
                Recall(s, false);
            break;
        case C_4JInput::ETextKey_PageUp:
            s.scroll += 3;
            break;
        case C_4JInput::ETextKey_PageDown:
            s.scroll = s.scroll > 3 ? s.scroll - 3 : 0;
            break;
        default:
            break;
        }
    }
}
#endif

void Text(Font& font, const std::wstring& text, int x, int y, unsigned int colour, bool shadow)
{
    GlStateManager::enableBlend();
    GlStateManager::blendFunc(4, 5);
    GlStateManager::enableTexture();
    if (shadow)
        font.drawShadow(text, x, y, static_cast<int>(colour));
    else
        font.draw(text, x, y, static_cast<int>(colour));
}

struct VisualLine
{
    std::wstring text;
    unsigned int colour;
    float opacity;
};

std::wstring ActiveFormat(const std::wstring& text)
{
    std::wstring colour;
    std::wstring formats;
    for (size_t index = 0; index + 1 < text.size(); ++index)
    {
        if (text[index] != 0xA7)
            continue;
        const wchar_t code = static_cast<wchar_t>(towlower(text[++index]));
        const std::wstring pair = std::wstring(1, static_cast<wchar_t>(0xA7)) + code;
        if ((code >= L'0' && code <= L'9') || (code >= L'a' && code <= L'f'))
        {
            colour = pair;
            formats.clear();
        }
        else if (code == L'r')
        {
            colour.clear();
            formats.clear();
        }
        else if (code >= L'k' && code <= L'o')
        {
            formats += pair;
        }
    }
    return colour + formats;
}

void WrapMessage(Font& font, const GuiMessage& message, int width, float opacity, std::vector<VisualLine>& lines)
{
    std::vector<VisualLine> wrapped;
    std::wstring rest = message.message;
    std::wstring carried;
    while (rest.size() > carried.size())
    {
        const size_t minimum = carried.size() + 1;
        size_t length = rest.size();
        while (length > minimum && font.width(rest.substr(0, length)) > width)
            --length;
        if (length < rest.size())
        {
            const size_t space = rest.find_last_of(L' ', length);
            if (space != std::wstring::npos && space >= minimum)
                length = space + 1;
            if (length > minimum && rest[length - 1] == 0xA7)
                --length;
        }
        const std::wstring text = rest.substr(0, length);
        VisualLine line = {text, message.colour, opacity};
        wrapped.push_back(line);
        carried = ActiveFormat(text);
        rest = carried + rest.substr(length);
    }
    for (size_t index = wrapped.size(); index > 0; --index)
        lines.push_back(wrapped[index - 1]);
}

unsigned int WithAlpha(unsigned int colour, float opacity)
{
    int alpha = static_cast<int>(opacity * 255.0f);
    if (alpha < 4)
        alpha = 4;
    if (alpha > 255)
        alpha = 255;
    return (static_cast<unsigned int>(alpha) << 24) | (colour & 0xFFFFFF);
}

int DrawHistory(Gui& gui, Font& font, int pad, int left, int bottom, int width, int maxLines, int scroll, bool open)
{
    std::vector<VisualLine> lines;
    const std::vector<GuiMessage>& messages = gui.GetMessages(pad);
    for (size_t index = 0; index < messages.size() && static_cast<int>(lines.size()) < maxLines + scroll; ++index)
    {
        const float opacity = open ? 1.0f : gui.GetMessageOpacity(pad, static_cast<unsigned int>(index));
        if (opacity <= 0.0f)
            break;
        WrapMessage(font, messages[index], width - 4, opacity, lines);
    }
    if (scroll > static_cast<int>(lines.size()) - maxLines)
        scroll = static_cast<int>(lines.size()) - maxLines;
    if (scroll < 0)
        scroll = 0;
    const int shown = static_cast<int>(lines.size()) - scroll < maxLines ? static_cast<int>(lines.size()) - scroll
                                                                          : maxLines;
    for (int index = 0; index < shown; ++index)
    {
        const VisualLine& line = lines[scroll + index];
        const int y = bottom - (index + 1) * kLineHeight;
        gui.fill(left, y, left + width, y + kLineHeight, WithAlpha(0x000000, line.opacity * 0.5f));
        Text(font, line.text, left + 2, y + 1, WithAlpha(line.colour, line.opacity), true);
    }
    if (open && static_cast<int>(lines.size()) > maxLines)
    {
        const int total = kLineHeight * maxLines;
        const int barHeight = total * maxLines / static_cast<int>(lines.size());
        const int barBottom = bottom - (total - barHeight) * scroll / (static_cast<int>(lines.size()) - maxLines);
        gui.fill(left + width - 1, barBottom - barHeight, left + width, barBottom, 0xFFC6C6C6);
    }
    return scroll;
}

void DrawField(Gui& gui, Font& font, State& s, int left, int top, int right, bool underline, int* textX,
               size_t* firstVisible)
{
    gui.fill(left, top, right, top + kFieldHeight, underline ? kPanelFill : kFieldFill);
    if (underline)
        gui.fill(left, top + kFieldHeight - 1, right, top + kFieldHeight, 0xFF000000 | kYellow);
    const int available = right - left - 8;
    size_t first = 0;
    while (first < s.caret && font.width(s.text.substr(first, s.caret - first)) > available - 6)
        ++first;
    size_t last = s.text.size();
    while (last > s.caret && font.width(s.text.substr(first, last - first)) > available)
        --last;
    const int x = left + 3;
    const std::wstring shown = s.text.substr(first, last - first);
    Text(font, shown, x, top + 2, 0xFFFFFFFF, true);
    const int caretX = x + font.width(s.text.substr(first, s.caret - first));
    if (s.caret == s.text.size() && !s.suggestions.empty())
    {
        const std::wstring current = s.text.substr(s.replaceFrom);
        const std::wstring insert = Trim(s.suggestions[s.selected].insert);
        if (StartsWithIgnoringCase(insert, current) && insert.size() > current.size())
            Text(font, insert.substr(current.size()), caretX, top + 2, 0xFF000000 | kDarkGrey, true);
    }
    if ((s.ticks / 6) % 2 == 0)
    {
        if (s.caret == s.text.size())
            Text(font, L"_", caretX, top + 2, 0xFFFFFFFF, true);
        else
            gui.fill(caretX, top + 2, caretX + 1, top + kFieldHeight - 2, 0xFFE0E0E0);
    }
    *textX = x;
    *firstVisible = first;
}

void DrawSuggestions(Gui& gui, Font& font, State& s, int anchorX, int bottom, int minX, int maxX)
{
    std::vector<std::wstring> labels;
    std::vector<std::wstring> usages;
    size_t first = 0;
    if (!s.suggestions.empty())
    {
        const size_t selected = static_cast<size_t>(s.selected);
        if (s.suggestions.size() > kVisibleSuggestions && selected >= kVisibleSuggestions)
            first = selected - kVisibleSuggestions + 1;
        for (size_t index = first; index < s.suggestions.size() && index < first + kVisibleSuggestions; ++index)
        {
            labels.push_back(Trim(s.suggestions[index].insert));
            usages.push_back(s.suggestions[index].usage);
        }
    }
    else if (!s.usage.empty())
    {
        labels.push_back(s.usage);
        usages.push_back(std::wstring());
    }
    if (labels.empty())
        return;

    int width = 0;
    for (size_t index = 0; index < labels.size(); ++index)
    {
        int rowWidth = font.width(labels[index]);
        if (!usages[index].empty())
            rowWidth += font.width(L" " + usages[index]);
        if (rowWidth > width)
            width = rowWidth;
    }
    width += 10;
    int x0 = anchorX - 4;
    if (x0 + width > maxX)
        x0 = maxX - width;
    if (x0 < minX)
        x0 = minX;
    const int y1 = bottom;
    const int y0 = y1 - static_cast<int>(labels.size()) * kRowHeight - 4;
    gui.fill(x0, y0, x0 + width, y1, kSuggestionFill);

    for (size_t index = 0; index < labels.size(); ++index)
    {
        const int y = y0 + 2 + static_cast<int>(index) * kRowHeight;
        const bool selected = !s.suggestions.empty() && static_cast<int>(first + index) == s.selected;
        int x = x0 + 5;
        Text(font, labels[index], x, y + 1, 0xFF000000 | (selected ? kYellow : kGrey), true);
        x += font.width(labels[index]);
        if (!usages[index].empty())
            Text(font, L" " + usages[index], x, y + 1, 0xFF000000 | kDarkGrey, true);
    }
}

void DrawButton(Gui& gui, Font& font, int x0, int y0, int x1, int y1, const std::wstring& label, bool focused,
                bool centred)
{
    gui.fill(x0, y0, x1, y1, focused ? kKeyFocusFill : kKeyFill);
    const unsigned int colour = 0xFF000000 | (focused ? kYellow : kButtonText);
    const int textY = y0 + (y1 - y0 - 8) / 2;
    if (centred)
        Text(font, label, (x0 + x1 - font.width(label)) / 2, textY, colour, true);
    else
        Text(font, label, x0 + 5, textY, colour, true);
}

void DrawTabs(Gui& gui, Font& font, int left, int panelTop, int tab)
{
    const wchar_t* const names[2] = {L"Quick Chat", L"Keyboard"};
    for (int index = 0; index < 2; ++index)
    {
        const bool selected = index == tab;
        const int x0 = left + index * kTabWidth;
        const int y0 = panelTop - kTabHeight;
        gui.fill(x0, y0, x0 + kTabWidth, panelTop, selected ? kPanelFill : kTabFill);
        const std::wstring label = names[index];
        Text(font, label, x0 + (kTabWidth - font.width(label)) / 2, y0 + (kTabHeight - 8) / 2,
             0xFF000000 | (selected ? kYellow : kGrey), true);
    }
}

int QuickChatHeight(const State& s)
{
    return QuickItemCount(s) * (kButtonHeight + kButtonGap) - kButtonGap + 2 * kPanelPadding;
}

int KeyboardHeight()
{
    return kFieldHeight + 3 + 10 + 3 + kKeyRows * (kButtonHeight + kButtonGap) - kButtonGap + 2 * kPanelPadding;
}

void DrawQuickChat(Gui& gui, Font& font, int pad, State& s, int left, int top, int right)
{
    const int count = QuickItemCount(s);
    for (int index = 0; index < count; ++index)
    {
        const int y = top + kPanelPadding + index * (kButtonHeight + kButtonGap);
        DrawButton(gui, font, left + kPanelPadding, y, right - kPanelPadding, y + kButtonHeight,
                   QuickItemLabel(pad, s, index), index == s.quickIndex, false);
    }
}

std::wstring KeyLabel(const State& s, int row, int column)
{
    if (row < 4)
    {
        wchar_t character = (s.symbols ? kSymbolRows : kLetterRows)[row][column];
        if (s.shift && !s.symbols)
            character = static_cast<wchar_t>(towupper(character));
        return std::wstring(1, character);
    }
    switch (KeySpanStart(column))
    {
    case 0:
        return L"Shift";
    case 2:
        return s.symbols ? L"abc" : L"!?#";
    case 4:
        return L"Space";
    default:
        return L"Send";
    }
}

int ChipsWidth(Font& font, const State& s, int first, int last)
{
    int width = 0;
    for (int index = first; index <= last; ++index)
        width += font.width(Trim(s.suggestions[index].insert)) + 8 + (index > first ? 3 : 0);
    return width;
}

void DrawKeyboard(Gui& gui, Font& font, State& s, int left, int top, int right)
{
    const int inner = left + kPanelPadding;
    const int innerRight = right - kPanelPadding;
    int textX;
    size_t firstVisible;
    DrawField(gui, font, s, inner, top + kPanelPadding, innerRight, true, &textX, &firstVisible);

    const int chipY = top + kPanelPadding + kFieldHeight + 3;
    const int count = static_cast<int>(s.suggestions.size());
    const int arrowWidth = font.width(L">") + 3;
    const int selected = s.selected < count ? s.selected : count - 1;
    int first = 0;
    while (first < selected && ChipsWidth(font, s, first, selected) > innerRight - inner - 2 * arrowWidth)
        ++first;
    int chipX = inner;
    if (first > 0)
    {
        Text(font, L"<", inner, chipY + 1, 0xFF000000 | kGrey, true);
        chipX += arrowWidth;
    }
    int index = first;
    for (; index < count; ++index)
    {
        const std::wstring label = Trim(s.suggestions[index].insert);
        const int chipWidth = font.width(label) + 8;
        const int limit = innerRight - (index + 1 < count ? arrowWidth : 0);
        if (chipX + chipWidth > limit && index > first)
            break;
        gui.fill(chipX, chipY, chipX + chipWidth, chipY + 10, kKeyFill);
        Text(font, label, chipX + 4, chipY + 1, 0xFF000000 | (index == s.selected ? kYellow : kGrey), true);
        chipX += chipWidth + 3;
    }
    if (index < count)
        Text(font, L">", innerRight - font.width(L">"), chipY + 1, 0xFF000000 | kGrey, true);

    const int gridTop = chipY + 13;
    const int keyWidth = (innerRight - inner - (kKeyColumns - 1) * kButtonGap) / kKeyColumns;
    for (int row = 0; row < kKeyRows; ++row)
    {
        const int y = gridTop + row * (kButtonHeight + kButtonGap);
        for (int column = 0; column < kKeyColumns; ++column)
        {
            int span = 1;
            if (row == 4)
            {
                if (KeySpanStart(column) != column)
                    continue;
                span = column == 4 ? 4 : 2;
            }
            const int x0 = inner + column * (keyWidth + kButtonGap);
            const int x1 = x0 + span * keyWidth + (span - 1) * kButtonGap;
            const bool focused = row == s.keyRow && (row == 4 ? KeySpanStart(s.keyColumn) == column : s.keyColumn == column);
            const bool latched = row == 4 && ((column == 0 && s.shift) || (column == 2 && s.symbols));
            DrawButton(gui, font, x0, y, x1, y + kButtonHeight, KeyLabel(s, row, column), focused || latched, true);
        }
    }
}
}

namespace
{
bool OpenScreen(int pad, ChatScreen::EOpenMode mode, bool afterMenus)
{
    if (pad < 0 || pad >= 4)
        return false;
    Minecraft* minecraft = Minecraft::GetInstance();
    State& s = sStates[pad];
    s.pad = pad;
    if (s.open || !minecraft->mLocalPlayers[pad] || !minecraft->mLocalPlayers[pad]->isAlive()
        || (!afterMenus && gConsoleUIController.GetMenuDisplayed(pad)))
        return false;

    s.open = true;
    s.controller = mode == ChatScreen::EOpenMode_Controller;
    s.pendingTicks = 0;
    s.justOpened = true;
    s.systemKeyboard = false;
    s.fullHistory = false;
    s.tab = ETab_QuickChat;
    s.recall = -1;
    s.draft.clear();
    s.scroll = 0;
    s.quickIndex = 0;
    s.keyRow = 1;
    s.keyColumn = 0;
    s.shift = false;
    s.symbols = false;
    s.ticks = 0;
    s.heldAction = -1;
    s.nextRepeatTime = 0;
    s.openedTime = System::processTimeInMilliSecs();
    for (int index = 0; index < 16; ++index)
        s.tooltips[index] = -2;
    SetText(s, mode == ChatScreen::EOpenMode_Command ? L"/" : L"");

    gConsoleUIController.SetMenuDisplayed(pad, true);
#if LCE_PLATFORM_PC
    if (!s.controller)
        g_pInput->BeginTextCapture(pad);
#endif
    UpdateTooltips(pad);
    return true;
}
}

namespace ChatScreen
{
bool Open(int pad, EOpenMode mode)
{
    return OpenScreen(pad, mode, false);
}

void OpenAfterMenus(int pad, EOpenMode mode)
{
    if (pad < 0 || pad >= 4)
        return;
    sStates[pad].pendingTicks = kPendingTicks;
    sStates[pad].pendingMode = mode;
}

void Close(int pad)
{
    if (pad < 0 || pad >= 4)
        return;
    State& s = sStates[pad];
    if (!s.open)
        return;
    s.open = false;
#if LCE_PLATFORM_PC
    if (!s.controller)
        g_pInput->EndTextCapture();
#endif
    if (s.systemKeyboard)
    {
        s.systemKeyboard = false;
        g_pInput->DestroyKeyboard();
    }
    gConsoleUIController.SetTooltips(pad, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, false, false);
    gConsoleUIController.SetMenuDisplayed(pad, false);
    Tooltips::ForceTooltipReload(pad);
}

bool IsOpen(int pad)
{
    return pad >= 0 && pad < 4 && sStates[pad].open;
}

void Tick(int pad)
{
    if (pad < 0 || pad >= 4)
        return;
    State& s = sStates[pad];
    if (!s.open && s.pendingTicks > 0)
    {
        --s.pendingTicks;
        if (gConsoleUIController.GetTopScene(pad, EUILayer_Scene, EUIGroup_PlayerDefault) == NULL)
        {
            s.pendingTicks = 0;
            OpenScreen(pad, static_cast<EOpenMode>(s.pendingMode), true);
        }
    }
    if (!s.open)
        return;
    Minecraft* minecraft = Minecraft::GetInstance();
    if (!minecraft->mLocalPlayers[pad] || !minecraft->mLocalPlayers[pad]->isAlive() || minecraft->mLevel == NULL)
    {
        Close(pad);
        return;
    }
    s.ticks = static_cast<int>((System::processTimeInMilliSecs() - s.openedTime) * 60 / 1000);
    if (s.justOpened)
    {
        s.justOpened = false;
        return;
    }
    if (s.systemKeyboard)
        return;
    if (s.controller)
        TickController(pad, s);
#if LCE_PLATFORM_PC
    else
        TickKeyboard(pad, s);
#endif
    if (s.open)
        UpdateTooltips(pad);
}

void Render(int pad, Gui& gui, Font& font, int width, int height, float pixelsPerUnit)
{
    if (pad < 0 || pad >= 4)
        return;
    State& s = sStates[pad];
    const float scale = kPixelsPerUnit / pixelsPerUnit;
    width = static_cast<int>(static_cast<float>(width) / scale);
    height = static_cast<int>(static_cast<float>(height) / scale);
    const int left = width / 20;
    const int right = width - left;
    const int fieldBottom = height - height / 9;
    const int fieldTop = fieldBottom - kFieldHeight;

    GlStateManager::pushMatrix();
    GlStateManager::scalef(scale, scale, 1.0f);
    GlStateManager::disableDepthTest();
    GlStateManager::enableBlend();
    GlStateManager::blendFunc(4, 5);

    if (!s.open)
    {
        if (GameSettings::GetGameSettings(pad, eGameSetting_DisplayHUD) != 0
            && !gConsoleUIController.GetMenuDisplayed(pad))
        {
            const int historyWidth = width * 3 / 4;
            DrawHistory(gui, font, pad, left, height * 7 / 10, historyWidth, kHistoryLines, 0, false);
        }
        GlStateManager::enableDepthTest();
        GlStateManager::popMatrix();
        return;
    }

    if (!s.controller)
    {
        s.scroll = DrawHistory(gui, font, pad, left, fieldTop - 3, right - left, kHistoryLines, s.scroll, true);
        int textX;
        size_t firstVisible;
        DrawField(gui, font, s, left, fieldTop, right, false, &textX, &firstVisible);
        const size_t anchor = s.replaceFrom > firstVisible ? s.replaceFrom : firstVisible;
        DrawSuggestions(gui, font, s, textX + font.width(s.text.substr(firstVisible, anchor - firstVisible)),
                        fieldTop - 2, left, right);
    }
    else if (s.fullHistory)
    {
        const int top = height / 10;
        s.scroll = DrawHistory(gui, font, pad, left, fieldBottom, right - left, (fieldBottom - top) / kLineHeight,
                               s.scroll, true);
    }
    else
    {
        const int panelLeft = right - kPanelWidth > left ? right - kPanelWidth : left;
        const int panelHeight = s.tab == ETab_QuickChat ? QuickChatHeight(s) : KeyboardHeight();
        const int panelTop = fieldBottom - panelHeight;
        const int historyRight = panelLeft - 6;
        if (historyRight - left > 60)
            DrawHistory(gui, font, pad, left, fieldBottom, historyRight - left, kHistoryLines, 0, true);
        DrawTabs(gui, font, panelLeft, panelTop, s.tab);
        gui.fill(panelLeft, panelTop, right, fieldBottom, kPanelFill);
        if (s.tab == ETab_QuickChat)
            DrawQuickChat(gui, font, pad, s, panelLeft, panelTop, right);
        else
            DrawKeyboard(gui, font, s, panelLeft, panelTop, right);
    }
    GlStateManager::enableDepthTest();
    GlStateManager::popMatrix();
}

bool DrawnOverHud(int pad)
{
    if (pad < 0 || pad >= 4)
        return false;
    const bool drawn = sDrawnOverHud[pad];
    sDrawnOverHud[pad] = false;
    return drawn;
}

void RenderOverHud(int pad, int viewport, int width, int height)
{
    Minecraft* minecraft = Minecraft::GetInstance();
    if (pad < 0 || pad >= 4 || minecraft == NULL || minecraft->mGui == NULL || minecraft->mFont == NULL
        || minecraft->mHeight <= 0)
        return;
    sDrawnOverHud[pad] = true;

    float screenWidth = 0.0f;
    float viewportWidth = static_cast<float>(width);
#if LCE_PLATFORM_PC
    const Renderer::ELayout layout = gRenderer->GetLayout();
    const bool fullWindow = viewport == C4JRender::eViewportType_FullScreen;
    if (fullWindow)
    {
        gRenderer->SetLayout(Renderer::ELayout_Screen);
        screenWidth = gConsoleUIController.getScreenHeight() * gRenderer->GetScreenAspect();
        viewportWidth = screenWidth;
    }
#endif
    const float unit = gConsoleUIController.getScreenHeight() / static_cast<float>(minecraft->mHeight);
    int originX = 0;
    int originY = 0;
    DebugOverlay::ViewportOrigin(viewport, &originX, &originY);
    DebugOverlay::BeginScreenDraw(originX, originY, unit, screenWidth);
    GlStateManager::enableBlend();
    GlStateManager::blendFunc(4, 5);
    GlStateManager::color4f(1.0f, 1.0f, 1.0f, 1.0f);
    Render(pad, *minecraft->mGui, *minecraft->mFont, static_cast<int>(viewportWidth / unit),
           static_cast<int>(static_cast<float>(height) / unit), 1.0f);
    GlStateManager::disableDepthTest();
    DebugOverlay::EndScreenDraw();
#if LCE_PLATFORM_PC
    if (fullWindow)
        gRenderer->SetLayout(layout);
#endif
}

void RememberSender(int pad, const std::wstring& name)
{
    if (pad >= 0 && pad < 4)
        sStates[pad].lastSender = name;
}
}
