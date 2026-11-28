#include "game/world/block/ScaffoldingBlock.hpp"

#include <algorithm>

#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/world/Level.hpp"
#include "game/world/WorldBorder.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"
#include "game/util/AABB.hpp"

namespace
{
constexpr int Unsupported = 7;
constexpr int BaseFlag = 8;
auto* DISTANCE = static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"support", 0, 15));
enum Texture { Top, Side, Bottom };

CuboidBlockModel::Part frame(const CuboidBlockModel::Point& low, const CuboidBlockModel::Point& high)
{
    CuboidBlockModel::Part part{{low, high}, {}};
    part.faces[0] = {{low[0], low[2], high[0], high[2]}, Bottom, -1};
    for (int i = 2; i < 6; ++i)
    {
        const int axis = i < 4 ? 0 : 2;
        part.faces[i] = {{low[axis], 16 - high[1], high[axis], 16 - low[1]}, Side, -1};
    }
    return part;
}
}

ScaffoldingBlock::ScaffoldingBlock() : FallingBlock(Material::BAMBOO)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(DISTANCE, 0));

    CuboidBlockModel::Part mesh{{{{0, 16, 0}, {16, 16, 16}}}, {}};
    mesh.faces[0] = mesh.faces[1] = {{0, 0, 16, 16}, Top, -1};
    mStable = {mesh, frame({0, 0, 0}, {2, 16, 2}), frame({14, 0, 0}, {16, 16, 2}),
              frame({0, 0, 14}, {2, 16, 16}), frame({14, 0, 14}, {16, 16, 16}),
              frame({2, 14, 0}, {14, 16, 2}), frame({2, 14, 14}, {14, 16, 16}),
              frame({0, 14, 2}, {2, 16, 14}), frame({14, 14, 2}, {16, 16, 14})};
    mBottom = mStable;
    CuboidBlockModel::Part brace{{{{0, 0, 0}, {16, 0, 16}}}, {}};
    brace.faces[0] = brace.faces[1] = {{0, 0, 16, 16}, Bottom, -1};
    mBottom.push_back(brace);
}

int ScaffoldingBlock::supportDistance(Level* level, const BlockPos& pos)
{
    const auto* below = level->getBlockState(pos.below());
    int distance = Unsupported;
    if (below->getBlock() == this)
        distance = below->getValue(DISTANCE) & Unsupported;
    else if (below->getBlock() == Blocks::glass || below->getBlock() == Blocks::stained_glass
             || level->canProvideSupport(pos.below(), Direction::UP, BLOCK_SUPPORT_CENTER))
        return 0;

    const Direction* directions[] = {Direction::NORTH, Direction::EAST, Direction::SOUTH, Direction::WEST};
    for (const auto* direction : directions)
    {
        const auto* neighbor = level->getBlockState(pos.relative(direction));
        if (neighbor->getBlock() == this)
            distance = std::min(distance, (neighbor->getValue(DISTANCE) & Unsupported) + 1);
    }
    return distance;
}

const BlockState* ScaffoldingBlock::stateForPosition(Level* level, const BlockPos& pos)
{
    const int distance = supportDistance(level, pos);
    const bool bottom = distance > 0 && level->getBlockState(pos.below())->getBlock() != this;
    return getBlockState(distance | (bottom ? BaseFlag : 0));
}

const BlockState* ScaffoldingBlock::getStateForPlacement(Level* level, const BlockPos& pos, const Direction*,
                                                        float, float, float, int, boost::shared_ptr<LivingEntity>)
{
    return stateForPosition(level, pos);
}

void ScaffoldingBlock::tick(Level* level, const BlockPos& pos, const BlockState* state, Random*)
{
    if (level->mIsClientSide)
        return;
    const auto* updated = stateForPosition(level, pos);
    if ((updated->getValue(DISTANCE) & Unsupported) == Unsupported)
    {
        if ((state->getValue(DISTANCE) & Unsupported) == Unsupported && isFree(level->getBlockState(pos.below())))
            checkSlide(level, pos);
        else
            level->destroyBlock(pos, true);
    }
    else if (updated != state)
        level->setBlock(pos, updated, Block::UPDATE_ALL, false);
}

