#ifndef GAME_UI_STONECUTTERMENU_HPP
#define GAME_UI_STONECUTTERMENU_HPP

#include "game/ui/TwoInputMenu.hpp"

class Item;
class SimpleContainer;

class StonecutterMenu : public TwoInputMenu
{
public:
    static constexpr int RecipeSlotStart = 39;
    static constexpr int RecipeSlotCount = 5;
    static constexpr int NextRecipe = RecipeSlotCount;

    StonecutterMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos);
    bool accepts(int slot, not_null_ptr<ItemInstance> item) override;
    bool clickMenuButton(boost::shared_ptr<Player> player, int id) override;
    not_null_ptr<ItemInstance> clicked(int slot, int button, ClickType::eClickType type,
                                      boost::shared_ptr<Player> player) override;
    int getSelectedRecipe();

protected:
    not_null_ptr<ItemInstance> makeResult() override;

private:
    Item* mMaterial = nullptr;
    int mMaterialData = 0;
    int mRecipe = 0;
    boost::shared_ptr<SimpleContainer> mRecipePreviews;
};

#endif
