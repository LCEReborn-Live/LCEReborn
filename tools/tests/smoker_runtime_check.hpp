// Exercise the registered recipes and run the normal block/entity tick paths.
#include "game/client/render/TextureAtlasSprite.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/Direction.hpp"
#include "game/core/GameType.hpp"
#include "game/item/CraftingContainer.hpp"
#include "game/item/FurnaceRecipes.hpp"
#include "game/item/Items.hpp"
#include "game/item/Recipes.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/FurnaceMenu.hpp"
#include "game/ui/LocalMenuContainer.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/FurnaceBlock.hpp"
#include "game/world/block/FurnaceBlockEntity.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"
#include <cstdio>

static int RunSmokerRegression()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto item = [](Item* type, int count = 1) {
        return not_null_ptr<ItemInstance>(new ItemInstance(type, count));
    };
    auto grid = boost::shared_ptr<CraftingContainer>(new CraftingContainer(nullptr, 3, 3));
    const auto craft = [&] {
        return Recipes::getInstance()->getItemFor(grid, nullptr, nullptr, Recipes::eRecipeCategory_Crafting);
    };
    grid->setItem(1, item(Item::byBlock(Blocks::log)));
    grid->setItem(3, item(Item::byBlock(Blocks::log2)));
    grid->setItem(4, item(Item::byBlock(Blocks::furnace)));
    grid->setItem(5, item(Item::byBlock(Blocks::stripped_log_birch)));
    grid->setItem(7, item(Item::byBlock(Blocks::bark_dark_oak)));
    if (craft()->getItem() != Item::byBlock(Blocks::smoker))
        return 1;
    grid->setItem(1, item(Item::byBlock(Blocks::planks)));
    if (!craft()->isEmpty())
        return 2;
    grid->clearContent();
    for (unsigned int slot : {0U, 2U, 7U})
        grid->setItem(slot, item(Items::stick));
    grid->setItem(1, item(Items::ironIngot));
    grid->setItem(3, item(Items::string));
    grid->setItem(4, item(Item::byBlock(Blocks::tripwire_hook)));
    grid->setItem(5, item(Items::string));
    if (craft()->getItem() != Items::crossbow || Items::crossbow->getBaseItemType() != Items::bow->getBaseItemType())
        return 3;
    bool assistedSmoker = false, assistedCrossbow = false;
    for (Recipe* recipe : *Recipes::getInstance()->getRecipes(Recipes::eRecipeCategory_Crafting))
    {
        if (!recipe->displayInSimpleCrafting() || !recipe->getResultItem())
            continue;
        Item* output = recipe->getResultItem()->getItem();
        if (output == Item::byBlock(Blocks::smoker) && recipe->getGroup() == Recipe::eGroupType_Structures)
            assistedSmoker = true;
        if (output == Items::crossbow && recipe->getGroup() == Recipe::eGroupType_Tools)
            assistedCrossbow = true;
    }
    if (!assistedSmoker || !assistedCrossbow)
        return 3;
    std::puts("PASS: mixed logs, invalid planks, and the crossbow recipe");

    grid->clearContent();
    for (unsigned int slot : {0U, 1U, 2U, 3U, 5U})
        grid->setItem(slot, item(Items::ironIngot));
    grid->setItem(4, item(Item::byBlock(Blocks::furnace)));
    for (unsigned int slot : {6U, 7U, 8U})
        grid->setItem(slot, item(Item::byBlock(Blocks::smooth_stone)));
    if (craft()->getItem() != Item::byBlock(Blocks::blast_furnace))
        return 12;
    grid->setItem(6, item(Item::byBlock(Blocks::stone)));
    if (!craft()->isEmpty())
        return 13;
    auto* recipes = FurnaceRecipes::getInstance();
    auto plainStone = item(Item::byBlock(Blocks::stone));
    if (recipes->getCookingResult(plainStone, FurnaceKind::Furnace)->getItem() != Item::byBlock(Blocks::smooth_stone)
        || !recipes->getCookingResult(plainStone, FurnaceKind::BlastFurnace)->isEmpty()
        || recipes->getCookingResult(item(Items::goldenSword), FurnaceKind::BlastFurnace)->getItem()
            != Items::goldNugget)
        return 14;
    const boost::shared_ptr<Container> remoteBlast(new LocalMenuContainer(ClientboundContainerOpenPacket::BLAST_FURNACE, L"", 3));
    if (FurnaceMenu::getKind(remoteBlast) != FurnaceKind::BlastFurnace)
        return 15;
    std::puts("PASS: blast furnace recipe, smooth stone, gear recycling and multiplayer menu type");

    const auto* face = Direction::EAST;
    if (Block::byId(Block::getId(Blocks::smoker)) != Blocks::smoker
        || Block::byId(Block::getId(Blocks::lit_smoker)) != Blocks::lit_smoker)
        return 4;
    const auto* smokerState = Blocks::smoker->defaultBlockState()->setValue(FurnaceBlock::FACING, face);
    const auto* litState = Blocks::lit_smoker->defaultBlockState()->setValue(FurnaceBlock::FACING, face);
    auto* front = Blocks::smoker->getTexture(face, smokerState);
    auto* frontOn = Blocks::lit_smoker->getTexture(face, litState);
    if (!front || !frontOn || front->getName() != L"smoker_front" || frontOn->getName() != L"smoker_front_on")
        return 4;
    std::puts("PASS: inactive and active front sprites");

    auto* level = new MultiPlayerLevel(nullptr, new LevelSettings(1, GameType::SURVIVAL, false, false, false,
        LevelType::DEFAULT, 54, 3, 128, false, 0, 0, 0), 0, Difficulty::NORMAL, 63);
    CGameNetworkManager::sInstance.Initialise();
    level->setChunkVisible(0, 0, true, false);
    level->mIsClientSide = false;
    const BlockPos smokerPos(8, 64, 8), furnacePos(8, 64, 10);
    level->setBlock(smokerPos, smokerState, Block::UPDATE_CLIENTS, false);
    level->setBlock(furnacePos, Blocks::furnace->defaultBlockState(), Block::UPDATE_CLIENTS, false);
    auto smoker = boost::dynamic_pointer_cast<FurnaceBlockEntity>(level->getBlockEntity(smokerPos));
    auto furnace = boost::dynamic_pointer_cast<FurnaceBlockEntity>(level->getBlockEntity(furnacePos));
    auto ore = item(Item::byBlock(Blocks::iron_ore));
    if (!smoker || !furnace || smoker->canPlaceItemThroughFace(0, ore, Direction::UP)
        || !smoker->canPlaceItemThroughFace(0, item(Items::beef), Direction::UP))
        return 5;
    smoker->setItem(0, ore);
    smoker->setItem(1, item(Items::coal));
    smoker->tick();
    if (smoker->isLit() || smoker->getItem(1)->isEmpty())
        return 6;
    smoker->setItem(0, item(Items::beef));
    furnace->setItem(0, item(Items::beef));
    furnace->setItem(1, item(Items::coal));
    for (int tick = 0; tick < 99; ++tick)
    {
        smoker->tick();
        furnace->tick();
    }
    if (!smoker->getItem(2)->isEmpty() || smoker->getContainerData(1) != 800 || furnace->getContainerData(1) != 1600
        || level->getBlockState(smokerPos) != litState || level->getBlockEntity(smokerPos) != smoker)
        return 7;
    CompoundTag saved;
    smoker->save(&saved);
    auto restored = boost::dynamic_pointer_cast<FurnaceBlockEntity>(BlockEntity::loadStatic(level, &saved));
    if (!restored || restored->getTypeId(false) != ClientboundContainerOpenPacket::SMOKER
        || restored->getContainerData(1) != 800 || restored->getContainerData(2) != 99
        || restored->getItem(0)->getItem() != Items::beef)
        return 8;
    smoker->tick();
    furnace->tick();
    if (smoker->getItem(2)->getItem() != Items::cookedBeef || !furnace->getItem(2)->isEmpty())
        return 9;
    for (int tick = 100; tick < 200; ++tick)
        furnace->tick();
    if (furnace->getItem(2)->getItem() != Items::cookedBeef)
        return 10;
    for (int tick = 100; tick <= 800; ++tick)
        smoker->tick();
    if (level->getBlockState(smokerPos) != smokerState || level->getBlockEntity(smokerPos) != smoker
        || smoker->getItem(2)->getItem() != Items::cookedBeef)
        return 11;
    std::puts("PASS: food-only input, 100/200 ticks, half fuel time, lit state and save round trip");

    const BlockPos blastPos(8, 64, 12);
    const auto* blastState = Blocks::blast_furnace->defaultBlockState()->setValue(FurnaceBlock::FACING, face);
    const auto* litBlastState = Blocks::lit_blast_furnace->defaultBlockState()->setValue(FurnaceBlock::FACING, face);
    const auto* blastFront = Blocks::blast_furnace->getTexture(face, blastState);
    const auto* litBlastFront = Blocks::lit_blast_furnace->getTexture(face, litBlastState);
    if (!blastFront || !litBlastFront || blastFront->getName() != L"blast_furnace_front"
        || litBlastFront->getName() != L"blast_furnace_front_on")
        return 16;
    level->setBlock(blastPos, blastState, Block::UPDATE_CLIENTS, false);
    auto blast = boost::dynamic_pointer_cast<FurnaceBlockEntity>(level->getBlockEntity(blastPos));
    auto freshOre = item(Item::byBlock(Blocks::iron_ore));
    if (!blast || blast->canPlaceItemThroughFace(0, item(Items::beef), Direction::UP)
        || !blast->canPlaceItemThroughFace(0, freshOre, Direction::UP))
        return 17;
    blast->setItem(0, freshOre);
    blast->setItem(1, item(Items::coal));
    for (int tick = 0; tick < 99; ++tick)
        blast->tick();
    if (!blast->getItem(2)->isEmpty() || blast->getContainerData(1) != 800
        || level->getBlockState(blastPos) != litBlastState || level->getBlockEntity(blastPos) != blast)
        return 18;
    CompoundTag savedBlast;
    blast->save(&savedBlast);
    auto restoredBlast = boost::dynamic_pointer_cast<FurnaceBlockEntity>(BlockEntity::loadStatic(level, &savedBlast));
    if (!restoredBlast || restoredBlast->getTypeId(false) != ClientboundContainerOpenPacket::BLAST_FURNACE
        || restoredBlast->getContainerData(1) != 800 || restoredBlast->getContainerData(2) != 99
        || restoredBlast->getItem(0)->getItem() != freshOre->getItem())
        return 19;
    blast->tick();
    if (blast->getItem(2)->getItem() != Items::ironIngot)
        return 20;
    for (int tick = 100; tick <= 800; ++tick)
        blast->tick();
    if (level->getBlockState(blastPos) != blastState || level->getBlockEntity(blastPos) != blast
        || blast->getItem(2)->getItem() != Items::ironIngot)
        return 21;
    std::puts("PASS: blast furnace smelts ore in 100 ticks and preserves its inventory and orientation");
    return 0;
}
