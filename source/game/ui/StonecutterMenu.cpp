#include "game/ui/StonecutterMenu.hpp"

#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/SimpleContainer.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockId.hpp"
#include "game/world/block/DecorativeStone.hpp"
#include "game/world/block/StoneBlock.hpp"
#include "game/world/block/StoneBrickBlock.hpp"
#include "game/world/block/StoneSlabBlock.hpp"
#include "game/world/block/WallBlock.hpp"

namespace
{
class RecipePreviewSlot : public Slot
{
public:
    RecipePreviewSlot(const boost::shared_ptr<Container>& container, int index) : Slot(container, index, 0, 0) {}
    bool mayPlace(not_null_ptr<ItemInstance>) override { return false; }
    bool mayPickup(boost::shared_ptr<Player>) override { return false; }
};

struct CuttingRecipe
{
    Block* material;
    int materialData;
    Block* product;
    int count;
    int productData = 0;
};

const CuttingRecipe* findRecipe(not_null_ptr<ItemInstance> stack, int index)
{
    const auto slab = Blocks::stone_slab;
    using Slab = StoneSlabBlock::Variant;
    using Stone = StoneBlock::Variant;
    const int chiseled = StoneBrickBlock::Variant::CHISELED->getData();
    static const std::vector<CuttingRecipe> recipes = [slab, chiseled] {
        std::vector<CuttingRecipe> result = {
            {Blocks::smooth_stone, 0, slab, 2, Slab::STONE->getData()},
            {Blocks::stone, 0, Blocks::stonebrick, 1},
            {Blocks::stone, 0, Blocks::stone_brick_stairs, 1},
            {Blocks::stone, 0, slab, 2, Slab::SMOOTHBRICK->getData()},
            {Blocks::cobblestone, 0, slab, 2, Slab::COBBLESTONE->getData()},
            {Blocks::cobblestone, 0, Blocks::stone_stairs, 1},
            {Blocks::cobblestone, 0, Blocks::cobblestone_wall, 1},
            {Blocks::mossy_cobblestone, 0, Blocks::cobblestone_wall, 1, WallBlock::Variant::MOSSY->getData()},
            {Blocks::stonebrick, 0, slab, 2, Slab::SMOOTHBRICK->getData()},
            {Blocks::stonebrick, 0, Blocks::stone_brick_stairs, 1},
            {Blocks::stonebrick, 0, Blocks::stonebrick, 1, chiseled},
            {Blocks::stone, Stone::GRANITE->getData(), Blocks::stone, 1, Stone::SMOOTH_GRANITE->getData()},
            {Blocks::stone, Stone::DIORITE->getData(), Blocks::stone, 1, Stone::SMOOTH_DIORITE->getData()},
            {Blocks::stone, Stone::ANDESITE->getData(), Blocks::stone, 1, Stone::SMOOTH_ANDESITE->getData()},
        };
        for (int material = 0; material < DecorativeStone::MaterialCount; ++material)
        {
            const auto& entry = DecorativeStone::materials[material];
            auto* input = Block::byId(entry.blockId);
            if (material < DecorativeStone::StairCount)
            {
                const int group = material / DecorativeStone::SlabTypesPerBlock;
                const int variant = material % DecorativeStone::SlabTypesPerBlock;
                result.push_back({input, entry.data, Block::byId(BlockId::DECOR_SLAB_FIRST + group), 2, variant});
                result.push_back({input, entry.data, Block::byId(BlockId::DECOR_STAIRS_FIRST + material), 1});
            }
            if (entry.wall)
                result.push_back({input, entry.data, Block::byId(BlockId::DECOR_WALL_FIRST + material), 1});
        }
        return result;
    }();
    if (stack->isEmpty())
        return nullptr;
    for (const auto& recipe : recipes)
        if (stack->getItem() == Item::byBlock(recipe.material) && stack->getAuxValue() == recipe.materialData
            && index-- == 0)
            return &recipe;
    return nullptr;
}

not_null_ptr<ItemInstance> recipeProduct(not_null_ptr<ItemInstance> material, int index)
{
    if (const auto* recipe = findRecipe(material, index))
        return not_null_ptr<ItemInstance>(new ItemInstance(recipe->product, recipe->count, recipe->productData));
    return ItemInstance::EMPTY;
}
}

StonecutterMenu::StonecutterMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos)
    : TwoInputMenu(inventory, level, pos, Blocks::stonecutter),
      mRecipePreviews(new SimpleContainer(L"", RecipeSlotCount))
{
    for (int i = 0; i < RecipeSlotCount; ++i)
        addSlot(new RecipePreviewSlot(mRecipePreviews, i));
}

bool StonecutterMenu::accepts(int slot, not_null_ptr<ItemInstance> item)
{
    return slot == 0 && findRecipe(item, 0) != nullptr;
}

bool StonecutterMenu::clickMenuButton(boost::shared_ptr<Player> player, int id)
{
    if (id < 0 || id > NextRecipe || !stillValid(player))
        return false;
    const auto material = input(0);
    if (!findRecipe(material, id == NextRecipe ? 1 : id))
        return false;
    if (!mLevel->mIsClientSide)
    {
        mRecipe = id == NextRecipe ? (findRecipe(material, mRecipe + 1) ? mRecipe + 1 : 0) : id;
        slotsChanged();
    }
    return true;
}

not_null_ptr<ItemInstance> StonecutterMenu::clicked(int slot, int button, ClickType::eClickType type,
                                                  boost::shared_ptr<Player> player)
{
    if (slot >= RecipeSlotStart)
        return ItemInstance::EMPTY;
    return TwoInputMenu::clicked(slot, button, type, player);
}

int StonecutterMenu::getSelectedRecipe()
{
    const auto result = getSlot(2)->getItem();
    if (!result->isEmpty())
        for (int i = 0; i < RecipeSlotCount; ++i)
            if (ItemInstance::isSame(result, mRecipePreviews->getItem(i)))
                return i;
    return -1;
}

not_null_ptr<ItemInstance> StonecutterMenu::makeResult()
{
    const auto stack = input(0);
    if (stack->getItem() != mMaterial || stack->getAuxValue() != mMaterialData)
    {
        mMaterial = stack->getItem();
        mMaterialData = stack->getAuxValue();
        mRecipe = 0;
        for (int i = 0; i < RecipeSlotCount; ++i)
            mRecipePreviews->setItem(i, recipeProduct(stack, i));
    }
    return recipeProduct(stack, mRecipe);
}
