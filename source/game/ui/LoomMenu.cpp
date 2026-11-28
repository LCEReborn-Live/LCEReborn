#include "game/ui/LoomMenu.hpp"

#include <algorithm>

#include "game/core/BannerPattern.hpp"
#include "game/core/DyeColor.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/L10N.hpp"
#include "game/item/BannerStencilItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/SimpleContainer.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BannerBlockEntity.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
class PatternSlot : public Slot
{
public:
    PatternSlot(const boost::shared_ptr<Container>& container, int index) : Slot(container, index, 0, 0) {}
    bool mayPickup(boost::shared_ptr<Player>) override { return false; }
    bool mayPlace(not_null_ptr<ItemInstance>) override { return false; }
};

not_null_ptr<ItemInstance> pageArrow(int name)
{
    auto arrow = not_null_ptr<ItemInstance>(new ItemInstance(Items::arrow));
    arrow->setHoverName(L10N::GetString(name));
    return arrow;
}
}

LoomMenu::LoomMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos)
    : TwoInputMenu(inventory, level, pos, Blocks::loom, InputCount), mPreviews(new SimpleContainer(L"", PatternSlotCount))
{
    for (int i = 0; i < PatternSlotCount; ++i)
        addSlot(new PatternSlot(mPreviews, i));
}

bool LoomMenu::accepts(int slot, not_null_ptr<ItemInstance> item)
{
    switch (slot)
    {
    case 0: return item->getItem() == Items::banner;
    case 1: return item->getItem() == Items::dye;
    case 2: return item->getItem() == Items::bannerStencil && BannerStencilItem::patternFor(item->getAuxValue());
    default: return false;
    }
}

std::vector<const BannerPattern*> LoomMenu::availablePatterns()
{
    if (input(0)->isEmpty() || input(1)->isEmpty() || BannerBlockEntity::getPatternCount(input(0)) >= 6)
        return {};
    if (!input(2)->isEmpty())
    {
        const auto* pattern = BannerStencilItem::patternFor(input(2)->getAuxValue());
        return pattern ? std::vector<const BannerPattern*>{pattern} : std::vector<const BannerPattern*>{};
    }
    std::vector<const BannerPattern*> patterns;
    for (const auto* pattern : BannerPattern::PATTERNS)
        if (pattern != BannerPattern::BASE && pattern->hasPattern() && !pattern->hasPatternItem())
            patterns.push_back(pattern);
    return patterns;
}

not_null_ptr<ItemInstance> LoomMenu::preview(const BannerPattern* pattern)
{
    if (!pattern)
        return ItemInstance::EMPTY;
    auto result = input(0)->copy();
    result->setCount(1);
    BannerBlockEntity::fjAddPatternToItem(result, pattern, DyeColor::byItemData(input(1)->getAuxValue()));
    return result;
}

not_null_ptr<ItemInstance> LoomMenu::makeResult()
{
    const auto patterns = availablePatterns();
    const int count = static_cast<int>(patterns.size());
    mPage = count > 0 ? std::min(mPage, (count - 1) / PatternsPerPage) : 0;
    if (count == 1 && !input(2)->isEmpty())
        mSelected = patterns.front();
    else if (std::find(patterns.begin(), patterns.end(), mSelected) == patterns.end())
        mSelected = nullptr;

    for (int i = 0; i < PatternsPerPage; ++i)
    {
        const int index = mPage * PatternsPerPage + i;
        mPreviews->setItem(i, index < count ? preview(patterns[index]) : ItemInstance::EMPTY);
    }
    mPreviews->setItem(PreviousPage, mPage > 0 ? pageArrow(IDS_LCE_LOOM_PREVIOUS) : ItemInstance::EMPTY);
    mPreviews->setItem(NextPage, (mPage + 1) * PatternsPerPage < count ? pageArrow(IDS_LCE_LOOM_NEXT) : ItemInstance::EMPTY);
    return preview(mSelected);
}

bool LoomMenu::clickMenuButton(boost::shared_ptr<Player> player, int id)
{
    if (id < 0 || id >= PatternSlotCount || !stillValid(player) || mPreviews->getItem(id)->isEmpty())
        return false;
    if (!mLevel->mIsClientSide)
    {
        if (id == PreviousPage || id == NextPage)
            mPage += id == NextPage ? 1 : -1;
        else
        {
            const auto patterns = availablePatterns();
            const int index = mPage * PatternsPerPage + id;
            if (index >= static_cast<int>(patterns.size()))
                return false;
            mSelected = patterns[index];
        }
        slotsChanged();
    }
    return true;
}

not_null_ptr<ItemInstance> LoomMenu::clicked(int slot, int button, ClickType::eClickType type, boost::shared_ptr<Player> player)
{
    return slot >= PatternStart ? ItemInstance::EMPTY : TwoInputMenu::clicked(slot, button, type, player);
}

int LoomMenu::getSelectedPattern()
{
    const auto result = getSlot(ResultSlot)->getItem();
    if (!result->isEmpty())
        for (int i = 0; i < PatternsPerPage; ++i)
            if (ItemInstance::isSame(result, mPreviews->getItem(i)) && ItemInstance::tagMatches(result, mPreviews->getItem(i)))
                return i;
    return -1;
}

void LoomMenu::finishResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance>)
{
    if (mSelected == BannerPattern::MOJANG)
        player->awardStat(GenericStats::achievement(eAward_FruitOnTheLoom), GenericStats::param_noArgs());
}
