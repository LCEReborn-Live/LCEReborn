#include "game/world/block/BambooBlock.hpp"

#include <algorithm>

#include "game/core/IconRegister.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/Material.hpp"

namespace
{
enum Stage { Shoot, Thin, Thick, ThinSmall, ThickSmall, ThinLarge, ThickLarge };
enum Texture { ShootTexture, StalkTexture, SmallLeavesTexture, LargeLeavesTexture };
auto* STAGE = static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"stage", Shoot, ThickLarge));
constexpr int MaxHeight = 16;

std::vector<CuboidBlockModel::Part> makeModel(int stage)
{
    const bool thick = stage % 2 == 0;
    const float width = thick ? 3.0f : 2.0f;
    const float low = 8.0f - width / 2;
    const float high = 8.0f + width / 2;
    CuboidBlockModel::Part stem{{{{low, 0, low}, {high, 16, high}}}, {}};
    for (auto& face : stem.faces)
        face = {{thick ? 2.0f : 0.0f, 0, thick ? 5.0f : 2.0f, 16}, StalkTexture, -1};
    stem.faces[0] = {{13, 4, 13 + width, 4 + width}, StalkTexture, 0};
    stem.faces[1] = {{13, 0, 13 + width, width}, StalkTexture, 1};
    if (stage < ThinSmall)
        return {stem};

    const float inset = stage >= ThinLarge ? 0.5f : 2.5f;
    const auto texture = stage >= ThinLarge ? LargeLeavesTexture : SmallLeavesTexture;
    CuboidBlockModel::Part eastWest{{{{inset, 0, 8}, {16 - inset, 16, 8}}}, {}, 45};
    CuboidBlockModel::Part northSouth{{{{8, 0, inset}, {8, 16, 16 - inset}}}, {}, 45};
    eastWest.faces[2] = eastWest.faces[3] = {{0, 0, 16, 16}, texture, -1};
    northSouth.faces[4] = northSouth.faces[5] = {{16, 0, 0, 16}, texture, -1};
    return {stem, eastWest, northSouth};
}
}

BambooBlock::BambooBlock() : Bush(Material::PLANT)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(STAGE, int(Shoot)));
    for (int stage = Thin; stage <= ThickLarge; ++stage)
        mModels[stage - Thin] = makeModel(stage);
}

bool BambooBlock::mayPlaceOn(Level*, const BlockPos&, const BlockState* state)
{
    const auto* block = state->getBlock();
    return block == this || block == Blocks::grass || block == Blocks::dirt || block == Blocks::sand
           || block == Blocks::gravel;
}

const BlockState* BambooBlock::getStateForPlacement(Level* level, const BlockPos& pos, const Direction*,
                                                   float, float, float, int, boost::shared_ptr<LivingEntity>)
{
    if (level->getBlockState(pos.below())->getBlock() == this)
        return getBlockState(ThinSmall);
    return defaultBlockState();
}

void BambooBlock::tick(Level* level, const BlockPos& pos, const BlockState* state, Random* random)
{
    if (level->mIsClientSide)
        return;
    Bush::tick(level, pos, state, random);
    if (level->getBlockState(pos) == state && random->nextInt(3) == 0
        && level->getRawBrightness(pos.above()) >= 9 && canGrow(level, pos))
        grow(level, pos);
}

void BambooBlock::neighborChanged(const BlockState* state, Level* level, const BlockPos& pos, Block* block,
                                  const BlockPos& from)
{
    Bush::neighborChanged(state, level, pos, block, from);
    if (!level->mIsClientSide && state->getValue(STAGE) == Shoot
        && level->getBlockState(pos.above())->getBlock() == this)
        level->setBlock(pos, getBlockState(Thin), Block::UPDATE_CLIENTS, false);
}

BlockPos BambooBlock::findTop(Level* level, const BlockPos& pos)
{
    BlockPos top = pos;
    for (int segment = 1; segment < MaxHeight && level->getBlockState(top.above())->getBlock() == this; ++segment)
        top = top.above();
    return top;
}

bool BambooBlock::canGrow(Level* level, const BlockPos& top)
{
    if (level->isOutsideBuildHeight(top.above()) || !level->isEmptyBlock(top.above()))
        return false;
    int height = 1;
    for (BlockPos below = top.below(); height < MaxHeight && below.y >= 0
         && level->getBlockState(below)->getBlock() == this; below = below.below())
        ++height;
    return height < MaxHeight;
}

