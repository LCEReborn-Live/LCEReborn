#include "game/core/L10N.hpp"

#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/ShieldStrings.hpp"
#include "game/data/StringTable.hpp"

static StringTable* g_stringTable = NULL;
extern CConsoleMinecraftApp gApp;

const wchar_t* ExtraString(int id)
{
    switch (id)
    {
    case IDS_LCE_ITEM_SHIELD:
    case IDS_LCE_DESC_SHIELD:
        return LocalizedShieldString(XGetLanguage(), id == IDS_LCE_DESC_SHIELD);
    case IDS_LCE_ITEM_CROSSBOW:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Арбалет" : L"Crossbow";
    case IDS_LCE_DESC_CROSSBOW:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN
            ? L"Удерживайте кнопку, чтобы натянуть тетиву, затем отпустите. Нажмите ещё раз, чтобы выстрелить."
            : L"Hold to draw, then release to load. Use again to shoot.";
    case IDS_LCE_CROSSBOW_DRAW:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Натянуть тетиву" : L"Draw Crossbow";
    case IDS_LCE_CROSSBOW_LOAD:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Отпустить для зарядки" : L"Release to Load";
    case IDS_LCE_CROSSBOW_FIRE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Выстрелить" : L"Fire Crossbow";
    case IDS_LCE_CROSSBOW_PROJECTILE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Снаряд:" : L"Projectile:";
    case IDS_LCE_ENCHANTMENT_MULTISHOT:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Тройной выстрел" : L"Multishot";
    case IDS_LCE_ENCHANTMENT_QUICK_CHARGE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Быстрая перезарядка" : L"Quick Charge";
    case IDS_LCE_ENCHANTMENT_PIERCING:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Пронзание" : L"Piercing";
    case IDS_LCE_CHAT:
        return L"Chat";
    case IDS_LCE_CHAT_SEND:
        return L"Send";
    case IDS_LCE_CHAT_CLOSE:
        return L"Close";
    case IDS_LCE_CHAT_COMPLETE:
        return L"Complete";
    case IDS_LCE_CHAT_TYPE:
        return L"Type";
    case IDS_LCE_CHAT_DELETE:
        return L"Delete";
    case IDS_LCE_CHAT_SPACE:
        return L"Space";
    case IDS_LCE_CHAT_SHIFT:
        return L"Shift";
    case IDS_LCE_CHAT_QUICK:
        return L"Quick Chat";
    case IDS_LCE_CHAT_KEYBOARD:
        return L"Keyboard";
    case IDS_LCE_CHAT_CHOOSE:
        return L"Choose";
    case IDS_LCE_CHAT_HISTORY:
        return L"History";
    case IDS_LCE_CHAT_SCROLL:
        return L"Scroll";
    case IDS_LCE_DEBUG_CHANGE:
        return L"Change";
    case IDS_LCE_DEBUG_PREVIEW:
        return L"Preview";
    case IDS_LCE_DEBUG_PRESET:
        return L"Preset";
    case IDS_LCE_DEBUG_RESET:
        return L"Reset";
    case IDS_LCE_HOLD_TO_SNEAK:
        return L"Hold to Sneak";
    case IDS_LCE_SHADER_PACKS:
        return L"Shader Packs";
    case IDS_LCE_SHADER_SETTINGS:
        return L"Shader Settings";
    case IDS_LCE_SHADER_PACK_SETTINGS:
        return L"Shader Pack Settings";
    case IDS_LCE_SHADER_REFRESH:
        return L"Refresh";
    case IDS_LCE_SHADER_APPLY:
        return L"Apply";
    case IDS_LCE_ONLINE_SETTINGS:
        return L"Online Settings";
    case IDS_LCE_HOST_PRIVILEGES_WARNING:
        return L"Turning on Host Privileges disables achievements for this world, even if you turn it off again "
               L"later. Are you sure you want to continue?";
    case IDS_LCE_TURN_ON:
        return L"Turn On";
    case IDS_LCE_GAME_MODE:
        return L"Game Mode";
    case IDS_LCE_CREATIVE_WARNING:
        return L"Setting this world to Creative Mode disables achievements for it, even if you set it back to "
               L"Survival Mode later. Are you sure you want to continue?";
    case IDS_LCE_SET_TO_CREATIVE:
        return L"Set to Creative";
    case IDS_LCE_STOP_LAN_WORLD:
        return L"Stop LAN World";
    case IDS_LCE_STOP_LAN_WARNING:
        return L"Players who joined over the network will be disconnected. Players on this console keep playing, "
               L"and you can open the world to LAN again from the pause menu.";
    case IDS_LCE_SERVER_PINGING:
        return L"Pinging...";
    case IDS_LCE_SERVER_CANT_CONNECT:
        return L"Can't connect to server";
    case IDS_LCE_CLIENT_OUT_OF_DATE:
        return L"Client out of date!";
    case IDS_LCE_SERVER_OUT_OF_DATE:
        return L"Server out of date!";
    case IDS_LCE_LEADERBOARD_TRAVELLING:
        return L"Travelling";
    case IDS_LCE_LEADERBOARD_MINING:
        return L"Mining";
    case IDS_LCE_LEADERBOARD_FARMING:
        return L"Farming";
    case IDS_LCE_LEADERBOARD_KILLS:
        return L"Kills";
    case IDS_LCE_LEADERBOARD_MY_STATISTICS:
        return L"My Statistics";
    case IDS_LCE_LEADERBOARD_UNAVAILABLE:
        return L"This game doesn't keep a leaderboard.";
    case IDS_LCE_LEADERBOARD_PLAYER:
        return L"Player";
    case IDS_LCE_SHADER_PREVIOUS_NEXT:
        return L"Previous / Next";
    case IDS_LCE_SHADER_DEFAULT:
        return L"Default";
    case IDS_LCE_SHADER_HIDE_MENU:
        return L"Hide Menu";
    case IDS_LCE_SHADER_SHOW_MENU:
        return L"Show Menu";
    case IDS_LCE_SHADER_OPEN:
        return L"Open";
    case IDS_LCE_SHADER_SWITCH_TAB:
        return L"Switch Tab";
    case IDS_LCE_DISCONNECTED_NAME_IN_USE:
        return L"Someone with your name is already playing in this game. Change your username to join.";
    default:
        return NULL;
    }
}

const wchar_t* L10N::GetString(int id)
{
    const wchar_t* extra = ExtraString(id);
    if (extra != NULL)
        return extra;
    if (g_stringTable == NULL)
        return L"";
    return g_stringTable->getString(id);
}

void L10N::loadStringTable()
{
    delete g_stringTable;
    std::wstring path = L"languages.loc";
    if (gApp.hasArchiveFile(path, true))
    {
        arrayWithLength<unsigned char> data = gApp.getArchiveFile(path, true);
        g_stringTable = new StringTable(data, false, false);
        delete[] data.data;
    }
    else
    {
        g_stringTable = NULL;
    }
}
