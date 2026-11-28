#include "game/world/block/DecorativeStone.hpp"

#include "game/client/audio/SoundType.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/StringIds.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/world/block/BlockId.hpp"
#include "game/world/block/BlockImpl.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/HalfSlabBlock.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/block/StairBlock.hpp"
#include "game/world/block/TypedBoxed.hpp"
#include "game/world/block/WallBlock.hpp"

namespace DecorativeStone
{
const MaterialEntry materials[MaterialCount] = {
    {BlockId::STONE, 0, L"normal_stone", L"Stone", L"камня", false},
    {BlockId::STONE, 1, L"granite", L"Granite", L"гранита", true},
    {BlockId::STONE, 2, L"polished_granite", L"Polished Granite", L"полированного гранита", false},
    {BlockId::STONE, 3, L"diorite", L"Diorite", L"диорита", true},
    {BlockId::STONE, 4, L"polished_diorite", L"Polished Diorite", L"полированного диорита", false},
    {BlockId::STONE, 5, L"andesite", L"Andesite", L"андезита", true},
    {BlockId::STONE, 6, L"polished_andesite", L"Polished Andesite", L"полированного андезита", false},
    {BlockId::SMOOTH_SANDSTONE, 0, L"smooth_sandstone", L"Smooth Sandstone", L"гладкого песчаника", false},
    {BlockId::SMOOTH_RED_SANDSTONE, 0, L"smooth_red_sandstone", L"Smooth Red Sandstone", L"гладкого красного песчаника", false},
    {BlockId::SMOOTH_QUARTZ, 0, L"smooth_quartz", L"Smooth Quartz", L"гладкого кварца", false},
    {BlockId::STONEBRICK, 1, L"mossy_stone_brick", L"Mossy Stone Brick", L"замшелого каменного кирпича", true},
    {BlockId::MOSSY_COBBLESTONE, 0, L"mossy_cobblestone", L"Mossy Cobblestone", L"замшелого булыжника", false},
    {BlockId::RED_NETHER_BRICK, 0, L"red_nether_brick", L"Red Nether Brick", L"красного незерского кирпича", true},
    {BlockId::END_BRICKS, 0, L"end_stone_brick", L"End Stone Brick", L"эндернякового кирпича", true},
    {BlockId::SANDSTONE, 0, L"sandstone", L"Sandstone", L"песчаника", true},
    {BlockId::RED_SANDSTONE, 0, L"red_sandstone", L"Red Sandstone", L"красного песчаника", true},
    {BlockId::BRICK_BLOCK, 0, L"brick", L"Brick", L"кирпича", true},
    {BlockId::STONEBRICK, 0, L"stone_brick", L"Stone Brick", L"каменного кирпича", true},
    {BlockId::NETHER_BRICK, 0, L"nether_brick", L"Nether Brick", L"незерского кирпича", true},
    {BlockId::PRISMARINE, 0, L"prismarine", L"Prismarine", L"призмарина", true},
};

namespace
{
const BlockState* sourceState(const MaterialEntry& material)
{
    return Block::byId(material.blockId)->getBlockState(material.data);
}

class MaterialWall : public WallBlock
{
public:
    explicit MaterialWall(const BlockState* source) : WallBlock(source->getBlock()), mSource(source) {}
    TextureAtlasSprite* getTexture(const Direction* face, const BlockState*) override
    {
        return mSource->getBlock()->getTexture(face, mSource);
    }
    std::wstring getName() override { return Block::getName(); }

private:
    const BlockState* mSource;
};

auto* VARIANT = static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"variant", 0, SlabTypesPerBlock - 1));

int slabVariant(int data)
{
    const int variant = data & 7;
    return variant < SlabTypesPerBlock ? variant : 0;
}

class MaterialSlab : public HalfSlabBlock
{
public:
    MaterialSlab(int group, bool full) : HalfSlabBlock(Material::STONE), mGroup(group), mFull(full)
    {
        DerivedInit();
        const auto* state = mStateDefinition->any()->setValue(VARIANT, 0);
        registerDefaultState(mFull ? state : state->setValue(TYPE, BOTTOM));
    }

