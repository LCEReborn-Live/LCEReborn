#include "game/world/generation/RaidHeroGifts.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <map>
#include "game/entity/ai/LookControl.hpp"
#include "game/core/LootPool.hpp"
#include "game/core/LootTable.hpp"
#include "game/core/LootTableContext.hpp"
#include "game/core/SetNBTFunction.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/entity/mob/FireworksRocketEntity.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/data/ListTag.hpp"
#include "game/item/DyeItem.hpp"
#include "game/core/DyeColor.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/item/Item.hpp"
#include "game/item/Items.hpp"
#include "game/item/LootItem.hpp"
#include "game/item/SetItemCountFunction.hpp"
#include "game/item/SetItemDataFunction.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Raids.hpp"

void announceRaidVictory(ServerLevel& level, const BlockPos& center)
{
    constexpr int range = 32;
    const auto* area = AABB::newTemp(center.x - range, center.y - range, center.z - range,
                                    center.x + range + 1, center.y + range + 1, center.z + range + 1);
    const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> residents(
        level.getEntitiesOfClass(typeid(Villager), area));
    boost::shared_ptr<Entity> nearest;
    double distance = range * range;
    for (const auto& resident : *residents)
    {
        const double candidate = resident->distanceToSqr(center.x + 0.5, center.y, center.z + 0.5);
        if (resident->isAlive() && candidate < distance)
        {
            nearest = resident;
            distance = candidate;
        }
    }
    if (nearest)
        nearest->playSound(SoundEvents::VILLAGER_YES, 1.0f, 1.0f);
}

void tickVillagerRaidCelebration(Villager& villager)
{
    auto* level = static_cast<ServerLevel*>(villager.mLevel);
    if (!level->getRaids()) return;
    auto* raid = level->getRaids()->getNearby(BlockPos(&villager));
    if (!raid || raid->getStatus() != Raid::Victory || !level->canSeeSky(BlockPos(&villager))) return;
    auto* random = villager.getRandom();
    if (random->nextInt(100) == 0) villager.playSound(SoundEvents::VILLAGER_YES, 1, 1);
    if (random->nextInt(200) != 0) return;
    auto rocket = not_null_ptr<ItemInstance>(new ItemInstance(Items::fireworks));
    auto* tag = new CompoundTag(); auto* fireworks = new CompoundTag();
    auto* explosions = new ListTag(); auto* explosion = new CompoundTag();
    arrayWithLength<int> colors(1, true);
    colors[0] = DyeItem::DYE_COLORS[DyeColor::byBlockData(random->nextInt(16))->getItemData()];
    explosion->putIntArray(L"Colors", colors); explosion->putByte(L"Type", 4);
    explosions->add(explosion); fireworks->put(L"Explosions", explosions);
    fireworks->putByte(L"Flight", random->nextInt(3)); tag->put(L"Fireworks", fireworks); rocket->setTag(tag);
    level->addEntity(boost::shared_ptr<FireworksRocketEntity>(new FireworksRocketEntity(level,
        villager.mX, villager.mY + villager.getEyeHeight(), villager.mZ, rocket)));
}

