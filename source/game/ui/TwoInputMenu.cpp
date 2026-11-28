#include "game/ui/TwoInputMenu.hpp"

#include <algorithm>

#include "game/core/SharedConstants.hpp"
#include "game/item/CraftingContainer.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/ResultContainer.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
class WorkstationInputSlot : public Slot
{
public:
    WorkstationInputSlot(TwoInputMenu& menu, const boost::shared_ptr<Container>& inputs, int slot)
        : Slot(inputs, slot, 56, 17 + slot * 36), mMenu(menu) {}

    bool mayPlace(not_null_ptr<ItemInstance> item) override { return mMenu.accepts(mSlot, item); }
    int getMaxStackSize() override { return mMenu.inputLimit(); }

private:
    TwoInputMenu& mMenu;
};

class WorkstationResultSlot : public Slot
{
public:
    WorkstationResultSlot(TwoInputMenu& menu, const boost::shared_ptr<Container>& result)
        : Slot(result, 0, 116, 35), mMenu(menu) {}

    bool mayPlace(not_null_ptr<ItemInstance>) override { return false; }
    bool mayCombine(not_null_ptr<ItemInstance>) override { return false; }

    not_null_ptr<ItemInstance> onTake(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> item) override
    {
        mMenu.takeResult(player, item);
        return item;
    }

private:
    TwoInputMenu& mMenu;
};
}

TwoInputMenu::TwoInputMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos,
                           Block* block, int inputCount)
    : mLevel(level), mPos(pos), mInputs(new CraftingContainer(this, 1, inputCount)),
      mResult(new ResultContainer()), mBlock(block), mInputCount(inputCount)
{
    for (int i = 0; i < mInputCount; ++i)
        addSlot(new WorkstationInputSlot(*this, mInputs, i));
    addSlot(new WorkstationResultSlot(*this, mResult));
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 9; ++column)
            addSlot(new Slot(inventory, 9 + row * 9 + column, 8 + column * 18, 84 + row * 18));
    for (int column = 0; column < 9; ++column)
        addSlot(new Slot(inventory, column, 8 + column * 18, 142));
}

not_null_ptr<ItemInstance> TwoInputMenu::input(int slot)
{
    return mInputs->getItem(slot);
}

void TwoInputMenu::slotsChanged()
{
    if (!mLevel->mIsClientSide)
        mResult->setItem(0, makeResult());
}

void TwoInputMenu::takeResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> result)
{
    if (!mLevel->mIsClientSide)
        finishResult(player, result);
    for (int slot = 0; slot < 2; ++slot)
        if (!mInputs->getItem(slot)->isEmpty())
            mInputs->removeItem(slot, 1);
}

not_null_ptr<ItemInstance> TwoInputMenu::clicked(int slot, int button, ClickType::eClickType type,
                                                   boost::shared_ptr<Player> player)
{
    if (slot == mInputCount && type == ClickType::eClickType_Clone)
        return ItemInstance::EMPTY;
    return AbstractContainerMenu::clicked(slot, button, type, player);
}

not_null_ptr<ItemInstance> TwoInputMenu::quickMoveStack(boost::shared_ptr<Player> player, int index)
{
    Slot* slot = getSlot(index);
    if (!slot->hasItem())
        return ItemInstance::EMPTY;
    auto stack = slot->getItem();
    auto original = stack->copy();
    const int inventoryStart = mInputCount + 1;
    const int hotbarStart = inventoryStart + 27;
    const int slotsEnd = hotbarStart + 9;
    if (index == mInputCount)
    {
        for (int i = slotsEnd - 1; i >= inventoryStart; --i)
        {
            Slot* destination = getSlot(i);
            auto existing = destination->getItem();
            if (!existing->isEmpty() && (!ItemInstance::isSame(stack, existing)
                                         || !ItemInstance::tagMatches(stack, existing)))
                continue;
            const int limit = std::min(destination->getMaxStackSize(stack), stack->getMaxStackSize());
            if (existing->getCount() + stack->getCount() > limit)
                continue;
            slot->set(ItemInstance::EMPTY);
            slot->onTake(player, stack);
            if (existing->isEmpty())
                destination->set(stack);
            else
            {
                existing->grow(stack->getCount());
                destination->setChanged();
            }
            return original;
        }
        return ItemInstance::EMPTY;
    }

    bool moved = false;
    if (index < mInputCount)
        moved = moveItemStackTo(stack, inventoryStart, slotsEnd, false, true, true);
    else
    {
        const int first = preferredInput(stack);
        for (int offset = 0; offset < mInputCount; ++offset)
        {
            const int inputSlot = (first + offset) % mInputCount;
            if (getSlot(inputSlot)->mayPlace(stack)
                && moveItemStackTo(stack, inputSlot, inputSlot + 1, false, true, true))
            {
                moved = true;
                break;
            }
        }
        if (!moved)
            moved = index < hotbarStart ? moveItemStackTo(stack, hotbarStart, slotsEnd, false, true, true)
                                        : moveItemStackTo(stack, inventoryStart, hotbarStart, false, true, true);
    }
    if (!moved)
        return ItemInstance::EMPTY;
    if (stack->isEmpty())
        slot->set(ItemInstance::EMPTY);
    else
        slot->setChanged();
    slotsChanged();
    return original;
}

bool TwoInputMenu::canTakeItemForPickAll(not_null_ptr<ItemInstance>, Slot* slot)
{
    return slot->mContainer != mResult;
}

bool TwoInputMenu::stillValid(boost::shared_ptr<Player> player)
{
    return mLevel->mIsClientSide
        || (mLevel->getBlockState(mPos)->getBlock() == mBlock
            && player->distanceAsIfHitscanned(mPos, *player->mBoundingBox) <= SharedConstants::CONTAINER_INTERACT_DISTANCE_SQR);
}

void TwoInputMenu::removed(boost::shared_ptr<Player> player)
{
    AbstractContainerMenu::removed(player);
    if (!mLevel->mIsClientSide)
        clearContainer(player, mLevel, mInputs);
}
