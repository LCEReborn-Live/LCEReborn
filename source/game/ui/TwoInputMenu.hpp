#ifndef GAME_UI_TWOINPUTMENU_HPP
#define GAME_UI_TWOINPUTMENU_HPP

#include "game/ui/AbstractContainerMenu.hpp"
#include "game/world/block/BlockPos.hpp"

class Block;
class CraftingContainer;
class ResultContainer;

class TwoInputMenu : public AbstractContainerMenu
{
public:
    TwoInputMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos, Block* block,
                 int inputCount = 2);
    void slotsChanged() override;
    void removed(boost::shared_ptr<Player> player) override;
    bool stillValid(boost::shared_ptr<Player> player) override;
    bool canTakeItemForPickAll(not_null_ptr<ItemInstance> item, Slot* slot) override;
    bool doQuickMoveLoop() override { return false; }
    not_null_ptr<ItemInstance> quickMoveStack(boost::shared_ptr<Player> player, int index) override;
    not_null_ptr<ItemInstance> clicked(int slot, int button, ClickType::eClickType type,
                                      boost::shared_ptr<Player> player) override;

    virtual bool accepts(int slot, not_null_ptr<ItemInstance> item) = 0;
    virtual int inputLimit() const { return 64; }
    void takeResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> item);

protected:
    virtual not_null_ptr<ItemInstance> makeResult() = 0;
    virtual void finishResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> item) {}
    virtual int preferredInput(not_null_ptr<ItemInstance> item) { return 0; }
    not_null_ptr<ItemInstance> input(int slot);
    Level* mLevel;
    BlockPos mPos;

private:
    boost::shared_ptr<CraftingContainer> mInputs;
    boost::shared_ptr<ResultContainer> mResult;
    Block* mBlock;
    int mInputCount;
};

#endif