bool BambooBlock::grow(Level* level, const BlockPos& top)
{
    const auto* state = level->getBlockState(top);
    if (!level->setBlock(top.above(), getBlockState(ThickLarge), Block::UPDATE_ALL, false))
        return false;
    const auto* previous = level->getBlockState(top.below());
    const bool leafy = previous->getBlock() == this && previous->getValue(STAGE) >= ThinSmall;
    if (state->getBlock() == this)
        level->setBlock(top, getBlockState(state->getValue(STAGE) == Shoot ? Thin : ThickSmall),
                        Block::UPDATE_CLIENTS, false);
    if (leafy)
        level->setBlock(top.below(), getBlockState(Thick), Block::UPDATE_CLIENTS, false);
    return true;
}

bool BambooBlock::isValidBonemealTarget(Level* level, const BlockPos& pos, const BlockState*, bool)
{
    return canGrow(level, findTop(level, pos));
}

bool BambooBlock::isBonemealSuccess(Level*, Random*, const BlockPos&, const BlockState*)
{
    return true;
}

void BambooBlock::performBonemeal(Level* level, Random* random, const BlockPos& pos, const BlockState*)
{
    BlockPos top = findTop(level, pos);
    const int count = 1 + random->nextInt(2);
    for (int i = 0; i < count && canGrow(level, top) && grow(level, top); ++i)
        top = top.above();
}

bool BambooBlock::placeStalk(Level* level, const BlockPos& base, int height)
{
    const auto* surface = level->getBlockState(base.below())->getBlock();
    if ((surface != Blocks::grass && surface != Blocks::dirt) || !level->isEmptyBlock(base))
        return false;
    int available = 1;
    while (available < height && !level->isOutsideBuildHeight(base.above(available))
           && level->isEmptyBlock(base.above(available)))
        ++available;
    if (available < 2)
        return false;
    for (int i = 0; i < available; ++i)
    {
        const int stage = i == available - 1 ? ThickLarge : i == available - 2 ? ThickSmall : Thick;
        level->setBlock(base.above(i), getBlockState(stage), Block::UPDATE_CLIENTS, false);
    }
    return true;
}

const AABB* BambooBlock::getShape(const BlockState* state, LevelSource*, const BlockPos&)
{
    if (state->getValue(STAGE) == Shoot)
        return AABB::newTemp(.25, 0, .25, .75, .75, .75);
    return AABB::newTemp(.375, 0, .375, .625, 1, .625);
}

const AABB* BambooBlock::getClipAABB(const BlockState* state, LevelSource*, const BlockPos&)
{
    return state->getValue(STAGE) == Shoot ? nullptr : AABB::newTemp(.40625, 0, .40625, .59375, 1, .59375);
}

EBlockRenderShape BambooBlock::getRenderShape(const BlockState* state)
{
    return state->getValue(STAGE) == Shoot ? BLOCK_RENDER_CROSS : BLOCK_RENDER_BAMBOO;
}

Item* BambooBlock::getResource(const BlockState*, Random*, int)
{
    return Items::bamboo;
}

not_null_ptr<ItemInstance> BambooBlock::getCloneItemInstance(Level*, const BlockPos&, const BlockState*)
{
    return not_null_ptr<ItemInstance>(new ItemInstance(Items::bamboo));
}

const BlockState* BambooBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(STAGE, std::min(std::max(data, int(Shoot)), int(ThickLarge)));
}

int BambooBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(STAGE);
}

BlockStateDefinition* BambooBlock::createBlockStateDefinition()
{
    const Property* properties[] = {STAGE};
    return new BlockStateDefinition(this, properties, 1);
}

TextureAtlasSprite* BambooBlock::getTexture(const Direction*, const BlockState* state)
{
    return mTextures[state->getValue(STAGE) == Shoot ? ShootTexture : StalkTexture];
}

void BambooBlock::registerIcons(IconRegister* icons)
{
    mTextures = {icons->registerIcon(L"bamboo_shoot"), icons->registerIcon(L"bamboo_stalk"),
                 icons->registerIcon(L"bamboo_leaf"), icons->registerIcon(L"bamboo_large_leaves")};
    mIcon = mTextures[ShootTexture];
}

const std::vector<CuboidBlockModel::Part>& BambooBlock::getModel(const BlockState* state) const
{
    return mModels[std::max(int(Thin), state->getValue(STAGE)) - Thin];
}
