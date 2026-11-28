#ifndef GAME_UI_LOOMMENU_HPP
#define GAME_UI_LOOMMENU_HPP

#include "game/ui/TwoInputMenu.hpp"

class BannerPattern;
class SimpleContainer;

class LoomMenu : public TwoInputMenu
{
public:
    static constexpr int InputCount = 3;
    static constexpr int ResultSlot = InputCount;
    static constexpr int PatternStart = ResultSlot + 37;
    static constexpr int PatternsPerPage = 6;
    static constexpr int PreviousPage = PatternsPerPage;
    static constexpr int NextPage = PreviousPage + 1;
    static constexpr int PatternSlotCount = NextPage + 1;

    LoomMenu(boost::shared_ptr<Inventory>, Level*, const BlockPos&);
    bool accepts(int, not_null_ptr<ItemInstance>) override;
    bool clickMenuButton(boost::shared_ptr<Player>, int) override;
    not_null_ptr<ItemInstance> clicked(int, int, ClickType::eClickType, boost::shared_ptr<Player>) override;
    int getSelectedPattern();

protected:
    not_null_ptr<ItemInstance> makeResult() override;
    void finishResult(boost::shared_ptr<Player>, not_null_ptr<ItemInstance>) override;

private:
    std::vector<const BannerPattern*> availablePatterns();
    not_null_ptr<ItemInstance> preview(const BannerPattern*);
    boost::shared_ptr<SimpleContainer> mPreviews;
    const BannerPattern* mSelected = nullptr;
    int mPage = 0;
};

#endif
