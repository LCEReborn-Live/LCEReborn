#include "game/world/block/LanternBlock.hpp"

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundType.hpp"
#include "game/core/Direction.hpp"
#include "game/util/AABB.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/BooleanProperty.hpp"
#include "game/world/block/Material.hpp"

namespace
{
auto* HANGING = static_cast<AbstractProperty<bool>*>(BooleanProperty::create(L"hanging"));

bool supported(Level* level, const BlockPos& pos, bool hanging)
{
    const auto* direction = hanging ? Direction::UP : Direction::DOWN;
    return level->canProvideSupport(pos.relative(direction), direction->getOpposite(), BLOCK_SUPPORT_CENTER);
}

std::vector<CuboidBlockModel::Part> defaultModel(bool hanging)
{
    using Part = CuboidBlockModel::Part;
    const float base = hanging ? 1.0f : 0.0f;
    Part housing{{{{5, base, 5}, {11, base + 7, 11}}}, {}};
    Part cap{{{{6, base + 7, 6}, {10, base + 9, 10}}}, {}};
    Part loop{{{{6.5f, base + 9, 8}, {9.5f, hanging ? 16.0f : 11.0f, 8}}}, {}, 45};
    for (size_t face = 0; face < housing.faces.size(); ++face)
    {
        housing.faces[face] = {face < 2 ? std::array<float, 4>{0, 9, 6, 15}
                                        : std::array<float, 4>{0, 2, 6, 9}, 0, -1};
        cap.faces[face] = {face < 2 ? std::array<float, 4>{1, 10, 5, 14}
                                    : std::array<float, 4>{1, 0, 5, 2}, 0, -1};
    }
    loop.faces[2] = {{11, 1, 14, hanging ? 7.0f : 3.0f}, 0, -1};
    loop.faces[3] = loop.faces[2];
    return {housing, cap, loop};
}
}

LanternBlock::LanternBlock() : Block(Material::METAL)
{
    static const SoundType sounds(1.0f, 1.0f, SoundEvents::LANTERN_BREAK, SoundEvents::LANTERN_STEP,
                                  SoundEvents::LANTERN_PLACE, SoundEvents::LANTERN_HIT, SoundEvents::LANTERN_FALL);
    setSoundType(&sounds);
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(HANGING, false));
    setLightBlock(0);
    mStanding = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/lantern_model.bin", 1);
    mHanging = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/hanging_lantern_model.bin", 1);
    if (mStanding.empty())
        mStanding = defaultModel(false);
    if (mHanging.empty())
        mHanging = defaultModel(true);
}

EBlockFaceShape LanternBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*)
{
    return BLOCK_FACE_UNDEFINED;
}

const AABB* LanternBlock::getShape(const BlockState* state, LevelSource*, const BlockPos&)
{
    const double y = state->getValue(HANGING) ? 1.0 / 16 : 0.0;
    return AABB::newTemp(5.0 / 16, y, 5.0 / 16, 11.0 / 16, y + 9.0 / 16, 11.0 / 16);
}

bool LanternBlock::mayPlace(Level* level, const BlockPos& pos)
{
    return Block::mayPlace(level, pos) && (supported(level, pos, false) || supported(level, pos, true));
}

bool LanternBlock::canSurvive(Level* level, const BlockPos& pos, const BlockState* state)
{
    return supported(level, pos, state->getValue(HANGING));
}

void LanternBlock::neighborChanged(const BlockState* state, Level* level, const BlockPos& pos, Block*, const BlockPos&)
{
    if (!level->mIsClientSide && !canSurvive(level, pos, state))
        level->destroyBlock(pos, true);
}

const BlockState* LanternBlock::getStateForPlacement(Level* level, const BlockPos& pos, const Direction* face,
                                                    float, float, float, int, boost::shared_ptr<LivingEntity>)
{
    const bool hanging = supported(level, pos, true) && (face == Direction::DOWN || !supported(level, pos, false));
    return defaultBlockState()->setValue(HANGING, hanging);
}

const BlockState* LanternBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(HANGING, (data & 1) != 0);
}

int LanternBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(HANGING) ? 1 : 0;
}

BlockStateDefinition* LanternBlock::createBlockStateDefinition()
{
    const Property* properties[] = {HANGING};
    return new BlockStateDefinition(this, properties, 1);
}

const std::vector<CuboidBlockModel::Part>& LanternBlock::getModel(const BlockState* state) const
{
    return state->getValue(HANGING) ? mHanging : mStanding;
}
