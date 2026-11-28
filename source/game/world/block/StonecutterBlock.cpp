#include "game/world/block/StonecutterBlock.hpp"

#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/core/StringIds.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/BlockTooltipDataHolder.hpp"
#include "game/ui/StonecutterMenu.hpp"
#include "game/util/AABB.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/LocalBlockInteraction.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
class StonecutterInteraction : public LocalBlockInteraction
{
public:
    StonecutterInteraction(Level* level, const BlockPos& pos)
        : LocalBlockInteraction(ClientboundContainerOpenPacket::STONECUTTER, L""), mLevel(level), mPos(pos) {}

    AbstractContainerMenu* createMenu(boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Player>) override
    {
        return new StonecutterMenu(inventory, mLevel, mPos);
    }

private:
    Level* mLevel;
    BlockPos mPos;
};
}

StonecutterBlock::StonecutterBlock() : HorizontalDirectionalBlock(Material::STONE)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(FACING, Direction::NORTH));
    setLightBlock(0);
    mModel = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/stonecutter_model.bin", mTextures.size());
    mImportedModel = !mModel.empty();
    if (!mImportedModel)
    {
        mModel = {
            {{{ {0, 0, 0}, {16, 7, 16} }}, {}},
            {{{ {0, 7, 0}, {16, 9, 16} }}, {}},
            {{{ {3, 9, 7}, {13, 14, 9} }}, {}},
        };
        for (auto& face : mModel[0].faces)
            face = {{0, 0, 16, 7}, 2, -1};
        for (auto& face : mModel[1].faces)
            face = {{0, 0, 16, 16}, 1, -1};
        for (auto& face : mModel[2].faces)
            face = {{2, 2, 14, 8}, 3, -1};
    }
}

bool StonecutterBlock::use(Level* level, const BlockPos& pos, const BlockState*, boost::shared_ptr<Player> player,
                          InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide)
{
    if (clientSide)
        return false;
    if (!level->mIsClientSide)
    {
        StonecutterInteraction interaction(level, pos);
        player->startBlockInteraction(interaction);
    }
    return true;
}

unsigned int StonecutterBlock::GetInteractTooltip(const BlockTooltipDataHolder& data)
{
    return (data.m_bUseBlock || data.m_bUseItemOnBlock) && data.m_bCanPlace
        ? IDS_TOOLTIPS_USE : Block::GetInteractTooltip(data);
}

const AABB* StonecutterBlock::getShape(const BlockState*, LevelSource*, const BlockPos&)
{
    return AABB::newTemp(0, 0, 0, 1, 9.0 / 16, 1);
}

EBlockFaceShape StonecutterBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&,
                                                  const Direction* face)
{
    return face == Direction::DOWN ? BLOCK_FACE_SOLID : BLOCK_FACE_UNDEFINED;
}

const BlockState* StonecutterBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(FACING, Direction::from2DDataValue(data & 3));
}

int StonecutterBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(FACING)->get2DDataValue();
}

const BlockState* StonecutterBlock::getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float,
                                                       float, int, boost::shared_ptr<LivingEntity> placer)
{
    return defaultBlockState()->setValue(FACING, placer->getDirection()->getOpposite());
}

const BlockState* StonecutterBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(FACING, rotation->rotate(state->getValue(FACING)));
}

const BlockState* StonecutterBlock::mirror(const BlockState* state, Mirror* mirror)
{
    return rotate(state, mirror->getRotation(state->getValue(FACING)));
}

BlockStateDefinition* StonecutterBlock::createBlockStateDefinition()
{
    const Property* properties[] = {FACING};
    return new BlockStateDefinition(this, properties, 1);
}

void StonecutterBlock::registerIcons(IconRegister* icons)
{
    const wchar_t* imported[] = {L"stonecutter_bottom", L"stonecutter_top", L"stonecutter_side", L"stonecutter_saw"};
    const wchar_t* fallback[] = {L"stonebrick", L"stoneslab_top", L"stonebrick", L"iron_block"};
    for (size_t i = 0; i < mTextures.size(); ++i)
        mTextures[i] = icons->registerIcon(mImportedModel ? imported[i] : fallback[i]);
    mIcon = mTextures[2];
}

CuboidBlockModel::Point StonecutterBlock::orientPoint(const BlockState* state, CuboidBlockModel::Point point)
{
    const auto* facing = state->getValue(FACING);
    if (facing == Direction::WEST || facing == Direction::EAST)
        return {point[2], point[1], 16 - point[0]};
    return point;
}
