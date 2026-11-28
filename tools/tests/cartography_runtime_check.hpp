#include "game/client/render/TextureAtlasSprite.hpp"
#include "game/core/GameType.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/User.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/SavedDataStorage.hpp"
#include "game/item/CraftingContainer.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/MapItem.hpp"
#include "game/item/MapItemSavedData.hpp"
#include "game/item/Recipes.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/CartographyMenu.hpp"
#include "game/ui/UIScene_FurnaceMenu.hpp"
#include "game/ui/FJ_SlotList.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/LocalBlockInteraction.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"
#include "game/world/generation/MultiPlayerGameMode.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include <cstdio>
#include <cstdlib>

class CartographyCheckPlayer : public MultiplayerLocalPlayer
{
public:
    using MultiplayerLocalPlayer::MultiplayerLocalPlayer;
    AbstractContainerMenu* openedMenu = nullptr;
    int openedType = -1;

    int startBlockInteraction(InteractionObject& interaction) override
    {
        openedType = interaction.getTypeId(false);
        openedMenu = interaction.createMenu(mInventory, boost::static_pointer_cast<Player>(shared_from_this()));
        return 1;
    }

    void awardStat(Stat*, arrayWithLength<unsigned char> args) override { delete[] args.data; }
};

static int RunCartographyRegression()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto make = [](Item* item, int count = 1, int data = 0) {
        return not_null_ptr<ItemInstance>(new ItemInstance(item, count, data));
    };
    auto grid = boost::shared_ptr<CraftingContainer>(new CraftingContainer(nullptr, 3, 3));
    grid->setItem(0, make(Items::paper));
    grid->setItem(1, make(Items::paper));
    for (unsigned int slot : {3U, 4U, 6U, 7U})
        grid->setItem(slot, make(Item::byBlock(Blocks::planks), 1, slot % 6));
    auto crafted = Recipes::getInstance()->getItemFor(grid, nullptr, nullptr, Recipes::eRecipeCategory_Crafting);
    if (crafted->getItem() != Item::byBlock(Blocks::cartography_table))
        return 1;
    auto* top = Blocks::cartography_table->getTexture(Direction::UP, Blocks::cartography_table->defaultBlockState());
    if (!top || top->getName() != L"cartography_table_top")
        return 2;
    std::puts("PASS: recipe with mixed planks and TU80 cartography art");

    auto* level = new MultiPlayerLevel(nullptr, new LevelSettings(1, GameType::SURVIVAL, false, false, false,
        LevelType::DEFAULT, 54, 3, 128, false, 0, 0, 0), 0, Difficulty::NORMAL, 63);
    level->setSavedDataStorage(new SavedDataStorage(nullptr));
    CGameNetworkManager::sInstance.Initialise();
    level->setChunkVisible(0, 0, true, false);
    level->mIsClientSide = false;
    const BlockPos pos(8, 64, 8);
    level->setBlock(pos, Blocks::cartography_table->defaultBlockState(), Block::UPDATE_CLIENTS, false);
    User user(L"CartographyCheck", L"");
    auto player = boost::shared_ptr<CartographyCheckPlayer>(new CartographyCheckPlayer(Minecraft::GetInstance(), level, &user, nullptr));
    auto* minecraft = Minecraft::GetInstance();
    player->mUserIndex = 0;
    minecraft->mGameModes[0] = minecraft->mGameMode = new MultiPlayerGameMode(0, nullptr, 0, false, false);
    player->setPos(8.5, 64, 9.5);
    Blocks::cartography_table->use(level, pos, level->getBlockState(pos), player,
        InteractionHand::eInteractionHand_MainHand, Direction::SOUTH, 0.5f, 0.5f, 0.5f, false);
    auto* menu = dynamic_cast<CartographyMenu*>(player->openedMenu);
    if (!menu || player->openedType != ClientboundContainerOpenPacket::CARTOGRAPHY || !menu->stillValid(player))
        return 3;
    auto* first = menu->getSlot(0);
    auto* second = menu->getSlot(1);
    auto* result = menu->getSlot(2);
    const auto take = [&] {
        menu->clicked(2, 1, ClickType::eClickType_Pickup, player);
        auto item = player->mInventory->getCarried();
        player->mInventory->setCarried(ItemInstance::EMPTY);
        return item;
    };
    first->set(make(Items::paper, 2));
    if (take()->getItem() != Items::map || first->getItem()->getCount() != 1)
        return 4;

    const int originalId = level->getFreeAuxValueFor(L"map");
    const auto originalData = boost::shared_ptr<MapItemSavedData>(new MapItemSavedData(L"map_0"));
    originalData->mColors[31] = 35;
    level->setSavedData(L"map_0", originalData);
    first->set(make(Items::filledMap, 1, originalId));
    second->set(make(Items::map));
    for (int slot = 3; slot < 39; ++slot)
        menu->getSlot(slot)->set(make(Items::stick, 64));
    menu->getSlot(38)->set(make(Items::filledMap, 63, originalId));
    if (result->getItem()->getCount() != 2 || !menu->quickMoveStack(player, 2)->isEmpty() || !first->hasItem())
        return 5;
    menu->getSlot(37)->set(ItemInstance::EMPTY);
    if (menu->quickMoveStack(player, 2)->isEmpty() || menu->getSlot(37)->getItem()->getCount() != 2
        || menu->getSlot(37)->getItem()->getAuxValue() != originalId || first->hasItem() || second->hasItem())
        return 6;
    std::puts("PASS: empty maps and complete copy batches with limited inventory space");

    first->set(make(Items::filledMap, 1, originalId));
    second->set(make(Items::paper));
    const auto zoomed = take();
    const auto zoomedData = MapItem::getSavedDataIfExists(zoomed->getAuxValue(), level);
    if (zoomed->getItem() != Items::filledMap || zoomed->getAuxValue() == originalId || !zoomedData
        || zoomedData->getScale() != 1 || originalData->getScale() != 0)
        return 7;

    first->set(make(Items::filledMap, 1, originalId));
    second->set(make(Item::byBlock(Blocks::glass_pane)));
    const auto locked = take();
    const auto lockedData = MapItem::getSavedDataIfExists(locked->getAuxValue(), level);
    if (locked->getItem() != Items::filledMap || locked->getAuxValue() == originalId || !lockedData
        || !lockedData->isLocked() || originalData->isLocked() || lockedData->mColors[31] != 35
        || !locked->hasTag() || !locked->getTag()->getBoolean(L"map_locked"))
        return 8;
    originalData->mColors[32] = 36;
    lockedData->mergeInMapData(originalData);
    CompoundTag saved;
    lockedData->save(&saved);
    MapItemSavedData restored(L"restored");
    restored.load(&saved);
    if (!restored.isLocked() || restored.mColors[31] != 35 || restored.mColors[32] != 0)
        return 9;
    first->set(locked);
    second->set(make(Items::paper));
    if (result->hasItem())
        return 10;
    second->set(make(Items::map));
    if (result->getItem()->getCount() != 2 || result->getItem()->getAuxValue() != locked->getAuxValue())
        return 11;
    std::puts("PASS: zoom, independent locked map data, save round trip, and copying locked maps");

    level->mIsClientSide = true;
    minecraft->mLevel = level;
    minecraft->mPlayer = player;
    minecraft->mLocalPlayers[0] = player;
    const bool split = std::getenv("LCE_CARTOGRAPHY_SPLIT") != nullptr;
    if (split)
    {
        minecraft->mLocalPlayers[1].reset(new CartographyCheckPlayer(minecraft, level, &user, nullptr));
        player->mViewport = C4JRender::eViewportType_TopHalf;
        minecraft->mLocalPlayers[1]->mViewport = C4JRender::eViewportType_BottomHalf;
        gConsoleUIController.UpdatePlayerBasePositions();
    }
    gApp.SetGameStarted(true);
    gConsoleUIController.SetupFont();
    gConsoleUIController.CloseAllPlayersScenes(true);
    for (int i = 0; i < 12; ++i)
        gConsoleUIController.CheckMenuDisplayed();
    LocalBlockInteraction interaction(ClientboundContainerOpenPacket::CARTOGRAPHY, L"");
    if (!player->LocalPlayer::startBlockInteraction(interaction)
        || !dynamic_cast<CartographyMenu*>(player->mContainerMenu))
        return 12;
    auto* scene = dynamic_cast<UIScene_FurnaceMenu*>(gConsoleUIController.FindScene(EUIScene_FurnaceMenu));
    if (!scene)
        return 13;
    auto* root = scene->getSection(IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient)
        ->getFuiRenderNode()->getRoot();
    if (scene->getMoviePath() != (split ? L"CartographyMenuSplit" : L"CartographyMenu"))
        return 14;
    const char* grids[] = {"MainPanel.inventoryList", "MainPanel.hotbarList", "MainPanel.Ingredient",
                           "MainPanel.Ingredient2", "MainPanel.Result"};
    const int columns[] = {9, 9, 1, 1, 1};
    const int rows[] = {3, 1, 1, 1, 1};
    for (int i = 0; i < 5; ++i)
    {
        auto* node = root->findNode(grids[i]);
        auto* list = node ? node->getFuiNode()->asFJ_SlotList() : nullptr;
        if (!list || list->getColumns() != columns[i] || list->getRows() != rows[i])
            return 17;
    }
    const char* nodes[] = {"MainPanel.Map_Map", "MainPanel.Map_Zoom", "MainPanel.Map_Copy",
                           "MainPanel.Map_Locked", "MainPanel.Map_Empty"};
    Item* modifiers[] = {nullptr, Items::paper, Items::map, Item::byBlock(Blocks::glass_pane), nullptr};
    auto* client = player->mContainerMenu;
    for (int preview = 0; preview < 5; ++preview)
    {
        client->setItem(0, make(preview == 4 ? Items::paper : Items::filledMap));
        client->setItem(1, modifiers[preview] ? make(modifiers[preview]) : ItemInstance::EMPTY);
        client->setItem(2, preview ? make(preview == 4 ? Items::map : Items::filledMap) : ItemInstance::EMPTY);
        scene->tick();
        for (int i = 0; i < 5; ++i)
        {
            auto* node = root->findNode(nodes[i]);
            if (!node || node->isVisible() != (i == preview))
                return 15;
        }
    }
    if (!root->findNode("MainPanel.Ingredient2") || !root->findNode("MainPanel.Result")
        || root->findNode("MainPanel.MapTextInput")->isVisible())
        return 16;
    std::puts(split ? "PASS: separate split-screen scene and five previews" : "PASS: separate full-screen scene and five previews");
    return 0;
}
