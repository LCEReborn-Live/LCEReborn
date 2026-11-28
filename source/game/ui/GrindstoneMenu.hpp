#ifndef GAME_UI_GRINDSTONEMENU_HPP
#define GAME_UI_GRINDSTONEMENU_HPP

#include "game/ui/TwoInputMenu.hpp"

class GrindstoneMenu : public TwoInputMenu
{
public:
    GrindstoneMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos);
    bool accepts(int slot, not_null_ptr<ItemInstance> item) override;
    int inputLimit() const override { return 1; }

private:
    not_null_ptr<ItemInstance> makeResult() override;
    void finishResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> result) override;
};

#endif
