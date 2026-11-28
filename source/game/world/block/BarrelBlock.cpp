#include "game/world/block/BarrelBlock.hpp"

#include <cmath>

#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/core/StringIds.hpp"
#include "game/entity/mob/LivingEntity.hpp"
#include "game/item/Containers.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/ui/AbstractContainerMenu.hpp"
#include "game/ui/BlockTooltipDataHolder.hpp"
#include "game/util/Vec3.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BarrelBlockEntity.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/BooleanProperty.hpp"
#include "game/world/block/DirectionalBlock.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

DirectionProperty* BarrelBlock::FACING = nullptr;
AbstractProperty<bool>* BarrelBlock::OPEN = static_cast<AbstractProperty<bool>*>(BooleanProperty::create(L"open"));

BarrelBlock::BarrelBlock() : BaseEntityBlock(Material::WOOD)
{
    FACING = DirectionalBlock::FACING_PROPERTY;
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(FACING, Direction::UP)->setValue(OPEN, false));
}

boost::shared_ptr<BlockEntity> BarrelBlock::newBlockEntity(Level*, int)
{
    return boost::shared_ptr<BlockEntity>(new BarrelBlockEntity());
}

bool BarrelBlock::use(Level* level, const BlockPos& pos, const BlockState*, boost::shared_ptr<Player> player,
                      InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide)
{
    if (!clientSide && !level->mIsClientSide)
        if (auto barrel = boost::dynamic_pointer_cast<BarrelBlockEntity>(level->getBlockEntity(pos)))
            player->openContainer(barrel);
    return true;
}

unsigned int BarrelBlock::GetInteractTooltip(const BlockTooltipDataHolder& data)
{
    if ((data.m_bUseBlock || data.m_bUseItemOnBlock) && data.m_bCanPlace)
        return IDS_TOOLTIPS_OPEN;
    return Block::GetInteractTooltip(data);
}

const BlockState* BarrelBlock::getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                                   int, boost::shared_ptr<LivingEntity> placer)
{
    const auto* look = placer->getLookAngle();
    const auto* facing = placer->getDirection()->getOpposite();
    if (std::abs(look->y) > std::abs(look->x) && std::abs(look->y) > std::abs(look->z))
        facing = look->y > 0 ? Direction::DOWN : Direction::UP;
    return defaultBlockState()->setValue(FACING, facing);
}

void BarrelBlock::setPlacedBy(Level* level, const BlockPos& pos, const BlockState*, boost::shared_ptr<LivingEntity>,
                              not_null_ptr<ItemInstance> item)
{
    if (item->hasCustomHoverName())
        if (auto barrel = boost::dynamic_pointer_cast<BarrelBlockEntity>(level->getBlockEntity(pos)))
            barrel->setCustomName(item->getHoverNameUnfiltered(), item->getOwnerUUID(),
                                  item->wasCreatedByRestrictedPlayer(), item->wasCreatedOnHost());
}

void BarrelBlock::onRemove(Level* level, const BlockPos& pos, const BlockState* state)
{
    if (!level->mIsClientSide)
        if (auto barrel = boost::dynamic_pointer_cast<BarrelBlockEntity>(level->getBlockEntity(pos)))
        {
            Containers::dropContents(level, pos, barrel);
            level->updateNeighbourForOutputSignal(pos, this);
        }
    BaseEntityBlock::onRemove(level, pos, state);
}

int BarrelBlock::getAnalogOutputSignal(const BlockState*, Level* level, const BlockPos& pos)
{
    return AbstractContainerMenu::getRedstoneSignalFromBlockEntity(level->getBlockEntity(pos));
}

const BlockState* BarrelBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(FACING, Direction::from3DDataValue((data & 7) % 6))
        ->setValue(OPEN, (data & 8) != 0);
}

int BarrelBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(FACING)->get3DDataValue() | (state->getValue(OPEN) ? 8 : 0);
}

const BlockState* BarrelBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(FACING, rotation->rotate(state->getValue(FACING)));
}

const BlockState* BarrelBlock::mirror(const BlockState* state, Mirror* mirror)
{
    return rotate(state, mirror->getRotation(state->getValue(FACING)));
}

BlockStateDefinition* BarrelBlock::createBlockStateDefinition()
{
    const Property* properties[] = {FACING, OPEN};
    return new BlockStateDefinition(this, properties, 2);
}

TextureAtlasSprite* BarrelBlock::getTexture(const Direction* direction, const BlockState* state)
{
    const auto* facing = state->getValue(FACING);
    if (direction == facing)
        return state->getValue(OPEN) ? mOpenLid : mLid;
    return direction == facing->getOpposite() ? mBottom : mIcon;
}

void BarrelBlock::registerIcons(IconRegister* icons)
{
    mIcon = icons->registerIcon(L"barrel_side");
    mLid = icons->registerIcon(L"barrel_top");
    mOpenLid = icons->registerIcon(L"barrel_top_open");
    mBottom = icons->registerIcon(L"barrel_bottom");
}
