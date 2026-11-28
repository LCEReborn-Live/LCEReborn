#include "game/world/block/FurnaceBlock.hpp"
#include "game/core/StringIds.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/client/render/ParticleType.hpp"

#include "game/core/Boxed.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/block/TypedBoxed.hpp"
#include "game/item/Containers.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/ui/AbstractContainerMenu.hpp"
#include "game/ui/BlockTooltipDataHolder.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionalBlock.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/FurnaceBlockEntity.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"
#include "game/world/block/Material.hpp"

DirectionProperty* FurnaceBlock::FACING = NULL;

bool FurnaceBlock::sKeepInventory = false;

namespace
{
Block* furnaceBlock(FurnaceKind kind, bool lit)
{
    switch (kind)
    {
    case FurnaceKind::Smoker: return lit ? Blocks::lit_smoker : Blocks::smoker;
    case FurnaceKind::BlastFurnace: return lit ? Blocks::lit_blast_furnace : Blocks::blast_furnace;
    default: return lit ? Blocks::lit_furnace : Blocks::furnace;
    }
}
}

FurnaceBlock::FurnaceBlock(bool lit, FurnaceKind kind) : BaseEntityBlock(Material::STONE), mLit(lit), mKind(kind)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any());
}

boost::shared_ptr<BlockEntity> FurnaceBlock::newBlockEntity(Level* level, int)
{
    boost::shared_ptr<BlockEntity> blockEntity(new FurnaceBlockEntity(mKind));
    blockEntity->setLevel(level);
    return blockEntity;
}

void FurnaceBlock::blockStaticCtor()
{
    FACING = HorizontalDirectionalBlock::FACING_PROPERTY;
}

Item* FurnaceBlock::getResource(const BlockState*, Random*, int)
{
    return Item::byBlock(furnaceBlock(mKind, false));
}

void FurnaceBlock::recalcLockDir(Level* level, const BlockPos& pos, const BlockState* state)
{
    if (level->mIsClientSide)
        return;
    const BlockState* north = level->getBlockState(pos.north());
    const BlockState* south = level->getBlockState(pos.south());
    const BlockState* west = level->getBlockState(pos.west());
    const BlockState* east = level->getBlockState(pos.east());
    const Direction* facing = state->getValue(FACING);
    if (facing == Direction::NORTH && north->isSolid() && !south->isSolid())
        facing = Direction::SOUTH;
    else if (facing == Direction::SOUTH && south->isSolid() && !north->isSolid())
        facing = Direction::NORTH;
    else if (facing == Direction::WEST && west->isSolid() && !east->isSolid())
        facing = Direction::EAST;
    else if (facing == Direction::EAST && east->isSolid() && !west->isSolid())
        facing = Direction::WEST;
    level->setBlock(pos, state->setValue(FACING, facing), Block::UPDATE_CLIENTS, false);
}

void FurnaceBlock::onPlace(Level* level, const BlockPos& pos, const BlockState* state)
{
    Block::onPlace(level, pos, state);
    recalcLockDir(level, pos, state);
}