namespace
{
struct GiftEntry { const wchar_t* item; int data, weight, minimum, maximum; const wchar_t* potion; };
struct GiftDefinition { const wchar_t* profession; const GiftEntry* entries; size_t count; };
#include "game/world/generation/RaidHeroGiftData.inc"
const char* professionName(int profession, unsigned int career)
{
    static const char* farmers[] = {"farmer", "farmer", "fisherman", "shepherd", "fletcher"};
    static const char* smiths[] = {"armorer", "armorer", "weaponsmith", "toolsmith"};
    if (profession == 0) return farmers[std::min(career, 4u)];
    if (profession == 1) return career == 2 ? "cartographer" : "librarian";
    if (profession == 2) return "cleric";
    if (profession == 3) return smiths[std::min(career, 3u)];
    if (profession == 4) return career == 2 ? "leatherworker" : "butcher";
    if (profession == Villager::MASON) return "mason";
    return "";
}
}
LootTable* raidHeroGiftTable(int profession, unsigned int career)
{
    static const auto tables = [] {
        std::map<std::wstring, LootTable*> result;
        for (const auto& definition : giftDefinitions)
        {
            auto* entries = new std::vector<LootPoolEntry*>;
            for (size_t i = 0; i < definition.count; ++i)
            {
                const auto& entry = definition.entries[i];
                auto* functions = new std::vector<LootItemFunction*>;
                if (entry.data) functions->push_back(new SetItemDataFunction(nullptr, RandomValueBounds(entry.data)));
                functions->push_back(new SetItemCountFunction(nullptr, RandomValueBounds(entry.minimum, entry.maximum)));
                if (entry.potion)
                {
                    auto* tag = new CompoundTag(); tag->putString(L"Potion", entry.potion);
                    functions->push_back(new SetNBTFunction(nullptr, tag));
                }
                entries->push_back(new LootItem(Item::itemRegistry.get(ResourceLocation(entry.item)), entry.weight,
                    0, functions, nullptr, nullptr));
            }
            auto* pools = new std::vector<LootPool*>;
            pools->push_back(new LootPool(entries, nullptr, RandomValueBounds(1), RandomValueBounds(0), nullptr));
            result.emplace(definition.profession, new LootTable(pools));
        }
        return result;
    }();
    const std::string name = professionName(profession, career);
    auto found = tables.find(std::wstring(name.begin(), name.end()));
    return found == tables.end() ? nullptr : found->second;
}
GiveRaidHeroGiftGoal::GiveRaidHeroGiftGoal(Villager& villager) : mVillager(villager) { setRequiredControlFlags(3); }
bool GiveRaidHeroGiftGoal::canUse()
{
    const int elapsed = std::max(1, mVillager.mTickCount - mLastCheck);
    mLastCheck = mVillager.mTickCount;
    if (mVillager.isTakingShelter() || mVillager.isSleeping() || mVillager.isTrading()) return false;
    boost::shared_ptr<Player> nearest;
    double distance = 16 * 16;
    for (auto& player : mVillager.mLevel->mPlayers)
        if (player->isAlive() && !player->isSpectator() && player->hasEffect(MobEffects::HERO_OF_THE_VILLAGE)
            && mVillager.distanceToSqr(player) < distance && mVillager.canSee(player))
        { nearest = player; distance = mVillager.distanceToSqr(player); }
    if (!nearest) return false;
    if (mCooldown > 0) { mCooldown -= std::min(elapsed, 3); return false; }
    mHero = nearest;
    return true;
}
bool GiveRaidHeroGiftGoal::canContinueToUse()
{
    auto hero = mHero.lock();
    return hero && hero->isAlive() && hero->hasEffect(MobEffects::HERO_OF_THE_VILLAGE) && !mGiftGiven
        && !mVillager.isTakingShelter() && mVillager.distanceToSqr(hero) <= 256
        && mVillager.mTickCount - mStarted < 100;
}
void GiveRaidHeroGiftGoal::start() { mStarted = mVillager.mTickCount; mGiftGiven = false; }
void GiveRaidHeroGiftGoal::stop()
{
    mCooldown = 600 + mVillager.getRandom()->nextInt(6001);
    mLastCheck = mVillager.mTickCount;
    mHero.reset(); mVillager.getNavigation()->stop();
}
void GiveRaidHeroGiftGoal::tick()
{
    auto hero = mHero.lock();
    if (!hero) return;
    mVillager.getLookControl()->setLookAt(hero, 30, 30);
    if (mVillager.distanceToSqr(hero) > 25) { mVillager.getNavigation()->moveTo(hero, .5); return; }
    mVillager.getNavigation()->stop();
    if (mGiftGiven || mVillager.mTickCount - mStarted <= 20) return;
    std::vector<not_null_ptr<ItemInstance>> gifts;
    auto* table = raidHeroGiftTable(mVillager.getProfession(), mVillager.getCareer());
    if (mVillager.isBaby()) gifts.emplace_back(new ItemInstance(Blocks::red_flower, 1, 0));
    else if (!table) gifts.emplace_back(new ItemInstance(Items::wheatSeeds));
    else
    {
        auto* level = static_cast<ServerLevel*>(mVillager.mLevel);
        LootTableContext context(0, level, level->getLootTables(), mVillager.shared_from_this(), hero, nullptr);
        std::unique_ptr<std::vector<not_null_ptr<ItemInstance>>> generated(table->getRandomItems(mVillager.getRandom(), context));
        gifts = *generated;
    }
    for (auto& gift : gifts)
    {
        auto item = boost::shared_ptr<ItemEntity>(new ItemEntity(mVillager.mLevel, mVillager.mX,
            mVillager.mY + mVillager.getEyeHeight() - .3, mVillager.mZ, gift));
        const double dx = hero->mX - mVillager.mX, dy = hero->mY - mVillager.mY, dz = hero->mZ - mVillager.mZ;
        const double length = std::sqrt(dx*dx + dy*dy + dz*dz);
        const double speed = length > 0 ? .3 / length : 0;
        item->mXd = dx * speed; item->mYd = dy * speed + .1; item->mZd = dz * speed;
        item->setDefaultPickUpDelay(); mVillager.mLevel->addEntity(item);
    }
    mGiftGiven = true;
}
