#include "game/core/L10N.hpp"

#include <array>
#include "game/world/block/DecorativeStone.hpp"

#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/ShieldStrings.hpp"
#include "game/data/StringTable.hpp"

static StringTable* g_stringTable = NULL;
extern CConsoleMinecraftApp gApp;

const wchar_t* ExtraString(int id)
{
    if (id >= IDS_LCE_WOOD_SIGN_FIRST && id <= IDS_LCE_WOOD_SIGN_LAST)
    {
        static const wchar_t* english[] = {L"Spruce Sign", L"Birch Sign", L"Jungle Sign", L"Acacia Sign",
                                           L"Dark Oak Sign"};
        static const wchar_t* russian[] = {L"Еловая табличка", L"Берёзовая табличка", L"Тропическая табличка",
                                           L"Акациевая табличка", L"Табличка из тёмного дуба"};
        return (XGetLanguage() == XC_LANGUAGE_RUSSIAN ? russian : english)[id - IDS_LCE_WOOD_SIGN_FIRST];
    }
    if (id >= IDS_LCE_TILE_STRIPPED_WOOD_FIRST && id <= IDS_LCE_TILE_STRIPPED_WOOD_LAST)
    {
        static const wchar_t* english[] = {L"Stripped Oak Wood", L"Stripped Spruce Wood", L"Stripped Birch Wood",
                                           L"Stripped Jungle Wood", L"Stripped Acacia Wood", L"Stripped Dark Oak Wood"};
        static const wchar_t* russian[] = {L"Обтёсанная дубовая древесина", L"Обтёсанная еловая древесина",
                                           L"Обтёсанная берёзовая древесина", L"Обтёсанная тропическая древесина",
                                           L"Обтёсанная акациевая древесина", L"Обтёсанная древесина тёмного дуба"};
        return (XGetLanguage() == XC_LANGUAGE_RUSSIAN ? russian : english)[id - IDS_LCE_TILE_STRIPPED_WOOD_FIRST];
    }
    constexpr int decorNames = DecorativeStone::MaterialCount * 3;
    if (id >= IDS_LCE_DECOR_STONE_FIRST && id < IDS_LCE_DECOR_STONE_FIRST + decorNames)
    {
        static const auto names = [] {
            std::array<std::array<std::wstring, decorNames>, 2> result;
            const wchar_t* english[] = {L" Stairs", L" Slab", L" Wall"};
            const wchar_t* russian[] = {L"Ступени из ", L"Плита из ", L"Стена из "};
            for (int material = 0; material < DecorativeStone::MaterialCount; ++material)
            {
                const auto& entry = DecorativeStone::materials[material];
                for (int shape = 0; shape < 3; ++shape)
                {
                    result[0][material * 3 + shape] = std::wstring(entry.name) + english[shape];
                    result[1][material * 3 + shape] = russian[shape] + std::wstring(entry.russian);
                }
            }
            return result;
        }();
        const bool russian = XGetLanguage() == XC_LANGUAGE_RUSSIAN;
        return names[russian][id - IDS_LCE_DECOR_STONE_FIRST].c_str();
    }

    switch (id)
    {
    case IDS_LCE_PILLAGER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Разбойник" : L"Pillager";
    case IDS_LCE_BAD_OMEN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Дурное знамение" : L"Bad Omen";
    case IDS_LCE_HERO_OF_THE_VILLAGE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Герой деревни" : L"Hero of the Village";
    case IDS_LCE_RAID:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Рейд" : L"Raid";
    case IDS_LCE_RAID_VICTORY:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Рейд — Победа" : L"Raid — Victory";
    case IDS_LCE_RAID_DEFEAT:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Рейд — Поражение" : L"Raid — Defeat";
    case IDS_LCE_RAID_REMAINING:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Осталось противников: " : L"Raiders remaining: ";
    case IDS_LCE_RAVAGER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Разоритель" : L"Ravager";
    case IDS_LCE_PILLAGER_CAPTAIN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Капитан разбойников" : L"Pillager Captain";
    case IDS_LCE_WANDERING_TRADER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Странствующий торговец" : L"Wandering Trader";
    case IDS_LCE_TRADER_LLAMA:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Лама торговца" : L"Trader Llama";
    case IDS_LCE_VILLAGER_MASON:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Каменщик" : L"Mason";
    case IDS_LCE_ILLAGER_BANNER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Знамя иллагеров" : L"Illager Banner";
    case IDS_LCE_PANDA:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Панда" : L"Panda";
    case IDS_LCE_CAT:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Кошка" : L"Cat";
    case IDS_LCE_DYE_FIRST:
    case IDS_LCE_DYE_FIRST + 1:
    case IDS_LCE_DYE_FIRST + 2:
    case IDS_LCE_DYE_LAST:
    {
        static const wchar_t* english[] = {L"Black Dye", L"Brown Dye", L"Blue Dye", L"White Dye"};
        static const wchar_t* russian[] = {L"Чёрный краситель", L"Коричневый краситель", L"Синий краситель", L"Белый краситель"};
        return (XGetLanguage() == XC_LANGUAGE_RUSSIAN ? russian : english)[id - IDS_LCE_DYE_FIRST];
    }
    case IDS_LCE_DYE_SOURCE_FIRST:
    case IDS_LCE_DYE_SOURCE_FIRST + 1:
    case IDS_LCE_DYE_SOURCE_FIRST + 2:
    case IDS_LCE_DYE_SOURCE_LAST:
    {
        static const wchar_t* english[] = {L"Ink Sac", L"Cocoa Beans", L"Lapis Lazuli", L"Bone Meal"};
        static const wchar_t* russian[] = {L"Чернильный мешок", L"Какао-бобы", L"Лазурит", L"Костная мука"};
        return (XGetLanguage() == XC_LANGUAGE_RUSSIAN ? russian : english)[id - IDS_LCE_DYE_SOURCE_FIRST];
    }
    case IDS_LCE_DESC_DYE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Используется для окрашивания." : L"Used to dye items and blocks.";
    case IDS_LCE_LOOM:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Ткацкий станок" : L"Loom";
    case IDS_LCE_DESC_LOOM:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Наносит узоры на знамёна." : L"Applies patterns to banners.";
    case IDS_LCE_LOOM_INPUTS:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Знамя, краситель, шаблон" : L"Banner, Dye, Stencil";
    case IDS_LCE_LOOM_PATTERNS:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Узоры" : L"Patterns";
    case IDS_LCE_LOOM_PREVIOUS:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Предыдущая страница" : L"Previous Page";
    case IDS_LCE_LOOM_NEXT:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Следующая страница" : L"Next Page";
    case IDS_LCE_STENCIL_FIRST:
    case IDS_LCE_STENCIL_FIRST + 1:
    case IDS_LCE_STENCIL_FIRST + 2:
    case IDS_LCE_STENCIL_FIRST + 3:
    case IDS_LCE_STENCIL_FIRST + 4:
    case IDS_LCE_STENCIL_LAST:
    {
        static const wchar_t* english[] = {L"Flower Charge", L"Creeper Charge", L"Skull Charge", L"Thing",
                                           L"Field Masoned", L"Bordure Indented"};
        static const wchar_t* russian[] = {L"Цветок", L"Крипер", L"Череп", L"Нечто", L"Кирпичная кладка", L"Зубчатая кайма"};
        return (XGetLanguage() == XC_LANGUAGE_RUSSIAN ? russian : english)[id - IDS_LCE_STENCIL_FIRST];
    }
    case IDS_LCE_LECTERN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Кафедра" : L"Lectern";
    case IDS_LCE_DESC_LECTERN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Позволяет читать книгу и подаёт сигнал при перелистывании."
                                                       : L"Holds a book and sends a redstone pulse when its page turns.";
    case IDS_LCE_CAMPFIRE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Костёр" : L"Campfire";
    case IDS_LCE_DESC_CAMPFIRE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Освещает пространство и готовит еду."
                                                       : L"Lights its surroundings and cooks food.";
    case IDS_LCE_BELL:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Колокол" : L"Bell";
    case IDS_LCE_DESC_BELL:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Звон предупреждает жителей деревни."
                                                       : L"Ring to warn nearby villagers.";
    case IDS_LCE_SMOOTH_SANDSTONE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Гладкий песчаник" : L"Smooth Sandstone";
    case IDS_LCE_SMOOTH_RED_SANDSTONE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Гладкий красный песчаник" : L"Smooth Red Sandstone";
    case IDS_LCE_SMOOTH_QUARTZ:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Гладкий кварц" : L"Smooth Quartz";
    case IDS_LCE_SCAFFOLDING:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Строительные леса" : L"Scaffolding";
    case IDS_LCE_DESC_SCAFFOLDING:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Помогают подниматься и строить на высоте."
                                                       : L"Climb scaffolding to build at higher elevations.";
    case IDS_LCE_BAMBOO:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Бамбук" : L"Bamboo";
    case IDS_LCE_DESC_BAMBOO:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Быстро растёт. Используется для крафта и как топливо."
                                                       : L"Grows quickly and can be used for crafting or as fuel.";
    case IDS_LCE_BIOME_BAMBOO_JUNGLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Бамбуковые джунгли" : L"Bamboo Jungle";
    case IDS_LCE_DESC_BAMBOO_JUNGLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Джунгли с густыми зарослями бамбука."
                                                       : L"A jungle with dense bamboo groves.";
    case IDS_LCE_TILE_LANTERN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Фонарь" : L"Lantern";
    case IDS_LCE_DESC_LANTERN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Освещает пространство. Можно подвесить снизу блока."
                                                       : L"A light source that can hang beneath a block.";
    case IDS_LCE_IMMEDIATE_RESPAWN:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Мгновенное возрождение" : L"Immediate Respawn";
    case IDS_LCE_TILE_LILY_OF_THE_VALLEY:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Ландыш" : L"Lily of the Valley";
    case IDS_LCE_TILE_CORNFLOWER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Василёк" : L"Cornflower";
    case IDS_LCE_ITEM_SWEET_BERRIES:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Сладкие ягоды" : L"Sweet Berries";
    case IDS_LCE_DESC_SWEET_BERRIES:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Их можно съесть или посадить."
                                                       : L"Sweet berries can be eaten or planted.";
    case IDS_LCE_TILE_SWEET_BERRY_BUSH:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Ягодный куст" : L"Sweet Berry Bush";
    case IDS_LCE_TILE_FLETCHING_TABLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Стол лучника" : L"Fletching Table";
    case IDS_LCE_DESC_FLETCHING_TABLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Рабочий стол для изготовления стрел."
                                                       : L"An arrow maker's workbench.";
    case IDS_LCE_TILE_SMITHING_TABLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Стол кузнеца" : L"Smithing Table";
    case IDS_LCE_DESC_SMITHING_TABLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Кузнечный рабочий стол." : L"A smith's workbench.";
    case IDS_LCE_TILE_STONECUTTER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Камнерез" : L"Stonecutter";
    case IDS_LCE_DESC_STONECUTTER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Изготавливает плиты, ступени и другие изделия из камня."
                                                       : L"Cuts stone into slabs, stairs, and other blocks.";
    case IDS_LCE_STONECUTTER_NEXT:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Другой рецепт" : L"Next recipe";
    case IDS_LCE_STONECUTTER_RECIPES:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Рецепты" : L"Recipes";
    case IDS_LCE_TILE_COMPOSTER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Компостер" : L"Composter";
    case IDS_LCE_DESC_COMPOSTER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Перерабатывает растения и семена в костную муку."
                                                       : L"Turns crops and seeds into bone meal.";
    case IDS_LCE_TILE_BARREL:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Бочка" : L"Barrel";
    case IDS_LCE_DESC_BARREL:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Хранилище на 27 слотов. Открывается под блоками."
                                                       : L"Stores 27 stacks and opens under other blocks.";
    case IDS_LCE_TILE_GRINDSTONE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Точило" : L"Grindstone";
    case IDS_LCE_DESC_GRINDSTONE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Ремонтирует снаряжение и снимает чары, кроме проклятий."
                                                       : L"Repairs gear and removes enchantments other than curses.";
    case IDS_LCE_TILE_CARTOGRAPHY_TABLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Стол картографа" : L"Cartography Table";
    case IDS_LCE_DESC_CARTOGRAPHY_TABLE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Создаёт, копирует, увеличивает и запечатывает карты."
                                                       : L"Creates, copies, zooms out, and locks maps.";
    case IDS_LCE_CARTOGRAPHY_MAP:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Карта" : L"Map";
    case IDS_LCE_CARTOGRAPHY_MATERIAL:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Материал" : L"Material";
    case IDS_LCE_CARTOGRAPHY_ZOOM:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Увеличение охвата" : L"Zoom out";
    case IDS_LCE_CARTOGRAPHY_COPY:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Копирование" : L"Copy";
    case IDS_LCE_CARTOGRAPHY_LOCK:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Запечатывание" : L"Lock";
    case IDS_LCE_CARTOGRAPHY_EMPTY:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Пустая карта" : L"Empty map";
    case IDS_LCE_TILE_BLAST_FURNACE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Плавильная печь" : L"Blast Furnace";
    case IDS_LCE_DESC_BLAST_FURNACE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Переплавляет руду и металлическое снаряжение вдвое быстрее."
                                                       : L"Smelts ores and metal gear twice as fast.";
    case IDS_LCE_TILE_SMOOTH_STONE:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Гладкий камень" : L"Smooth Stone";
    case IDS_LCE_TILE_SMOKER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Коптильня" : L"Smoker";
    case IDS_LCE_DESC_SMOKER:
        return XGetLanguage() == XC_LANGUAGE_RUSSIAN ? L"Готовит еду вдвое быстрее печи."
                                                       : L"Cooks food twice as fast as a furnace.";
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
