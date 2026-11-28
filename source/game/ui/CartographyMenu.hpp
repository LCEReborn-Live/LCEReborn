#ifndef GAME_UI_CARTOGRAPHYMENU_HPP
#define GAME_UI_CARTOGRAPHYMENU_HPP

#include "game/ui/TwoInputMenu.hpp"

class CartographyMenu : public TwoInputMenu
{
public:
    CartographyMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos);
    bool accepts(int slot, not_null_ptr<ItemInstance> item) override;

private:
    not_null_ptr<ItemInstance> makeResult() override;
    void finishResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance> result) override;
    int preferredInput(not_null_ptr<ItemInstance> item) override;
};

#endif