void FurnaceBlock::animateTick(const BlockState* state, Level* level, const BlockPos& pos, Random* random)
{
    if (!mLit)
        return;
    if (mKind == FurnaceKind::Smoker)
    {
        if (random->nextDouble() < 0.1)
            level->playLocalSound(pos.x + 0.5, pos.y, pos.z + 0.5, SoundEvents::SMOKER_SMOKE,
                                  SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, false, 16.0f);
        level->addParticle(PARTICLE_SMOKE, pos.x + 0.5, pos.y + 1.1, pos.z + 0.5, 0.0, 0.0, 0.0,
                           arrayWithLength<int>());
        return;
    }
    const Direction* facing = state->getValue(FACING);
    const double x = pos.x + 0.5;
    const double y = pos.y + random->nextDouble() * 6.0 / 16.0;
    const double z = pos.z + 0.5;
    const double offset = 0.52;
    const double side = random->nextDouble() * 0.6 - 0.3;
    if (random->nextDouble() < 0.1)
    {
        const SoundEvent* sound = mKind == FurnaceKind::BlastFurnace ? SoundEvents::BLAST_FURNACE_FIRE_CRACKLE
                                                                   : SoundEvents::FURNACE_FIRE_CRACKLE;
        level->playLocalSound(pos.x + 0.5, pos.y, pos.z + 0.5, sound,
                              SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, false, 16.0f);
    }
    const bool eastWest = facing->getAxis() == Direction::Axis::X;
    const double frontX = x + offset * facing->getStepX() + (eastWest ? 0.0 : side);
    const double frontZ = z + offset * facing->getStepZ() + (eastWest ? side : 0.0);
    level->addParticle(PARTICLE_SMOKE, frontX, y, frontZ, 0.0, 0.0, 0.0, arrayWithLength<int>());
    if (mKind == FurnaceKind::Furnace)
        level->addParticle(PARTICLE_FLAME, frontX, y, frontZ, 0.0, 0.0, 0.0, arrayWithLength<int>());
}

bool FurnaceBlock::TestUse()
{
    return true;
}

bool FurnaceBlock::use(Level* level, const BlockPos& pos, const BlockState* state, boost::shared_ptr<Player> player,
                       InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide)
{
    if (clientSide)
        return false;
    if (!level->mIsClientSide)
    {
        boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(pos);
        bool isFurnace = blockEntity && eTYPE_DERIVED_FROM(BlockEntityType::FURNACE, blockEntity->GetType());
        if (isFurnace)
            player->openContainer(boost::static_pointer_cast<FurnaceBlockEntity>(blockEntity));
    }
    return true;
}

void FurnaceBlock::setLit(bool lit, Level* level, const BlockPos& pos)
{
    const BlockState* state = level->getBlockState(pos);
    const auto* block = dynamic_cast<FurnaceBlock*>(state->getBlock());
    if (!block)
        return;
    Block* replacement = furnaceBlock(block->mKind, lit);
    const BlockState* replacementState = replacement->defaultBlockState()->setValue(FACING, state->getValue(FACING));
    boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(pos);
    sKeepInventory = true;
    level->setBlock(pos, replacementState, Block::UPDATE_ALL, false);
    level->setBlock(pos, replacementState, Block::UPDATE_ALL, false);
    sKeepInventory = false;
    if (blockEntity != NULL)
    {
        blockEntity->clearRemoved();
        level->setBlockEntity(pos, blockEntity);
    }
}

void FurnaceBlock::setPlacedBy(Level* level, const BlockPos& pos, const BlockState* state,
                               boost::shared_ptr<LivingEntity> placer, not_null_ptr<ItemInstance> itemInstance)
{
    DirectionProperty* property = FACING;
    level->setBlock(pos, state->setValue(property, placer->getDirection()->getOpposite()), Block::UPDATE_CLIENTS,
                    false);
    if (itemInstance->hasCustomHoverName())
    {
        boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(pos);
        bool isFurnace = blockEntity && eTYPE_DERIVED_FROM(BlockEntityType::FURNACE, blockEntity->GetType());
        if (isFurnace)
        {
            boost::shared_ptr<FurnaceBlockEntity> furnace = boost::dynamic_pointer_cast<FurnaceBlockEntity>(
                blockEntity);
            furnace->setCustomName(itemInstance->getHoverNameUnfiltered(), itemInstance->getOwnerUUID(),
                                   itemInstance->wasCreatedByRestrictedPlayer(), itemInstance->wasCreatedOnHost());
        }
    }
}

void FurnaceBlock::onRemove(Level* level, const BlockPos& pos, const BlockState* state)
{
    if (!sKeepInventory)
    {
        boost::shared_ptr<BlockEntity> blockEntity = level->getBlockEntity(pos);
        if (blockEntity.get() != NULL)
        {
            bool isFurnace = eTYPE_DERIVED_FROM(BlockEntityType::FURNACE, blockEntity->GetType());
            if (isFurnace)
            {
                Containers::dropContents(level, pos, boost::dynamic_pointer_cast<FurnaceBlockEntity>(blockEntity));
                level->updateNeighbourForOutputSignal(pos, this);
            }
        }
    }
    BaseEntityBlock::onRemove(level, pos, state);
}