    bool isFullSize() const override { return mFull; }
    int getAuxName(int data) override { return IDS_LCE_DECOR_STONE_FIRST + (mGroup * SlabTypesPerBlock + slabVariant(data)) * 3 + 1; }
    Property* getTypeProperty() override { return VARIANT; }
    Boxed* getTypeForItem(not_null_ptr<ItemInstance> item) override
    {
        return new TypedBoxed<int>(slabVariant(item->getAuxValue()));
    }
    BlockStateDefinition* createBlockStateDefinition() override
    {
        const Property* properties[] = {VARIANT, TYPE};
        return new BlockStateDefinition(this, properties, mFull ? 1 : 2);
    }
    const BlockState* getBlockState(int data) override
    {
        const auto* state = defaultBlockState()->setValue(VARIANT, slabVariant(data));
        return mFull ? state : state->setValue(TYPE, (data & 8) ? TOP : BOTTOM);
    }
    int convertBlockStateToLegacyData(const BlockState* state) override
    {
        return state->getValue(VARIANT) | (!mFull && state->getValue(TYPE) == TOP ? 8 : 0);
    }
    int getSpawnResourcesAuxValue(const BlockState* state) override { return state->getValue(VARIANT); }
    Item* getResource(const BlockState*, Random*, int) override
    {
        return Item::byBlock(Block::byId(BlockId::DECOR_SLAB_FIRST + mGroup));
    }
    not_null_ptr<ItemInstance> getCloneItemInstance(Level*, const BlockPos&, const BlockState* state) override
    {
        return not_null_ptr<ItemInstance>(new ItemInstance(getResource(state, nullptr, 0), 1, state->getValue(VARIANT)));
    }
    TextureAtlasSprite* getTexture(const Direction* face, const BlockState* state) override
    {
        const auto* source = sourceState(materials[mGroup * SlabTypesPerBlock + state->getValue(VARIANT)]);
        return source->getBlock()->getTexture(face, source);
    }

private:
    const int mGroup;
    const bool mFull;
};

void addSmoothBlock(int blockId, const wchar_t* name, const wchar_t* texture, int nameId)
{
    Block::registerBlock(blockId, name,
        (new BlockImpl(Material::STONE))->setDestroyTime(2.0f)->setExplodeable(6.0f)
            ->setSoundType(SoundType::STONE)->setIconName(texture)->setNameAndDescriptionId(nameId, IDS_DESC_STONE));
}
}

void registerBlocks()
{
    addSmoothBlock(BlockId::SMOOTH_SANDSTONE, L"smooth_sandstone", L"sandstone_top", IDS_LCE_SMOOTH_SANDSTONE);
    addSmoothBlock(BlockId::SMOOTH_RED_SANDSTONE, L"smooth_red_sandstone", L"red_sandstone_top", IDS_LCE_SMOOTH_RED_SANDSTONE);
    addSmoothBlock(BlockId::SMOOTH_QUARTZ, L"smooth_quartz", L"quartz_block_bottom", IDS_LCE_SMOOTH_QUARTZ);
    for (int material = 0; material < MaterialCount; ++material)
    {
        const auto& entry = materials[material];
        const std::wstring key = entry.id;
        const int title = IDS_LCE_DECOR_STONE_FIRST + material * 3;
        if (material < StairCount)
            Block::registerBlock(BlockId::DECOR_STAIRS_FIRST + material, key + L"_stairs",
                (new StairBlock(sourceState(entry)))->sendBlockData(15)->setNameAndDescriptionId(title, IDS_DESC_STONE));
        if (entry.wall)
            Block::registerBlock(BlockId::DECOR_WALL_FIRST + material, key + L"_wall",
                (new MaterialWall(sourceState(entry)))->setNameAndDescriptionId(title + 2, IDS_DESC_STONE));
    }
    for (int group = 0; group < StairCount / SlabTypesPerBlock; ++group)
    {
        const auto suffix = std::to_wstring(group + 1);
        Block::registerBlock(BlockId::DECOR_SLAB_FIRST + group, L"decorative_slab_" + suffix,
            (new MaterialSlab(group, false))->setDestroyTime(2.0f)->setExplodeable(6.0f)->setSoundType(SoundType::STONE)
                ->sendBlockData(15)->setNameAndDescriptionId(IDS_LCE_DECOR_STONE_FIRST + group * SlabTypesPerBlock * 3 + 1, IDS_DESC_STONE));
        Block::registerBlock(BlockId::DECOR_DOUBLE_SLAB_FIRST + group, L"decorative_double_slab_" + suffix,
            (new MaterialSlab(group, true))->setDestroyTime(2.0f)->setExplodeable(6.0f)->setSoundType(SoundType::STONE)
                ->sendBlockData(7)->setNameAndDescriptionId(IDS_LCE_DECOR_STONE_FIRST + group * SlabTypesPerBlock * 3 + 1, IDS_DESC_STONE));
    }
}
}
