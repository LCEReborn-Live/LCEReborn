#include "game/ui/GrindstoneMenu.hpp"

#include <algorithm>
#include <memory>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/core/ExperienceOrb.hpp"
#include "game/core/GenericStats.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/item/Enchantment.hpp"
#include "game/item/EnchantmentHelper.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
using EnchantmentMap = boost::unordered_map<Enchantment*, int>;

int readEnchantments(const not_null_ptr<ItemInstance>& item, EnchantmentMap& curses)
{
    if (item->isEmpty())
        return 0;
    std::unique_ptr<EnchantmentMap> enchantments(EnchantmentHelper::getEnchantments(item));
    int experience = 0;
    for (const auto& entry : *enchantments)
    {
        Enchantment* enchantment = entry.first;
        if (!enchantment || entry.second <= 0)
            continue;
        if (enchantment->isCurse())
            curses[enchantment] = std::max(curses[enchantment], entry.second);
        else
            experience += enchantment->getMinCost(std::min(entry.second, enchantment->getMaxLevel()));
    }
    return experience;
}
}

GrindstoneMenu::GrindstoneMenu(boost::shared_ptr<Inventory> inventory, Level* level, const BlockPos& pos)
    : TwoInputMenu(inventory, level, pos, Blocks::grindstone)
{
}

bool GrindstoneMenu::accepts(int, not_null_ptr<ItemInstance> item)
{
    return item->isDamageableItem() || item->isEnchanted() || item->getItem() == Items::enchantedBook;
}

not_null_ptr<ItemInstance> GrindstoneMenu::makeResult()
{
    const auto first = input(0);
    const auto second = input(1);
    const bool repairing = !first->isEmpty() && !second->isEmpty();
    auto source = first->isEmpty() ? second : first;
    if (source->isEmpty() || (repairing && (first->getItem() != second->getItem()
                                           || !first->isDamageableItem() || !second->isDamageableItem())))
        return ItemInstance::EMPTY;

    EnchantmentMap curses;
    const int experience = readEnchantments(first, curses) + readEnchantments(second, curses);
    if (!repairing && experience == 0)
        return ItemInstance::EMPTY;

    auto result = source->copy();
    result->setCount(1);
    if (repairing)
    {
        const int maximum = source->getMaxDamage();
        const int remaining = 2 * maximum - first->getDamageValue() - second->getDamageValue() + maximum / 20;
        result->setAuxValue(std::max(0, maximum - remaining));
    }
    if (auto* tag = result->getTag())
    {
        tag->remove(ItemInstance::TAG_ENCH);
        tag->remove(L"StoredEnchantments");
        tag->remove(ItemInstance::TAG_REPAIR_COST);
    }
    if (result->getItem() == Items::enchantedBook && curses.empty())
    {
        auto book = not_null_ptr<ItemInstance>(new ItemInstance(Items::book));
        if (result->getTag())
            book->setTag(result->getTag()->copy());
        return book;
    }
    if (!curses.empty())
        EnchantmentHelper::setEnchantments(&curses, result);
    return result;
}

void GrindstoneMenu::finishResult(boost::shared_ptr<Player> player, not_null_ptr<ItemInstance>)
{
    mLevel->playSound(boost::shared_ptr<Player>(), mPos, SoundEvents::GRINDSTONE_USE,
                     SoundSource::eSoundSource_Blocks, 1.0f, 1.0f, 16.0f);
    EnchantmentMap curses;
    const int total = readEnchantments(input(0), curses) + readEnchantments(input(1), curses);
    if (total <= 0)
        return;
    player->awardStat(GenericStats::achievement(eAward_Disenchanted), GenericStats::param_noArgs());
    const int minimum = (total + 1) / 2;
    int experience = minimum + mLevel->mRandom->nextInt(minimum);
    while (experience > 0)
    {
        const int value = ExperienceOrb::getExperienceValue(experience);
        experience -= value;
        mLevel->addEntity(boost::shared_ptr<ExperienceOrb>(new ExperienceOrb(
            mLevel, mPos.x + 0.5, mPos.y + 0.5, mPos.z + 0.5, value)));
    }
}
