#include "game/world/block/BarrelBlockEntity.hpp"

#include "game/core/EntityType.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/L10N.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/DataFixTypes.hpp"
#include "game/data/DataFixerUpper.hpp"
#include "game/item/ContainerHelper.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/ItemListDataWalker.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/ChestMenu.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BarrelBlock.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/generation/Player.hpp"

BarrelBlockEntity::BarrelBlockEntity()
    : mItems(NonNullList<not_null_ptr<ItemInstance>>::withSize(getContainerSize(), ItemInstance::EMPTY))
{
}

BarrelBlockEntity::~BarrelBlockEntity()
{
    delete[] mItems.items.data;
}

BlockEntity* BarrelBlockEntity::create()
{
    return new BarrelBlockEntity();
}

void BarrelBlockEntity::addDataWalkers(DataFixerUpper* fixer)
{
    fixer->addWalker(DataFixTypes::eDataFixTypes_BlockEntity,
                     new ItemListDataWalker(BlockEntityType::BARREL, L"Items"));
}

bool BarrelBlockEntity::isEmpty()
{
    for (unsigned int slot = 0; slot < mItems.size(); ++slot)
        if (!mItems[slot]->isEmpty())
            return false;
    return true;
}

std::wstring BarrelBlockEntity::getName()
{
    return hasCustomName() ? mCustomName->get() : L10N::GetString(IDS_LCE_TILE_BARREL);
}

std::wstring BarrelBlockEntity::getCustomName()
{
    return hasCustomName() ? mCustomName->get() : std::wstring();
}

void BarrelBlockEntity::setItems(NonNullList<not_null_ptr<ItemInstance>> items)
{
    delete[] mItems.items.data;
    mItems = items;
}

int BarrelBlockEntity::getTypeId(bool)
{
    return ClientboundContainerOpenPacket::BARREL;
}

AbstractContainerMenu* BarrelBlockEntity::createMenu(boost::shared_ptr<Inventory> inventory,
                                                    boost::shared_ptr<Player> player)
{
    unpackLootTable(player);
    return new ChestMenu(inventory, boost::static_pointer_cast<BarrelBlockEntity>(shared_from_this()), player);
}

void BarrelBlockEntity::load(CompoundTag* tag)
{
    LockableContainerBlockEntity::load(tag);
    mCustomName->loadFromTag(tag);
    setItems(NonNullList<not_null_ptr<ItemInstance>>::withSize(getContainerSize(), ItemInstance::EMPTY));
    if (!tryLoadLootTable(tag))
        ContainerHelper::loadAllItems(tag, mItems);
}

CompoundTag* BarrelBlockEntity::save(CompoundTag* tag)
{
    LockableContainerBlockEntity::save(tag);
    mCustomName->saveToTag(tag);
    if (!trySaveLootTable(tag))
        ContainerHelper::saveAllItems(tag, mItems);
    return tag;
}

boost::shared_ptr<BlockEntity> BarrelBlockEntity::clone()
{
    auto copy = boost::shared_ptr<BarrelBlockEntity>(new BarrelBlockEntity());
    CompoundTag data;
    save(&data);
    copy->load(&data);
    BlockEntity::clone(copy);
    return copy;
}

unsigned long long BarrelBlockEntity::GetType()
{
    return BlockEntityType::BARREL;
}

void BarrelBlockEntity::startOpen(boost::shared_ptr<Player> player)
{
    if (mLevel && !mLevel->mIsClientSide && !player->isSpectator() && ++mOpenCount == 1)
        setOpen(true, true);
}

void BarrelBlockEntity::stopOpen(boost::shared_ptr<Player> player)
{
    if (mLevel && !mLevel->mIsClientSide && !player->isSpectator() && mOpenCount > 0 && --mOpenCount == 0)
        setOpen(false, true);
}

void BarrelBlockEntity::tick()
{
    if (mCheckLoadedState && mLevel && !mLevel->mIsClientSide)
    {
        mCheckLoadedState = false;
        setOpen(mOpenCount > 0, false);
    }
}

void BarrelBlockEntity::setOpen(bool open, bool playSound)
{
    const auto* state = mLevel->getBlockState(mPos);
    if (isRemoved() || state->getBlock() != Blocks::barrel || state->getValue(BarrelBlock::OPEN) == open)
        return;

    mLevel->setBlock(mPos, state->setValue(BarrelBlock::OPEN, open), Block::UPDATE_CLIENTS, false);
    if (playSound)
        mLevel->playSound(boost::shared_ptr<Player>(), mPos.x + 0.5, mPos.y + 0.5, mPos.z + 0.5,
                          open ? SoundEvents::BARREL_OPEN : SoundEvents::BARREL_CLOSE,
                          SoundSource::eSoundSource_Blocks, 0.5f, 1.0f, 16.0f);
}