bool FurnaceBlock::hasAnalogOutputSignal(const BlockState*)
{
    return true;
}

int FurnaceBlock::getAnalogOutputSignal(const BlockState*, Level* level, const BlockPos& pos)
{
    return AbstractContainerMenu::getRedstoneSignalFromBlockEntity(level->getBlockEntity(pos));
}

not_null_ptr<ItemInstance> FurnaceBlock::getCloneItemInstance(Level*, const BlockPos&, const BlockState*)
{
    return not_null_ptr<ItemInstance>(new ItemInstance(furnaceBlock(mKind, false)));
}

EBlockRenderShape FurnaceBlock::getRenderShape(const BlockState*)
{
    return BLOCK_RENDER_MODEL;
}

const BlockState* FurnaceBlock::getInventoryRenderState(const BlockState*)
{
    return defaultBlockState()->setValue(FACING, Direction::SOUTH);
}

const BlockState* FurnaceBlock::getBlockState(int data)
{
    const Direction* direction = Direction::from3DDataValue(data);
    if (direction->getAxis() == Direction::Axis::Y)
        direction = Direction::NORTH;
    return defaultBlockState()->setValue(FACING, direction);
}

int FurnaceBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(FACING)->get3DDataValue();
}

unsigned int FurnaceBlock::GetInteractTooltip(const BlockTooltipDataHolder& tooltipData)
{
    if ((tooltipData.m_bUseBlock || tooltipData.m_bUseItemOnBlock) && tooltipData.m_bCanPlace)
        return IDS_TOOLTIPS_USE;
    return Block::GetInteractTooltip(tooltipData);
}

const BlockState* FurnaceBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(FACING, rotation->rotate(state->getTypedBoxed(FACING)->getValue()));
}

const BlockState* FurnaceBlock::mirror(const BlockState* state, Mirror* mirror)
{
    DirectionProperty* facing = FACING;
    const BlockState* result = rotate(state, mirror->getRotation(state->getValue(facing)));
    return result;
}

BlockStateDefinition* FurnaceBlock::createBlockStateDefinition()
{
    static const Property* properties[] = {FACING};
    return new BlockStateDefinition(this, properties, 1);
}

TextureAtlasSprite* FurnaceBlock::getTexture(const Direction* direction, const BlockState* state)
{
    if (direction == Direction::DOWN)
        return mBottomIcon;
    if (direction == Direction::UP)
        return mTopIcon;
    return direction != state->getValue(FACING) ? mIcon : mFrontIcon;
}

void FurnaceBlock::registerIcons(IconRegister* iconRegister)
{
    if (mKind == FurnaceKind::Smoker)
    {
        mIcon = iconRegister->registerIcon(L"smoker_side");
        mFrontIcon = iconRegister->registerIcon(mLit ? L"smoker_front_on" : L"smoker_front");
        mTopIcon = iconRegister->registerIcon(L"smoker_top");
        mBottomIcon = iconRegister->registerIcon(L"smoker_bottom");
        return;
    }
    if (mKind == FurnaceKind::BlastFurnace)
    {
        mIcon = iconRegister->registerIcon(L"blast_furnace_side");
        mFrontIcon = iconRegister->registerIcon(mLit ? L"blast_furnace_front_on" : L"blast_furnace_front");
        mTopIcon = iconRegister->registerIcon(L"blast_furnace_top");
        mBottomIcon = mTopIcon;
        return;
    }
    mIcon = iconRegister->registerIcon(L"furnace_side");
    mFrontIcon = iconRegister->registerIcon(mLit ? L"furnace_front_lit" : L"furnace_front");
    mTopIcon = iconRegister->registerIcon(L"furnace_top");
    mBottomIcon = mTopIcon;
}
