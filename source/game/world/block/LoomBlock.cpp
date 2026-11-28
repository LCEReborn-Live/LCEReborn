#include "game/world/block/LoomBlock.hpp"

#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/core/StringIds.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/BlockTooltipDataHolder.hpp"
#include "game/ui/LoomMenu.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/LocalBlockInteraction.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
class LoomInteraction : public LocalBlockInteraction
{
public:
    LoomInteraction(Level* level, const BlockPos& pos)
        : LocalBlockInteraction(ClientboundContainerOpenPacket::LOOM, L""), mLevel(level), mPos(pos) {}

    AbstractContainerMenu* createMenu(boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Player>) override
    {
        return new LoomMenu(inventory, mLevel, mPos);
    }

private:
    Level* mLevel;
    BlockPos mPos;
};
}

LoomBlock::LoomBlock() : HorizontalDirectionalBlock(Material::WOOD)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(FACING, Direction::NORTH));
}

bool LoomBlock::use(Level* level, const BlockPos& pos, const BlockState*, boost::shared_ptr<Player> player,
                    InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide)
{
    if (clientSide)
        return false;
    if (!level->mIsClientSide)
    {
        LoomInteraction interaction(level, pos);
        player->startBlockInteraction(interaction);
    }
    return true;
}

unsigned int LoomBlock::GetInteractTooltip(const BlockTooltipDataHolder& data)
{
    return (data.m_bUseBlock || data.m_bUseItemOnBlock) && data.m_bCanPlace
        ? IDS_TOOLTIPS_USE : Block::GetInteractTooltip(data);
}

const BlockState* LoomBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(FACING, Direction::from2DDataValue(data & 3));
}

int LoomBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(FACING)->get2DDataValue();
}

const BlockState* LoomBlock::getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                                 int, boost::shared_ptr<LivingEntity> placer)
{
    return defaultBlockState()->setValue(FACING, placer->getDirection()->getOpposite());
}

const BlockState* LoomBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(FACING, rotation->rotate(state->getValue(FACING)));
}

const BlockState* LoomBlock::mirror(const BlockState* state, Mirror* mirror)
{
    return rotate(state, mirror->getRotation(state->getValue(FACING)));
}

BlockStateDefinition* LoomBlock::createBlockStateDefinition()
{
    const Property* properties[] = {FACING};
    return new BlockStateDefinition(this, properties, 1);
}

void LoomBlock::registerIcons(IconRegister* icons)
{
    mTextures = {icons->registerIcon(L"loom_side"), icons->registerIcon(L"loom_front"),
                 icons->registerIcon(L"loom_top"), icons->registerIcon(L"loom_bottom")};
    mIcon = mTextures[0];
}

TextureAtlasSprite* LoomBlock::getTexture(const Direction* direction, const BlockState* state)
{
    if (direction == Direction::UP)
        return mTextures[2];
    if (direction == Direction::DOWN)
        return mTextures[3];
    return mTextures[direction == (state ? state : defaultBlockState())->getValue(FACING) ? 1 : 0];
}