void ScaffoldingBlock::onLand(Level* level, const BlockPos& pos, const BlockState*, const BlockState*)
{
    level->setBlock(pos, stateForPosition(level, pos), Block::UPDATE_ALL, false);
}

bool ScaffoldingBlock::canSurvive(Level* level, const BlockPos& pos, const BlockState*)
{
    return supportDistance(level, pos) < Unsupported;
}

EBlockFaceShape ScaffoldingBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&,
                                                   const Direction*)
{
    return BLOCK_FACE_UNDEFINED;
}

bool ScaffoldingBlock::canProvideSupport(const BlockState*, const Direction* direction, EBlockSupportType type)
{
    return direction == Direction::UP && type == BLOCK_SUPPORT_CENTER;
}

void ScaffoldingBlock::addCollisionAABBs(const BlockState*, Level*, const BlockPos& pos, const AABB* query,
                                         std::vector<AABB*>* boxes, boost::shared_ptr<Entity> entity, bool)
{
    if (entity && (entity->isSneaking() || entity->mBoundingBox->mMinY < pos.y + 1.0 - 0.00001))
        return;
    addCollisionAABB(pos, query, boxes, AABB::newTemp(0, 0.875, 0, 1, 1, 1));
}

const BlockState* ScaffoldingBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(DISTANCE, data & 15);
}

int ScaffoldingBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(DISTANCE);
}

BlockStateDefinition* ScaffoldingBlock::createBlockStateDefinition()
{
    const Property* properties[] = {DISTANCE};
    return new BlockStateDefinition(this, properties, 1);
}

void ScaffoldingBlock::registerIcons(IconRegister* icons)
{
    mTextures = {icons->registerIcon(L"scaffolding_top"), icons->registerIcon(L"scaffolding_side"),
                 icons->registerIcon(L"scaffolding_bottom")};
    mIcon = mTextures[Top];
}

TextureAtlasSprite* ScaffoldingBlock::getTexture(const Direction* face, const BlockState*)
{
    return mTextures[face == Direction::UP ? Top : face == Direction::DOWN ? Bottom : Side];
}

const std::vector<CuboidBlockModel::Part>& ScaffoldingBlock::getModel(const BlockState* state) const
{
    return state->getValue(DISTANCE) & BaseFlag ? mBottom : mStable;
}

bool ScaffoldingItem::findExtension(Level* level, const BlockPos& pos, const Direction* face, Player& player,
                                     BlockPos& target, const Direction*& extend)
{
    extend = player.isSneaking() ? face : Direction::UP;
    const int limit = extend->getStepY() == 0 ? Unsupported : level->getMaxBuildHeight();
    target = pos.relative(extend);
    for (int i = 0; i < limit && !level->isOutsideBuildHeight(target); ++i, target = target.relative(extend))
    {
        auto* existing = level->getBlockState(target)->getBlock();
        if (existing == mBlock)
            continue;
        return existing->mayReplaceWithPlace(level, target) && level->getWorldBorder()->isWithinBounds(target);
    }
    return false;
}

bool ScaffoldingItem::mayPlace(Level* level, const BlockPos& pos, const Direction* face,
                               boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> item)
{
    if (level->getBlockState(pos)->getBlock() != mBlock)
        return BlockItem::mayPlace(level, pos, face, player, item);

    BlockPos target;
    const Direction* extend;
    return findExtension(level, pos, face, *player, target, extend)
        && BlockItem::mayPlace(level, target, extend, player, item);
}

InteractionResult::EInteractionResult ScaffoldingItem::useOn(
    boost::shared_ptr<Player> player, Level* level, const BlockPos& pos, InteractionHand::EInteractionHand hand,
    const Direction* face, float hitX, float hitY, float hitZ, bool simulate)
{
    if (level->getBlockState(pos)->getBlock() != mBlock)
        return BlockItem::useOn(player, level, pos, hand, face, hitX, hitY, hitZ, simulate);

    BlockPos target;
    const Direction* extend;
    if (!findExtension(level, pos, face, *player, target, extend))
        return InteractionResult::eInteractionResult_Fail;
    return BlockItem::useOn(player, level, target, hand, extend, hitX, hitY, hitZ, simulate);
}
