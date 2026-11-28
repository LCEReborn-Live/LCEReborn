#include "game/world/generation/Villager.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/world/generation/Raids.hpp"
#include "game/world/generation/RaidHeroGifts.hpp"
#include "game/core/Pillager.hpp"
#include "game/entity/mob/EntityEvent.hpp"
#include "game/core/StringIds.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/GenericStats.hpp"

#include <initializer_list>
#include <limits>
#include <memory>
#include <algorithm>

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/render/ParticleType.hpp"
#include "game/core/AttributeInstance.hpp"
#include "game/core/BuiltInLootTables.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/DifficultyInstance.hpp"
#include "game/core/DyeColor.hpp"
#include "game/core/EvocationIllager.hpp"
#include "game/core/ExperienceOrb.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Node.hpp"
#include "game/core/Path.hpp"
#include "game/core/UniqueTypeWalker.hpp"
#include "game/core/Vex.hpp"
#include "game/core/VindicationIllager.hpp"
#include "game/core/Witch.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/DataFixerUpper.hpp"
#include "game/data/ListTag.hpp"
#include "game/entity/ai/AvoidEntityGoal.hpp"
#include "game/entity/ai/FloatGoal.hpp"
#include "game/entity/ai/GoalSelector.hpp"
#include "game/entity/ai/GroundPathNavigation.hpp"
#include "game/entity/ai/InteractGoal.hpp"
#include "game/entity/ai/MakeLoveGoal.hpp"
#include "game/entity/ai/MoveIndoorsGoal.hpp"
#include "game/entity/ai/MoveTowardsRestrictionGoal.hpp"
#include "game/entity/ai/OpenDoorGoal.hpp"
#include "game/entity/ai/PanicGoal.hpp"
#include "game/entity/ai/PlayGoal.hpp"
#include "game/entity/ai/RestrictOpenDoorGoal.hpp"
#include "game/entity/ai/TakeFlowerGoal.hpp"
#include "game/entity/ai/WaterAvoidingRandomStrollGoal.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/mob/EntityDataSerializers.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/entity/mob/SynchedEntityData.hpp"
#include "game/entity/mob/Zombie.hpp"
#include "game/entity/projectile/Arrow.hpp"
#include "game/item/EnchantedBookItem.hpp"
#include "game/item/Enchantment.hpp"
#include "game/item/EnchantmentHelper.hpp"
#include "game/item/EnchantmentInstance.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/ItemListDataWalker.hpp"
#include "game/item/Items.hpp"
#include "game/item/MapItem.hpp"
#include "game/item/MapItemSavedData.hpp"
#include "game/item/MerchantRecipe.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/item/SimpleContainer.hpp"
#include "game/item/SpawnEggItem.hpp"
#include "game/ui/EntityTooltipDataHolder.hpp"
#include "game/util/Random.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/ComposterBlock.hpp"
#include "game/world/block/HarvestFarmBlockGoal.hpp"
#include "game/world/block/MappedRegistry.hpp"
#include "game/world/generation/LargeFeature.hpp"
#include "game/world/generation/LookAtPlayerGoal.hpp"
#include "game/world/generation/LookAtTradingPlayerGoal.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/PlayerUID.hpp"
#include "game/world/generation/TradeWithPlayerGoal.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/VillagerInteractGoal.hpp"
#include "game/world/generation/Villages.hpp"
#include "game/item/ItemId.hpp"


const EntityDataAccessor<int>* Villager::DATA_PROFESSION = NULL;
const EntityDataAccessor<int>* Villager::DATA_CAREER = NULL;
const EntityDataAccessor<int>* Villager::DATA_TRADING = NULL;
const EntityDataAccessor<int>* Villager::DATA_SLEEP_DIRECTION = NULL;
const EntityDataAccessor<int>* Villager::DATA_TRADE_LEVEL = NULL;
const EntityDataAccessor<not_null_ptr<ItemInstance>>* Villager::DATA_TRADE_PREVIEW = nullptr;
std::vector<std::vector<std::vector<arrayWithLength<Villager::ItemListing*> > > > Villager::sTrades;

namespace
{
class VillagerDestinationGoal : public Goal
{
public:
    enum class Destination { Home, Work, Meeting };

    VillagerDestinationGoal(Villager* villager, Destination destination)
        : mVillager(villager), mDestinationType(destination)
    {
        setRequiredControlFlags(1);
    }

    bool canUse() override
    {
        const auto home = target();
        if (!home || mVillager->mLevel->getGameTime() < mRetryAt)
            return false;

        const BlockPos current(mVillager);
        if (current.distSqr(*home) <= 2 && BedBlock::hasSolidGroundAndTwoBlocksSpaceAt(mVillager->mLevel, current))
        {
            mHome = *home;
            mDestination = current;
            return true;
        }

        for (int x = -1; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z)
            {
                const BlockPos destination = home->offset(x, 0, z);
                if (!mVillager->mLevel->hasChunkAt(destination)
                    || !mVillager->mLevel->isEmptyBlock(destination)
                    || !BedBlock::hasSolidGroundAndTwoBlocksSpaceAt(mVillager->mLevel, destination))
                    continue;

                std::unique_ptr<Path> path(mVillager->getNavigation()->createPath(destination));
                const Node* end = path ? path->last() : nullptr;
                if (end && destination == BlockPos(end->x, end->y, end->z))
                {
                    mHome = *home;
                    mDestination = destination;
                    mPath = std::move(path);
                    return true;
                }
            }
        }

        mRetryAt = mVillager->mLevel->getGameTime() + 40;
        return false;
    }

    bool canContinueToUse() override
    {
        const auto* home = target();
        return home && *home == mHome
               && (!mVillager->getNavigation()->isDone() || BlockPos(mVillager) == mDestination);
    }

    void start() override
    {
        if (mPath)
            mVillager->getNavigation()->moveTo(mPath.release(), mVillager->isTakingShelter() ? 1.35 : 0.8);
        else
            mVillager->getNavigation()->stop();
    }

    void stop() override
    {
        mVillager->getNavigation()->stop();
    }

    std::wstring getName() override
    {
        if (mDestinationType == Destination::Work) return L"GoToWork";
        if (mDestinationType == Destination::Meeting) return L"VisitMeetingPlace";
        return L"GoHome";
    }

private:
    const BlockPos* target()
    {
        if (mVillager->isSleeping())
            return nullptr;
        if (mDestinationType == Destination::Work) return mVillager->getWorkDestination();
        if (mDestinationType == Destination::Meeting) return mVillager->getMeetingDestination();
        return mVillager->getHomeForShelter();
    }

    Villager* mVillager;
    Destination mDestinationType;
    BlockPos mHome;
    BlockPos mDestination;
    std::unique_ptr<Path> mPath;
    long long mRetryAt = std::numeric_limits<long long>::min();
};
}

Villager::PriceRange::PriceRange()
{
    mMin = 0;
    mMax = 0;
}

Villager::PriceRange::PriceRange(int min, int max)
{
    mMin = min;
    mMax = max;
}

int Villager::PriceRange::getValue(Random* random)
{
    if (mMin >= mMax)
    {
        return mMin;
    }

    return mMin + random->nextInt(mMax - mMin + 1);
}

Villager::EmeraldForItems::EmeraldForItems(Item* item, PriceRange price, int data)
    : mItem(item), mPrice(price), mData(data)
{
}

void Villager::EmeraldForItems::addMerchantRecipe(boost::shared_ptr<Merchant>, MerchantRecipeList* recipes,
                                                  Random* random)
{
    recipes->push_back(new MerchantRecipe(
        not_null_ptr<ItemInstance>(new ItemInstance(mItem, mPrice.getValue(random), mData)), Items::emerald));
}

Villager::ItemForEmeralds::ItemForEmeralds(const ItemInstance& item, const PriceRange& price)
    : mItem(item), mPrice(price)
{
}

void Villager::ItemForEmeralds::addMerchantRecipe(boost::shared_ptr<Merchant>, MerchantRecipeList* recipes,
                                                  Random* random)
{
    const auto buy = not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald, mPrice.getValue(random)));
    recipes->push_back(new MerchantRecipe(buy, mItem.copy()));
}

Villager::EnchantedItemForEmeralds::EnchantedItemForEmeralds(Item* item, const PriceRange& price) : mItem(item)
{
    mPrice = price;
}

void Villager::EnchantedItemForEmeralds::addMerchantRecipe(boost::shared_ptr<Merchant>, MerchantRecipeList* recipes,
                                                           Random* random)
{
    not_null_ptr<ItemInstance> buy(new ItemInstance(Items::emerald, mPrice.getValue(random), 0));
    not_null_ptr<ItemInstance> sell(new ItemInstance(mItem.getItem(), 1, mItem.getAuxValue()));
    sell = EnchantmentHelper::enchantItem(random, sell, random->nextInt(15) + 5, false);
    recipes->push_back(new MerchantRecipe(buy, sell));
}

Villager::ItemAndEmeraldToItem::ItemAndEmeraldToItem(Item* firstItem, const PriceRange& firstPrice, Item* secondItem,
                                                     const PriceRange& secondPrice)
    : mFirstItem(firstItem),
      mSecondItem(secondItem)
{
    mFirstPrice = firstPrice;
    mSecondPrice = secondPrice;
}

void Villager::ItemAndEmeraldToItem::addMerchantRecipe(boost::shared_ptr<Merchant>, MerchantRecipeList* recipes,
                                                       Random* random)
{
    int firstCount = mFirstPrice.getValue(random);
    int secondCount = mSecondPrice.getValue(random);
    recipes->push_back(new MerchantRecipe(
        not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald)),
        not_null_ptr<ItemInstance>(new ItemInstance(mFirstItem.getItem(), firstCount, mFirstItem.getAuxValue())),
        not_null_ptr<ItemInstance>(new ItemInstance(mSecondItem.getItem(), secondCount, mSecondItem.getAuxValue())), 0,
        7));
}

Villager::EnchantBookForEmeralds::EnchantBookForEmeralds()
{
}

void Villager::EnchantBookForEmeralds::addMerchantRecipe(boost::shared_ptr<Merchant>, MerchantRecipeList* recipes,
                                                         Random* random)
{
    Enchantment* enchantment = Enchantment::enchantmentRegistry->getRandom(random);
    int level = Mth::nextInt(random, enchantment->getMinLevel(), enchantment->getMaxLevel());
    not_null_ptr<ItemInstance> enchantedBook = EnchantedBookItem::createForEnchantment(
        new EnchantmentInstance(enchantment, level));
    int cost = random->nextInt(level * 10 + 5) + 2 + level * 3;

    if (enchantment->isTreasureOnly())
    {
        cost *= 2;
    }

    if (cost > 64)
    {
        cost = 64;
    }

    recipes->push_back(new MerchantRecipe(not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald, cost)),
                                          not_null_ptr<ItemInstance>(new ItemInstance(Items::book)),
                                          enchantedBook, 0, 7));
}

Villager::TreasureMapForEmeralds::TreasureMapForEmeralds(const PriceRange& price, std::wstring name,
                                                         const MapDecoration::Type* icon)
{
    mPrice = price;
    mName = name;
    mIcon = icon;
}

void Villager::TreasureMapForEmeralds::addMerchantRecipe(boost::shared_ptr<Merchant> merchant,
                                                         MerchantRecipeList* recipes, Random* random)
{
    int value = mPrice.getValue(random);
    Level* level = merchant->getLevel();
    BlockPos position = merchant->getPosition();
    BlockPos* target = level->findNearestMapFeature(mName, position, false);

    if (target != NULL)
    {
        not_null_ptr<ItemInstance> map = MapItem::setupNewMap(PlayerUID(0), level, 0, (double)target->x,
                                                              (double)target->z, 3, true, true);
        MapItem::renderBiomePreviewMap(level, map);
        MapItemSavedData::addTargetDecoration(map, *target, L"+", mIcon);

        if (mName == LargeFeature::MONUMENT)
        {
            map->setTranslatableName(IDS_ITEM_FILLED_MAP_OCEAN_MONUMENT);
        }
        else
        {
            map->setTranslatableName(mName == LargeFeature::MANSION ? IDS_ITEM_FILLED_MAP_WOODLAND_MANSION
                                                                    : IDS_DESC_MAP);
        }

        recipes->push_back(new MerchantRecipe(not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald, value)),
                                              not_null_ptr<ItemInstance>(new ItemInstance(Items::compass)), map, 0, 7));
        delete target;
    }
}

Villager::Villager(Level* level) : AgableMob(level)
{
    _init(UNEMPLOYED);
}

Villager::~Villager()
{
    if (mInventory != NULL)
    {
        delete mInventory;
    }

    delete mOffers;
}

Entity* Villager::create(Level* level)
{
    return new Villager(level);
}

void Villager::_init(int profession)
{
    fjDerivedCtorCalls();
    setProfession(profession);
    setSize(0.6f, 1.95f);
    mRandomTickDivider = 0;
    mInLove = false;
    mChasing = false;
    mVillage = boost::weak_ptr<Village>();
    mTradingPlayer = boost::weak_ptr<Player>();
    mOffers = NULL;
    mWillingToBreed = false;
    mRiches = 0;
    mRewardPlayersInVillage = false;
    mCareerLevel = 0;
    mInventory = new SimpleContainer(IDS_ITEMS, L"", false, 8);
    static_cast<GroundPathNavigation*>(getNavigation())->setCanOpenDoors(true);
    setCanPickUpLoot(true);
    mCategories |= EntityCategory::eEntityCategory_Villager;
}

namespace
{
class TradeChoice : public Villager::ItemListing
{
public:
    TradeChoice(std::initializer_list<Villager::ItemListing*> choices) : mChoices(choices) {}

    void addMerchantRecipe(boost::shared_ptr<Merchant> merchant, MerchantRecipeList* recipes, Random* random) override
    {
        mChoices[random->nextInt(static_cast<int>(mChoices.size()))]->addMerchantRecipe(merchant, recipes, random);
    }

private:
    std::vector<Villager::ItemListing*> mChoices;
};
}

void Villager::staticCtor()
{
    DATA_PROFESSION = SynchedEntityData::defineId(EntityType::VILLAGER, EntityType::AGABLE_MOB,
                                                  EntityDataSerializers::INT);
    DATA_CAREER = SynchedEntityData::defineId(EntityType::VILLAGER, EntityType::AGABLE_MOB, EntityDataSerializers::INT);
    DATA_TRADING = SynchedEntityData::defineId(EntityType::VILLAGER, EntityType::AGABLE_MOB,
                                               EntityDataSerializers::INT);
    DATA_SLEEP_DIRECTION = SynchedEntityData::defineId(EntityType::VILLAGER, EntityType::AGABLE_MOB,
                                                       EntityDataSerializers::INT);
    DATA_TRADE_LEVEL = SynchedEntityData::defineId(EntityType::VILLAGER, EntityType::AGABLE_MOB,
                                                  EntityDataSerializers::INT);

    DATA_TRADE_PREVIEW = SynchedEntityData::defineId(EntityType::VILLAGER, EntityType::AGABLE_MOB,
                                                    EntityDataSerializers::ITEM_INSTANCE);

    const auto buy = [](Item* item, int count, int data = 0) {
        return new EmeraldForItems(item, PriceRange(count, count), data);
    };
    const auto sell = [](Item* item, int cost, int count = 1, int data = 0) {
        return new ItemForEmeralds(ItemInstance(item, count, data), PriceRange(cost, cost));
    };
    const auto enchanted = [](Item* item, int minimum, int maximum) {
        return new EnchantedItemForEmeralds(item, PriceRange(minimum, maximum));
    };
    const auto block = [](Block* value) { return Item::byBlock(value); };

    // Profession and career indices are also used by saved villagers and their work sites.
    sTrades.resize(MASON + 1);
    const auto addTier = [](int profession, unsigned int career, auto& listings) {
        auto& careers = sTrades[profession];
        if (careers.size() < career)
            careers.resize(career);
        careers[career - 1].emplace_back(listings, static_cast<unsigned int>((sizeof(listings) / sizeof(*listings))));
    };

    static ItemListing* farmer[][2] = {
        {new TradeChoice({buy(Items::wheat, 20), buy(Items::potato, 26), buy(Items::carrot, 22),
                          buy(Items::beetroot, 15)}),
         sell(Items::bread, 1, 6)},
        {buy(block(Blocks::pumpkin), 6), sell(Items::pumpkinPie, 1, 4)},
        {buy(block(Blocks::melon_block), 4), sell(Items::apple, 1, 4)},
        {sell(Items::cookie, 1, 18), sell(Items::cake, 1)},
        {sell(Items::speckledMelon, 4, 3), sell(Items::goldenCarrot, 3, 3)},
    };
    for (auto& tier : farmer)
        addTier(0, 1, tier);

    static ItemListing* fisherman[][2] = {
        {buy(Items::string, 20), new ItemAndEmeraldToItem(Items::fish, {6, 6}, Items::cookedFish, {6, 6})},
        {buy(Items::fish, 15), sell(Items::fishBucket, 3)},
        {buy(Items::fish, 13, 1), enchanted(Items::fishingRod, 8, 22)},
        {buy(Items::fish, 6, 2), sell(Items::salmonBucket, 3)},
        {buy(Items::fish, 4, 3), buy(Items::boat, 1)},
    };
    for (auto& tier : fisherman)
        addTier(0, 2, tier);

    static ItemListing* shepherd[][2] = {
        {buy(block(Blocks::wool), 18), sell(Items::shears, 2)},
        {buy(Items::dye, 12, DyeColor::WHITE->getItemData()), sell(block(Blocks::wool), 1)},
        {buy(Items::dye, 12, DyeColor::GREEN->getItemData()), sell(Items::bed, 3)},
        {buy(Items::dye, 12, DyeColor::BLUE->getItemData()), sell(Items::banner, 3)},
        {buy(Items::dye, 12, DyeColor::RED->getItemData()), sell(Items::painting, 1, 3)},
    };
    for (auto& tier : shepherd)
        addTier(0, 3, tier);

    static ItemListing* fletcher[][2] = {
        {buy(Items::stick, 32),
         new TradeChoice({new ItemAndEmeraldToItem(block(Blocks::gravel), {10, 10}, Items::flint, {10, 10}),
                          sell(Items::arrow, 1, 16)})},
        {buy(Items::flint, 26), sell(Items::bow, 2)},
        {buy(Items::string, 14), sell(Items::crossbow, 3)},
        {buy(Items::feather, 24), enchanted(Items::bow, 7, 21)},
        {buy(block(Blocks::tripwire_hook), 8), enchanted(Items::crossbow, 8, 22)},
    };
    for (auto& tier : fletcher)
        addTier(0, 4, tier);

    static ItemListing* librarian[][2] = {
        {buy(Items::paper, 24), new TradeChoice({new EnchantBookForEmeralds(), sell(block(Blocks::bookshelf), 9)})},
        {buy(Items::book, 4), sell(block(Blocks::lantern), 1)},
        {buy(Items::dye, 5), sell(block(Blocks::glass), 1, 4)},
        {sell(Items::clock, 5), sell(Items::compass, 4)},
        {new EnchantBookForEmeralds(), sell(Items::nameTag, 20)},
    };
    for (auto& tier : librarian)
        addTier(1, 1, tier);

    static ItemListing* cartographer[][2] = {
        {buy(Items::paper, 24), sell(Items::map, 7)},
        {buy(block(Blocks::glass_pane), 11),
         new TreasureMapForEmeralds({13, 13}, LargeFeature::MONUMENT, &MapDecoration::Type::TYPES[15])},
        {buy(Items::compass, 1),
         new TreasureMapForEmeralds({14, 14}, LargeFeature::MANSION, &MapDecoration::Type::TYPES[14])},
        {sell(Items::itemFrame, 7), sell(Items::banner, 3)},
        {sell(Items::bannerStencil, 8, 1, 4), sell(Items::banner, 3, 1, 15)},
    };
    for (auto& tier : cartographer)
        addTier(1, 2, tier);

    static ItemListing* cleric[][2] = {
        {buy(Items::rottenFlesh, 32), sell(Items::redstone, 1, 2)},
        {buy(Items::goldIngot, 3), sell(Items::dye, 1, 1, DyeColor::BLUE->getItemData())},
        {buy(Items::rabbitFoot, 2), sell(block(Blocks::glowstone), 1)},
        {buy(Items::glassBottle, 9), sell(Items::enderPearl, 5)},
        {buy(Items::netherWart, 22), sell(Items::experienceBottle, 3)},
    };
    for (auto& tier : cleric)
        addTier(2, 1, tier);

    static ItemListing* armorer[][2] = {
        {buy(Items::coal, 15), sell(Items::ironHelmet, 5)},
        {buy(Items::ironIngot, 4), sell(Items::chainmailBoots, 1)},
        {buy(Items::lavaBucket, 1), sell(Items::chainmailChestplate, 4)},
        {enchanted(Items::diamondLeggings, 19, 33), enchanted(Items::diamondBoots, 13, 27)},
        {enchanted(Items::diamondHelmet, 13, 27), enchanted(Items::diamondChestplate, 21, 35)},
    };
    for (auto& tier : armorer)
        addTier(3, 1, tier);

    static ItemListing* weaponsmith[][2] = {
        {buy(Items::coal, 15), sell(Items::ironAxe, 3)},
        {buy(Items::ironIngot, 4), enchanted(Items::ironSword, 7, 21)},
        {buy(Items::flint, 24), sell(block(Blocks::bell), 36)},
        {buy(Items::diamond, 1), enchanted(Items::diamondAxe, 17, 31)},
        {buy(Items::diamond, 1), enchanted(Items::diamondSword, 13, 27)},
    };
    for (auto& tier : weaponsmith)
        addTier(3, 2, tier);

    static ItemListing* toolsmith[][2] = {
        {buy(Items::coal, 15), sell(Items::stonePickaxe, 1)},
        {buy(Items::ironIngot, 4), sell(block(Blocks::bell), 36)},
        {buy(Items::flint, 30), enchanted(Items::ironShovel, 7, 21)},
        {buy(Items::diamond, 1), enchanted(Items::diamondShovel, 10, 24)},
        {enchanted(Items::diamondAxe, 17, 31), enchanted(Items::diamondPickaxe, 18, 32)},
    };
    for (auto& tier : toolsmith)
        addTier(3, 3, tier);

    static ItemListing* butcher[][2] = {
        {buy(Items::chicken, 14), sell(Items::rabbitStew, 1)},
        {buy(Items::coal, 15), sell(Items::cookedPorkchop, 1, 5)},
        {buy(Items::mutton, 7), buy(Items::beef, 10)},
        {buy(block(Blocks::dried_kelp_block), 10), sell(Items::cookedBeef, 1, 3)},
        {buy(Items::sweetBerries, 10), sell(Items::cookedChicken, 1, 8)},
    };
    for (auto& tier : butcher)
        addTier(4, 1, tier);

    static ItemListing* leatherworker[][2] = {
        {buy(Items::leather, 6), sell(Items::leatherLeggings, 3)},
        {buy(Items::flint, 26), sell(Items::leatherChestplate, 7)},
        {buy(Items::rabbitHide, 9), sell(Items::leatherBoots, 4)},
        {buy(Items::turtleShellPiece, 4), sell(Items::leatherHorseArmor, 6)},
        {sell(Items::leatherHelmet, 5), sell(Items::saddle, 6)},
    };
    for (auto& tier : leatherworker)
        addTier(4, 2, tier);

    static ItemListing* mason[][2] = {
        {buy(Items::clayBall, 10), sell(Items::brick, 1, 10)},
        {buy(block(Blocks::stone), 20), sell(block(Blocks::stonebrick), 1, 4, 3)},
        {buy(block(Blocks::stone), 16, 1), sell(block(Blocks::stone), 1, 4, 2)},
        {buy(Items::quartz, 12),
         new TradeChoice({sell(block(Blocks::white_glazed_terracotta), 1, 12),
                          sell(block(Blocks::orange_glazed_terracotta), 1, 12),
                          sell(block(Blocks::cyan_glazed_terracotta), 1, 12),
                          sell(block(Blocks::blue_glazed_terracotta), 1, 12),
                          sell(block(Blocks::stained_hardened_clay), 1, 1, 14)})},
        {sell(block(Blocks::quartz_block), 1, 1, 2), sell(block(Blocks::quartz_block), 1)},
    };
    for (auto& tier : mason)
        addTier(MASON, 1, tier);
}

void Villager::addDataWalkers(DataFixerUpper* fixer)
{
    Mob::addDataWalkers(fixer, EntityType::VILLAGER);
    fixer->addWalker(DataFixTypes::eDataFixTypes_Entity,
                     (DataWalker*)new ItemListDataWalker(EntityType::VILLAGER, L"Inventory"));
    fixer->addWalker(DataFixTypes::eDataFixTypes_Entity,
                     (DataWalker*)new UniqueTypeWalker(EntityIO::getKey(EntityType::VILLAGER)));
}

unsigned long long Villager::GetType()
{
    return EntityType::VILLAGER;
}

void Villager::defineSynchedData()
{
    AgableMob::defineSynchedData();
    mEntityData->createDateItem(DATA_PROFESSION, 0);
    mEntityData->createDateItem(DATA_CAREER, 0);
    mEntityData->createDateItem(DATA_TRADING, 0);
    mEntityData->createDateItem(DATA_SLEEP_DIRECTION, 0);
    mEntityData->createDateItem(DATA_TRADE_LEVEL, 0);
    mEntityData->createDateItem(DATA_TRADE_PREVIEW, ItemInstance::EMPTY);
}

int Villager::getProfession()
{
    const int profession = mEntityData->get(DATA_PROFESSION);
    return profession >= 0 && profession <= MASON ? profession : UNEMPLOYED;
}

void Villager::setProfession(int profession)
{
    mEntityData->set(DATA_PROFESSION, profession, true);
}

unsigned int Villager::getCareer()
{
    return mEntityData->get(DATA_CAREER);
}

void Villager::setCareer(int career)
{
    mEntityData->set(DATA_CAREER, career, true);
}

unsigned int Villager::getTrading()
{
    return mEntityData->get(DATA_TRADING);
}

void Villager::setTrading(int trading)
{
    mEntityData->set(DATA_TRADING, trading, true);
}

bool Villager::isTrading()
{
    return mTradingPlayer.lock().get() != NULL;
}

std::wstring Villager::getDisplayName()
{
    if (hasCustomName())
    {
        return getCustomName();
    }

    const wchar_t* name;

    switch (getProfession())
    {
    case 0:
        if (getCareer() == 1)
            name = L10N::GetString(IDS_VILLAGER_FARMER);
        else if (getCareer() == 2)
            name = L10N::GetString(IDS_VILLAGER_FISHERMAN);
        else if (getCareer() == 3)
            name = L10N::GetString(IDS_VILLAGER_SHEPHERD);
        else if (getCareer() == 4)
            name = L10N::GetString(IDS_VILLAGER_FLETCHER);
        else
            name = L10N::GetString(IDS_VILLAGER);
        break;
    case 1:
        if (getCareer() == 1)
            name = L10N::GetString(IDS_VILLAGER_LIBRARIAN);
        else if (getCareer() == 2)
            name = L10N::GetString(IDS_VILLAGER_CARTOGRAPHER);
        else
            name = L10N::GetString(IDS_VILLAGER);
        break;
    case 2:
        name = L10N::GetString(IDS_VILLAGER_CLERIC);
        break;
    case 3:
        if (getCareer() == 1)
            name = L10N::GetString(IDS_VILLAGER_ARMORER);
        else if (getCareer() == 2)
            name = L10N::GetString(IDS_VILLAGER_WEAPONSMITH);
        else if (getCareer() == 3)
            name = L10N::GetString(IDS_VILLAGER_TOOLSMITH);
        else
            name = L10N::GetString(IDS_VILLAGER);
        break;
    case 4:
        if (getCareer() == 1)
            name = L10N::GetString(IDS_VILLAGER_BUTCHER);
        else if (getCareer() == 2)
            name = L10N::GetString(IDS_VILLAGER_LEATHERWORKER);
        else
            name = L10N::GetString(IDS_VILLAGER);
        break;
    case 5:
        name = L10N::GetString(IDS_VILLAGER_NITWIT);
        break;
    case MASON:
        name = L10N::GetString(IDS_LCE_VILLAGER_MASON);
        break;
    default:
        name = L10N::GetString(IDS_VILLAGER);
        break;
    }

    return name;
}

void Villager::setTradingPlayer(const boost::shared_ptr<Player>& player)
{
    setTrading(player.get() != NULL);
    if (player && !getTradePreview()->isEmpty())
        mEntityData->set(DATA_TRADE_PREVIEW, ItemInstance::EMPTY, true);
    mTradingPlayer = boost::weak_ptr<Player>(player);
}

boost::shared_ptr<Player> Villager::getTradingPlayer()
{
    return mTradingPlayer.lock();
}

void Villager::increaseMerchantCareer()
{
    if (mCareerLevel >= 5)
        return;
    std::vector<std::vector<arrayWithLength<ItemListing*> > >& careers = sTrades[getProfession()];

    if (getCareer() != 0 && mCareerLevel != 0)
    {
        mCareerLevel++;
    }
    else
    {
        if (careers.empty())
        {
            setCareer(1);
            mCareerLevel = 1;
        }
        else if (!getCareer() || getCareer() > careers.size())
            setCareer(mRandom->nextInt(careers.size()) + 1);
        mCareerLevel = 1;
    }
    mEntityData->set(DATA_TRADE_LEVEL, mCareerLevel, true);

    prepareTradeOffers(mCareerLevel);
}

void Villager::prepareTradeOffers(int throughLevel)
{
    if (!mOffers)
        mOffers = new MerchantRecipeList();

    const auto& careers = sTrades[getProfession()];
    const unsigned int career = getCareer() - 1;
    while (mGeneratedTradeLevel < std::min(5, throughLevel) && career < careers.size())
    {
        const int level = ++mGeneratedTradeLevel;
        const auto& tiers = careers[career];
        if (level > tiers.size())
            continue;

        const auto& listings = tiers[level - 1];
        auto merchant = boost::static_pointer_cast<Villager>(shared_from_this());
        const int firstNewOffer = mOffers->size();
        for (unsigned int i = 0; i < listings.length; ++i)
            listings.data[i]->addMerchantRecipe(merchant, mOffers, mRandom);

        for (int i = firstNewOffer; i < mOffers->size(); ++i)
        {
            auto* offer = mOffers->at(i);
            const bool buysGoods = offer->getSellItem()->getItem() == Items::emerald;
            constexpr int buyingExperience[] = {2, 10, 20, 30, 30};
            constexpr int sellingExperience[] = {1, 5, 10, 15, 30};
            offer->setTradeProgress(level, buysGoods ? buyingExperience[level - 1] : sellingExperience[level - 1]);
            const int stock = buysGoods ? 16 : offer->getSellItem()->getMaxStackSize() == 1 ? 3 : 12;
            offer->increaseMaxUses(stock - offer->getMaxUses());
        }
    }
    mOffers->setTradeProgress(mCareerLevel, mTradeExperience);
}

MerchantRecipeList* Villager::getOffers(const boost::shared_ptr<Player>& player)
{
    if (mOffers == NULL)
    {
        if (mCareerLevel == 0)
            increaseMerchantCareer();
        else
        {
            mGeneratedTradeLevel = 0;
            prepareTradeOffers(mCareerLevel);
        }
    }

    if (mOffers && !mLevel->mIsClientSide)
    {
        prepareTradeOffers(mCareerLevel + 1);
        auto* hero = player ? player->getEffect(MobEffects::HERO_OF_THE_VILLAGE) : nullptr;
        const auto village = mVillage.lock();
        const int standing = village && player ? village->getStanding(player->getName()) : 0;
        for (auto* offer : *mOffers)
        {
            offer->setHeroDiscount(hero ? hero->getAmplifier() : -1);
            offer->setVillageReputation(standing);
        }
    }
    return mOffers;
}

void Villager::overrideOffers(MerchantRecipeList*)
{
}

void Villager::notifyTrade(MerchantRecipe* recipe)
{
    if (mLevel->mIsClientSide || !mOffers || mOffers->isLocked(recipe) || recipe->isDeprecated())
        return;

    mTradeLocked = true;
    recipe->increaseUses();
    mTradeExperience = std::min(MerchantRecipeList::experienceForLevel(5),
                                mTradeExperience + std::max(1, recipe->getTradeExperience()));
    while (mCareerLevel > 0 && mCareerLevel < 5
           && mTradeExperience >= MerchantRecipeList::experienceForLevel(mCareerLevel + 1))
        increaseMerchantCareer();
    prepareTradeOffers(mCareerLevel + 1);

    if (const auto player = mTradingPlayer.lock())
        if (player->getEffect(MobEffects::HERO_OF_THE_VILLAGE))
            player->awardStat(GenericStats::achievement(eAward_BuyLowSellHigh), GenericStats::param_noArgs());
    mAmbientSoundTime = -getAmbientSoundInterval();
    playSound(SoundEvents::VILLAGER_YES, getSoundVolume(), getVoicePitch());
    int experience = mRandom->nextInt(4) + 3;

    if (recipe->getUses() == 1 || mRandom->nextInt(5) == 0)
    {
        boost::shared_ptr<Player> tradingPlayer = mTradingPlayer.lock();
        if (const auto village = mVillage.lock())
            if (tradingPlayer)
                village->modifyStanding(tradingPlayer->getName(), 1);

        experience += 5;
    }

    if (recipe->getBuyAItem()->getItem() == Items::emerald)
    {
        mRiches += recipe->getBuyAItem()->getCount();
    }

    if (recipe->shouldRewardExp())
    {
        boost::shared_ptr<ExperienceOrb> orb(new ExperienceOrb(mLevel, mX, mY + 0.5, mZ, experience));
        mLevel->addEntity(orb);
    }
}

int Villager::getTradeLevel() const
{
    return mEntityData->get(DATA_TRADE_LEVEL);
}

not_null_ptr<ItemInstance> Villager::getTradePreview() const
{
    return mEntityData->get(DATA_TRADE_PREVIEW);
}

void Villager::updateTradePreview()
{
    if (mTickCount % 10 != 0)
        return;

    auto preview = ItemInstance::EMPTY;
    if (mOffers && isAlive() && !isBaby() && !isSleeping() && !isTrading())
    {
        const auto player = mLevel->getNearestPlayer(mX, mY, mZ, 4.0, false, 2.5);
        if (player)
        {
            const auto held = player->getMainHandItem();
            if (!held->isEmpty())
            {
                std::vector<MerchantRecipe*> matched;
                for (auto* offer : *mOffers)
                    if (!mOffers->isLocked(offer) && !offer->isDeprecated() && held->sameItem(offer->getBuyAItem()))
                        matched.push_back(offer);
                if (!matched.empty())
                    preview = matched[(mTickCount / 40) % matched.size()]->getSellItem();
            }
        }
    }
    if (!ItemInstance::matches(preview, getTradePreview(), true))
        mEntityData->set(DATA_TRADE_PREVIEW, preview->copy(), true);
}

void Villager::updateTrades()
{
    if (isTrading() || !mOffers || mOffers->empty())
        return;

    const auto* work = getWorkDestination();
    const auto now = mLevel->getGameTime();
    if (!work || BlockPos(this).distSqr(*work) > 2 || (mLastRestock >= 0 && now - mLastRestock < 2400))
        return;
    const auto day = mLevel->getDayTime() / 24000;
    if (mRestockDay != day)
    {
        mRestockDay = day;
        mRestocksToday = 0;
    }
    if (mRestocksToday >= 2
        || std::none_of(mOffers->begin(), mOffers->end(), [](MerchantRecipe* recipe) { return recipe->getUses() > 0; }))
        return;
    for (auto* recipe : *mOffers)
        recipe->restock();
    ++mRestocksToday;
    mLastRestock = now;
}

void Villager::tendFarm()
{
    const auto* work = getWorkDestination();
    if (!isFarmer() || !work || BlockPos(this).distSqr(*work) > 2
        || !mLevel->getGameRules()->getBoolean(GameRules::RULE_MOBGRIEFING))
        return;
    auto* composter = dynamic_cast<ComposterBlock*>(mLevel->getBlock(*work));
    if (!composter)
        return;

    int wheat = -1;
    bool canKeepBread = false;
    for (unsigned int slot = 0; slot < mInventory->getContainerSize(); ++slot)
    {
        const auto stack = mInventory->getItem(slot);
        if (stack->isEmpty() || (stack->getItem() == Items::bread && stack->getCount() < stack->getMaxStackSize()))
            canKeepBread = true;
        if (stack->getItem() == Items::wheat && stack->getCount() >= 3)
            wheat = slot;
    }
    if (wheat >= 0 && (canKeepBread || mInventory->getItem(wheat)->getCount() == 3))
    {
        mInventory->removeItem(wheat, 3);
        mInventory->addItem(not_null_ptr<ItemInstance>(new ItemInstance(Items::bread)));
    }

    for (unsigned int slot = 0; slot < mInventory->getContainerSize(); ++slot)
    {
        const auto stack = mInventory->getItem(slot);
        if ((stack->getItem() == Items::wheatSeeds || stack->getItem() == Items::beetrootSeeds)
            && stack->getCount() > 16 && composter->addCompost(mLevel, *work, stack))
        {
            mInventory->removeItem(slot, 1);
            break;
        }
    }
}

void Villager::notifyTradeUpdated(const not_null_ptr<ItemInstance>& item)
{
    if (!mLevel->mIsClientSide && mAmbientSoundTime > 20 - getAmbientSoundInterval())
    {
        mAmbientSoundTime = -getAmbientSoundInterval();

        if (item->isEmpty())
        {
            playSound(SoundEvents::VILLAGER_NO, getSoundVolume(), getVoicePitch());
        }
        else
        {
            playSound(SoundEvents::VILLAGER_YES, getSoundVolume(), getVoicePitch());
        }
    }
}

bool Villager::isAvailableToTrade()
{
    return !mRemoved;
}

Level* Villager::getLevel()
{
    return mLevel;
}

BlockPos Villager::getPosition()
{
    return BlockPos(this);
}

MobGroupData* Villager::finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                         bool randomProfession, const std::wstring&)
{
    data = Mob::finalizeMobSpawn(difficulty, data, L"");

    if (randomProfession)
    {
        setProfession(mLevel->mRandom->nextInt(6) == 5 ? NITWIT : UNEMPLOYED);
    }

    loadPersonalAiGoals();
    if (getProfession() != UNEMPLOYED)
        increaseMerchantCareer();
    return data;
}

MobGroupData* Villager::finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                         const std::wstring& dataString)
{
    return finalizeMobSpawn(difficulty, data, true, dataString);
}

void Villager::setRewardPlayersInVillage()
{
    mRewardPlayersInVillage = true;
}

bool Villager::countFood(int multiplier)
{
    for (unsigned int slot = 0; slot < mInventory->getContainerSize(); slot++)
    {
        not_null_ptr<ItemInstance> item = mInventory->getItem(slot);

        if (!item->isEmpty())
        {
            if ((item->getItem() == Items::bread && item->getCount() >= multiplier * 3)
                || (item->getItem() == Items::potato && item->getCount() >= multiplier * 12)
                || (item->getItem() == Items::carrot && item->getCount() >= multiplier * 12)
                || (item->getItem() == Items::beetroot && item->getCount() >= multiplier * 12))
            {
                return true;
            }

            if (isFarmer() && item->getItem() == Items::wheat && item->getCount() >= multiplier * 9)
            {
                return true;
            }
        }
    }

    return false;
}

bool Villager::hasExcessFood()
{
    return countFood(2);
}

bool Villager::wantsMoreFood()
{
    if (isFarmer())
    {
        return !countFood(5);
    }

    return !countFood(1);
}

SimpleContainer* Villager::getInventory()
{
    return mInventory;
}

void Villager::loadPersonalAiGoals()
{
    if (!isBaby() && isFarmer())
    {
        if (!mHarvestGoal)
        {
            mHarvestGoal = new HarvestFarmBlockGoal(this, 0.6);
            mGoalSelector.addGoal(6, mHarvestGoal, true);
        }
    }
    else if (mHarvestGoal)
    {
        mGoalSelector.removeGoal(mHarvestGoal);
        mHarvestGoal = nullptr;
    }
}

bool Villager::isFarmer()
{
    return getProfession() == 0 && getCareer() <= 1;
}

void Villager::grewUp()
{
    AgableMob::grewUp();
    loadPersonalAiGoals();
}

void Villager::registerAttributes()
{
    Mob::registerAttributes();
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(0.5);
    getAttribute(SharedMonsterAttributes::FOLLOW_RANGE)->setBaseValue(32.0);
}

void Villager::hearBell()
{
    wakeUp();
    mShelterUntil = mLevel->getGameTime() + 300;
    if (getTradingPlayer())
        setTradingPlayer({});
}

bool Villager::isTakingShelter() const
{
    if (auto* level = dynamic_cast<ServerLevel*>(mLevel))
        if (level->getRaids())
            if (auto* raid = level->getRaids()->getNearby(BlockPos(mX,mY,mZ)))
                if (!raid->isOver()) return true;
    return mLevel && mShelterUntil != 0 && mLevel->getGameTime() < mShelterUntil;
}

const BlockPos* Villager::getHomeForShelter()
{
    if (mLevel->mIsClientSide || isPassenger())
        return nullptr;

    const bool evening = mLevel->mDimension->isHasSkyLight() && mLevel->getDayTime() % 24000 >= 12000;
    if (evening || isTakingShelter())
    {
        if (const auto villages = mLevel->getVillages())
        {
            const auto home = villages->getHome(getUUID());
            if (home && mLevel->hasChunkAt(*home))
                return home;
        }
    }

    return nullptr;
}

const BlockPos* Villager::getWorkDestination()
{
    if (mLevel->mIsClientSide || isBaby() || isPassenger() || isTakingShelter()
        || mLevel->getDayTime() % 24000 >= 9000)
        return nullptr;
    const auto villages = mLevel->getVillages();
    const auto* site = villages ? villages->getJobSite(getUUID()) : nullptr;
    return site && mLevel->hasChunkAt(site->pos) ? &site->pos : nullptr;
}

const BlockPos* Villager::getMeetingDestination()
{
    const auto time = mLevel->getDayTime() % 24000;
    if (mLevel->mIsClientSide || isPassenger() || isTakingShelter() || time < 9000 || time >= 12000)
        return nullptr;
    const auto villages = mLevel->getVillages();
    return villages ? villages->getMeetingBell(BlockPos(this)) : nullptr;
}

bool Villager::isSleeping()
{
    return mEntityData->get(DATA_SLEEP_DIRECTION) != 0;
}

bool Villager::isSleepingAt(const BlockPos& pos)
{
    return isSleeping() && mSleepingBed == pos;
}

float Villager::getSleepRotation()
{
    switch (mEntityData->get(DATA_SLEEP_DIRECTION) - 1)
    {
    case 2: return 270.0f;
    case 3: return 90.0f;
    case 5: return 180.0f;
    default: return 0.0f;
    }
}

bool Villager::sleptRecently() const
{
    return mLastSlept >= 0 && mLevel->getGameTime() - mLastSlept <= 24000;
}

void Villager::wakeUp()
{
    if (!isSleeping())
        return;
    mEntityData->set(DATA_SLEEP_DIRECTION, 0, true);
    if (!mLevel->mIsClientSide && mLevel->hasChunkAt(mSleepingBed, false)
        && mLevel->getBlock(mSleepingBed) == Blocks::bed)
    {
        std::unique_ptr<BlockPos> stand(BedBlock::findStandUpPosition(mLevel, mSleepingBed, 0));
        if (stand)
            setPos(stand->x + 0.5, stand->y, stand->z + 0.5);
    }
}

void Villager::travel(float strafe, float vertical, float forward)
{
    if (!isSleeping())
        AgableMob::travel(strafe, vertical, forward);
}

void Villager::onSyncedDataUpdated(const EntityDataAccessor_Base* accessor)
{
    AgableMob::onSyncedDataUpdated(accessor);
    if (accessor == DATA_SLEEP_DIRECTION)
        setSize(0.6f, isSleeping() ? 0.2f : 1.95f);
}

void Villager::updateSchedule()
{
    const auto* home = getHomeForShelter();
    const bool canSleep = home && !isTakingShelter() && !isTrading() && !isInWater()
        && mLevel->mDimension->isHasSkyLight() && mLevel->getDayTime() % 24000 >= 12000;
    if (!canSleep || (isSleeping() && mSleepingBed != *home))
    {
        wakeUp();
        return;
    }
    const auto* state = mLevel->getBlockState(*home);
    if (state->getBlock() != Blocks::bed || state->getValue(BedBlock::OCCUPIED))
    {
        wakeUp();
        return;
    }
    if (isSleeping() || BlockPos(this).distSqr(*home) > 2)
        return;
    mSleepingBed = *home;
    mLastSlept = mLevel->getGameTime();
    getNavigation()->stop();
    mXd = mYd = mZd = 0;
    mEntityData->set(DATA_SLEEP_DIRECTION, state->getValue(BedBlock::FACING)->get3DDataValue() + 1, true);
    setPos(home->x + 0.5, home->y + 0.56, home->z + 0.5);
}

void Villager::updateJobSite()
{
    if (isBaby() || getProfession() == NITWIT || isTrading() || isPassenger())
        return;
    const auto villages = mLevel->getVillages();
    const auto* site = villages->getJobSite(getUUID());
    if (!site)
    {
        villages->releaseJobSite(getUUID());
        if (mLevel->getDayTime() % 24000 < 12000 && !isTakingShelter())
        {
            const Villages::Job current{getProfession(), static_cast<int>(getCareer())};
            site = villages->findOrClaimJobSite(getUUID(), BlockPos(this), mTradeLocked ? &current : nullptr);
        }
    }

    if (mTradeLocked || (!site && !mProfessionFromJobSite))
        return;
    mProfessionFromJobSite = site != nullptr;
    const auto job = site ? site->job : Villages::Job{UNEMPLOYED, 0};
    if (getProfession() == job.profession && getCareer() == static_cast<unsigned int>(job.career))
        return;
    setProfession(job.profession);
    setCareer(job.career);
    delete mOffers;
    mOffers = nullptr;
    mCareerLevel = 0;
    mEntityData->set(DATA_TRADE_LEVEL, 0, true);
    mTradeExperience = 0;
    mGeneratedTradeLevel = 0;
    mRestocksToday = 0;
    mRestockDay = -1;
    mLastRestock = -1;
    if (site)
        increaseMerchantCareer();
    loadPersonalAiGoals();
}

void Villager::registerGoals()
{
    mGoalSelector.addGoal(3, new GiveRaidHeroGiftGoal(*this), true);
    mGoalSelector.addGoal(0, new FloatGoal(this), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(Zombie), 8.0f, 0.6, 0.6), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(EvocationIllager), 12.0f, 0.8, 0.8), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(VindicationIllager), 8.0f, 0.8, 0.8), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(Pillager), 12.0f, 0.8, 0.8), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(Vex), 8.0f, 0.6, 0.6), true);
    mGoalSelector.addGoal(1, new TradeWithPlayerGoal(this), true);
    mGoalSelector.addGoal(1, new LookAtTradingPlayerGoal(this), true);
    mGoalSelector.addGoal(2, new PanicGoal(this, 0.6), true);
    mGoalSelector.addGoal(2, new VillagerDestinationGoal(this, VillagerDestinationGoal::Destination::Home), true);
    mGoalSelector.addGoal(2, new MoveIndoorsGoal(this), true);
    mGoalSelector.addGoal(3, new RestrictOpenDoorGoal(this), true);
    mGoalSelector.addGoal(4, new OpenDoorGoal(this, true), true);
    mGoalSelector.addGoal(5, new MoveTowardsRestrictionGoal(this, 0.6), true);
    mGoalSelector.addGoal(6, new MakeLoveGoal(this), true);
    mGoalSelector.addGoal(7, new VillagerDestinationGoal(this, VillagerDestinationGoal::Destination::Work), true);
    mGoalSelector.addGoal(7, new VillagerDestinationGoal(this, VillagerDestinationGoal::Destination::Meeting), true);
    mGoalSelector.addGoal(7, new TakeFlowerGoal(this), true);
    mGoalSelector.addGoal(8, new PlayGoal(this, 0.32), true);
    mGoalSelector.addGoal(9, new InteractGoal(this, typeid(Player), 3.0f, 1.0f), true);
    mGoalSelector.addGoal(9, new VillagerInteractGoal(this), true);
    mGoalSelector.addGoal(9, new WaterAvoidingRandomStrollGoal(this, 0.6), true);
    mGoalSelector.addGoal(10, new LookAtPlayerGoal(this, typeid(Mob), 8.0f), true);
}

void Villager::customServerAiStep()
{
    tickVillagerRaidCelebration(*this);
    updateSchedule();
    if (--mRandomTickDivider < 1)
    {
        BlockPos pos(shared_from_this());
        const auto villages = mLevel->getVillages();
        villages->findOrClaimHome(getUUID(), pos);
        updateJobSite();
        tendFarm();
        villages->queryUpdateAround(pos);
        mRandomTickDivider = mRandom->nextInt(50) + 70;
        boost::shared_ptr<Village> village = villages->getClosestVillage(pos, 32);
        mVillage = village;

        if (village == NULL)
        {
            clearRestriction();
        }
        else
        {
            restrictTo(*village->getCenter(), static_cast<int>(static_cast<float>(village->getRadius())));

            if (mRewardPlayersInVillage)
            {
                mRewardPlayersInVillage = false;
                village->rewardAllPlayers(5);
            }
        }
    }

    if (!isTrading() && getProfession() != UNEMPLOYED && mOffers == NULL && (getCareer() == 0 || mCareerLevel == 0))
    {
        increaseMerchantCareer();
    }

    updateTrades();
    updateTradePreview();

    Mob::customServerAiStep();
}

bool Villager::mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand)
{
    not_null_ptr<ItemInstance> item = player->getItemInHand(hand);

    if (item->getItem() == Items::nameTag)
    {
        item->interactEnemy(player, boost::static_pointer_cast<LivingEntity>(shared_from_this()), hand);
        return true;
    }

    if (holdingSpawnEggOfClass(item, GetType()) || !isAlive() || isTrading() || isBaby()
        || getProfession() == UNEMPLOYED || getProfession() == NITWIT)
    {
        return AgableMob::mobInteract(player, hand);
    }

    if (!mLevel->mIsClientSide)
    {
        if (mOffers == NULL || !mOffers->empty())
        {
            setTradingPlayer(player);

            if (mOffers == NULL)
            {
                increaseMerchantCareer();
            }

            boost::shared_ptr<Merchant> merchant = boost::static_pointer_cast<Villager>(shared_from_this());

            player->openTrading(merchant, getDisplayName(), getId());
        }
        else
        {
            return AgableMob::mobInteract(player, hand);
        }
    }

    return true;
}

bool Villager::hurt(const DamageSource* source, float damage)
{
    wakeUp();
    if (source != NULL && dynamic_cast<const EntityDamageSource*>(source) != NULL)
    {
        boost::shared_ptr<Entity> attacker = source->getDirectEntity();

        if (eTYPE_DERIVED_FROM(EntityType::PLAYER, attacker->GetType())
            && !boost::static_pointer_cast<Player>(attacker)->isAllowedToAttackAnimals())
        {
            return false;
        }

        if (attacker != NULL && eTYPE_DERIVED_FROM(EntityType::ARROW, attacker->GetType()))
        {
            boost::shared_ptr<Arrow> arrow = boost::static_pointer_cast<Arrow>(attacker);

            if (arrow->mOwner != NULL && eTYPE_DERIVED_FROM(EntityType::PLAYER, arrow->mOwner->GetType())
                && !boost::static_pointer_cast<Player>(arrow->mOwner)->isAllowedToAttackAnimals())
            {
                return false;
            }
        }
    }

    return LivingEntity::hurt(source, damage);
}

unsigned int Villager::GetInteractTooltip(const EntityTooltipDataHolder& holder)
{
    int itemId = Item::getId(holder.m_pItemInstance->getItem());

    switch (itemId)
    {
    case ItemId::NAME_TAG:
    {
        if (!holder.m_pItemInstance->hasCustomHoverName())
        {
            return IDS_NONE;
        }

        int differs = !hasCustomName() || holder.m_pItemInstance->getHoverName().compare(getCustomName()) != 0;

        return differs ? IDS_TOOLTIPS_NAME : IDS_NONE;
    }
    case ItemId::SPAWN_EGG:
        if (SpawnEggItem::fjGetType(holder.m_pItemInstance) == EntityType::VILLAGER)
        {
            return IDS_TOOLTIPS_PLACE;
        }
        break;
    }

    if (!isBaby() && getProfession() != NITWIT && getProfession() != UNEMPLOYED)
    {
        return IDS_TOOLTIPS_TRADE;
    }

    return IDS_NONE;
}

bool Villager::HideUseTooltips()
{
    return getProfession() == NITWIT || getProfession() == UNEMPLOYED;
}

void Villager::addAdditonalSaveData(CompoundTag* tag)
{
    AgableMob::addAdditonalSaveData(tag);
    tag->putInt(std::wstring(L"Profession"), getProfession());
    tag->putInt(std::wstring(L"Riches"), mRiches);
    tag->putInt(std::wstring(L"Career"), getCareer());
    tag->putInt(std::wstring(L"CareerLevel"), mCareerLevel);
    tag->putBoolean(L"TradeLocked", mTradeLocked);
    tag->putBoolean(L"ProfessionFromJobSite", mProfessionFromJobSite);
    tag->putInt(L"TradeExperience", mTradeExperience);
    tag->putInt(L"GeneratedTradeLevel", mGeneratedTradeLevel);
    tag->putLong(L"LastRestock", mLastRestock);
    tag->putLong(L"RestockDay", mRestockDay);
    tag->putInt(L"RestocksToday", mRestocksToday);
    tag->putLong(L"LastSlept", mLastSlept);
    tag->putBoolean(std::wstring(L"Willing"), mWillingToBreed);

    if (mOffers != NULL)
    {
        tag->put(std::wstring(L"Offers"), mOffers->createTag());
    }

    ListTag* inventory = new ListTag();

    for (unsigned int slot = 0; slot < mInventory->getContainerSize(); slot++)
    {
        not_null_ptr<ItemInstance> item = mInventory->getItem(slot);

        if (!item->isEmpty())
        {
            inventory->add(item->save(new CompoundTag()));
        }
    }

    tag->put(std::wstring(L"Inventory"), inventory);
}

void Villager::readAdditionalSaveData(CompoundTag* tag)
{
    AgableMob::readAdditionalSaveData(tag);
    auto* followRange = getAttribute(SharedMonsterAttributes::FOLLOW_RANGE);
    followRange->setBaseValue(std::max(32.0, followRange->getBaseValue()));
    int profession = tag->getInt(L"Profession");
    if (!tag->contains(L"ProfessionFromJobSite") && tag->contains(L"VillagerData", Tag::TAG_COMPOUND)
        && tag->getCompound(L"VillagerData")->getString(L"profession") == L"minecraft:none")
        profession = UNEMPLOYED;
    setProfession(profession);
    mRiches = tag->getInt(L"Riches");
    setCareer(tag->getInt(L"Career"));
    mCareerLevel = std::max(0, std::min(5, tag->getInt(L"CareerLevel")));
    mEntityData->set(DATA_TRADE_LEVEL, mCareerLevel, true);
    if (tag->contains(L"GeneratedTradeLevel"))
    {
        mGeneratedTradeLevel = Mth::clamp(tag->getInt(L"GeneratedTradeLevel"), mCareerLevel, 5);
        mTradeExperience =
            Mth::clamp(tag->getInt(L"TradeExperience"), 0, MerchantRecipeList::experienceForLevel(5));
    }
    else
    {
        mGeneratedTradeLevel = mCareerLevel;
        const int oldLevelThresholds[] = {0, 0, 3, 8, 15, 25, 26};
        const int start = MerchantRecipeList::experienceForLevel(mCareerLevel);
        const int end = MerchantRecipeList::experienceForLevel(mCareerLevel + 1);
        const int oldStart = oldLevelThresholds[mCareerLevel];
        const int oldRange = oldLevelThresholds[mCareerLevel + 1] - oldStart;
        const int previousExperience = Mth::clamp(tag->getInt(L"TradeExperience") - oldStart, 0, oldRange);
        mTradeExperience = oldRange > 0 ? start + previousExperience * (end - start) / oldRange : start;
    }
    mLastRestock = tag->contains(L"LastRestock") ? tag->getLong(L"LastRestock") : -1;
    mRestockDay = tag->contains(L"RestockDay") ? tag->getLong(L"RestockDay") : -1;
    mRestocksToday = std::max(0, std::min(2, tag->getInt(L"RestocksToday")));
    mLastSlept = tag->contains(L"LastSlept") ? tag->getLong(L"LastSlept") : -1;
    mProfessionFromJobSite = tag->getBoolean(L"ProfessionFromJobSite");
    mTradeLocked = tag->getBoolean(L"TradeLocked") || mRiches > 0 || mCareerLevel > 1;
    mWillingToBreed = tag->getBoolean(L"Willing");

    if (tag->contains(L"Offers"))
    {
        CompoundTag* offers = tag->getCompound(L"Offers");
        delete mOffers;
        mOffers = new MerchantRecipeList(offers);
        mOffers->setTradeProgress(mCareerLevel, mTradeExperience);
        if (!tag->contains(L"GeneratedTradeLevel"))
        {
            for (auto* recipe : *mOffers)
                recipe->setTradeProgress(std::max(1, mCareerLevel), std::max(2, mCareerLevel * 5));
        }
        mTradeLocked = mTradeLocked || std::any_of(mOffers->begin(), mOffers->end(), [](MerchantRecipe* recipe) {
            return recipe->getUses() > 0;
        });
    }

    ListTag* inventory = tag->fjSafeGetListTag(L"Inventory", 10);

    for (int index = 0; index < inventory->size(); index++)
    {
        not_null_ptr<ItemInstance> item(new ItemInstance(inventory->getCompound(index)));

        if (!item->isEmpty())
        {
            mInventory->addItem(item);
        }
    }

    setCanPickUpLoot(true);
    loadPersonalAiGoals();
}

bool Villager::removeWhenFarAway()
{
    return false;
}

const SoundEvent* Villager::getAmbientSound()
{
    if (isTrading())
    {
        return SoundEvents::VILLAGER_TRADING;
    }

    return SoundEvents::VILLAGER_AMBIENT;
}

const SoundEvent* Villager::getHurtSound(const DamageSource*)
{
    return SoundEvents::VILLAGER_HURT;
}

const SoundEvent* Villager::getDeathSound()
{
    return SoundEvents::VILLAGER_DEATH;
}

bool Villager::isInLove()
{
    return mInLove;
}

void Villager::setInLove(bool inLove)
{
    mInLove = inLove;
}

void Villager::setChasing(bool chasing)
{
    mChasing = chasing;
}

bool Villager::isChasing()
{
    return mChasing;
}

void Villager::setLastHurtByMob(boost::shared_ptr<LivingEntity> attacker)
{
    LivingEntity::setLastHurtByMob(attacker);

    boost::shared_ptr<Village> village = mVillage.lock();

    if (village != NULL && attacker != NULL)
    {
        village->addAggressor(attacker);

        if (eTYPE_DERIVED_FROM(EntityType::PLAYER, attacker->GetType()))
        {
            int amount = -1;

            if (isBaby())
            {
                amount = -3;
            }

            village->modifyStanding(attacker->getName(), amount);

            if (isAlive())
            {
                mLevel->broadcastEntityEvent(shared_from_this(), EntityEvent::VILLAGER_ANGRY, 0);
            }
        }
    }
}

void Villager::die(const DamageSource* source)
{
    wakeUp();
    if (const auto villages = mLevel->getVillages())
    {
        villages->releaseHome(getUUID());
        villages->releaseJobSite(getUUID());
    }
    boost::shared_ptr<Village> village = mVillage.lock();

    if (village != NULL)
    {
        boost::shared_ptr<Entity> killer = source->getEntity();

        if (killer != NULL)
        {
            if (eTYPE_DERIVED_FROM(EntityType::PLAYER, killer->GetType()))
            {
                village->modifyStanding(killer->getName(), -2);
            }
            else if (eTYPE_DERIVED_FROM(EntityType::ENEMY, killer->GetType()))
            {
                village->resetNoBreedTimer();
            }
        }
        else
        {
            boost::shared_ptr<Player> player = mLevel->getNearestPlayer(shared_from_this(), 16.0, -1.0);

            if (player != NULL)
            {
                village->resetNoBreedTimer();
            }
        }
    }

    LivingEntity::die(source);
}

bool Villager::hasEnoughFoodToBreed()
{
    return countFood(1);
}

namespace
{
int breedingFoodPoints(Item* item)
{
    if (item == Items::bread)
        return 4;
    return item == Items::potato || item == Items::carrot || item == Items::beetroot ? 1 : 0;
}
}

bool Villager::hasBreedingFood()
{
    int points = 0;
    for (unsigned int i = 0; i < mInventory->getContainerSize(); ++i)
    {
        const auto item = mInventory->getItem(i);
        points += item->getCount() * breedingFoodPoints(item->getItem());
    }
    return points >= 12;
}

void Villager::consumeBreedingFood()
{
    int remaining = 12;
    for (unsigned int i = 0; i < mInventory->getContainerSize() && remaining > 0; ++i)
    {
        const auto item = mInventory->getItem(i);
        const int value = breedingFoodPoints(item->getItem());
        if (value == 0 || item->isEmpty())
            continue;
        const int consumed = std::min(item->getCount(), (remaining + value - 1) / value);
        mInventory->removeItem(i, consumed);
        remaining -= consumed * value;
    }
}

bool Villager::isWillingToBreed(bool consumeFood)
{
    if (!mWillingToBreed && consumeFood && hasEnoughFoodToBreed())
    {
        for (unsigned int slot = 0; slot < mInventory->getContainerSize(); slot++)
        {
            not_null_ptr<ItemInstance> item = mInventory->getItem(slot);

            if (!item->isEmpty())
            {
                if (item->getItem() == Items::bread && item->getCount() >= 3)
                {
                    mInventory->removeItem(slot, 3);
                }
                else if ((item->getItem() == Items::potato || item->getItem() == Items::carrot)
                         && item->getCount() >= 12)
                {
                    mInventory->removeItem(slot, 12);
                }
                else
                {
                    continue;
                }

                mLevel->broadcastEntityEvent(shared_from_this(), EntityEvent::IN_LOVE_HEARTS, 0);
                mWillingToBreed = true;
                break;
            }
        }
    }

    return mWillingToBreed;
}

void Villager::setWillingToBreed(bool willing)
{
    mWillingToBreed = willing;
}

ResourceLocation* Villager::getDefaultLootTable()
{
    return &BuiltInLootTables::VILLAGER;
}

float Villager::getEyeHeight()
{
    if (isBaby())
    {
        return 0.81f;
    }

    return 1.62f;
}

void Villager::addParticlesAroundSelf(const ParticleType* particle)
{
    for (int i = 0; i < 5; i++)
    {
        double xd = mRandom->nextGaussian() * 0.02;
        double yd = mRandom->nextGaussian() * 0.02;
        double zd = mRandom->nextGaussian() * 0.02;
        arrayWithLength<int> params;
        float xo = mRandom->nextFloat();
        float yo = mRandom->nextFloat();
        float zo = mRandom->nextFloat();
        mLevel->addParticle(particle, mX + (double)(xo * mBbWidth * 2.0f) - (double)mBbWidth,
                            mY + 1.0 + (double)(yo * mBbHeight), mZ + (double)(zo * mBbWidth * 2.0f) - (double)mBbWidth,
                            xd, yd, zd, params);
    }
}

void Villager::handleEntityEvent(unsigned char event, int data)
{
    switch (event)
    {
    case EntityEvent::LOVE_HEARTS:
        addParticlesAroundSelf(PARTICLE_HEART);
        break;
    case EntityEvent::VILLAGER_ANGRY:
        addParticlesAroundSelf(PARTICLE_ANGRY_VILLAGER);
        break;
    case EntityEvent::VILLAGER_HAPPY:
        addParticlesAroundSelf(PARTICLE_HAPPY_VILLAGER);
        break;
    default:
        Mob::handleEntityEvent(event, data);
        break;
    }
}

boost::shared_ptr<AgableMob> Villager::getBreedOffspring(boost::shared_ptr<AgableMob>)
{
    boost::shared_ptr<Villager> child(new Villager(mLevel));
    child->finalizeMobSpawn(mLevel->getCurrentDifficultyAt(BlockPos(child)), NULL, false, L"");
    return child;
}

bool Villager::canBeLeashed(const boost::shared_ptr<Player>&)
{
    return false;
}

void Villager::thunderHit(const LightningBolt*)
{
    if (mLevel->mIsClientSide)
    {
        return;
    }

    boost::shared_ptr<Witch> witch(new Witch(mLevel));
    witch->moveTo(mX, mY, mZ, mYRot, mXRot);
    witch->finalizeMobSpawn(mLevel->getCurrentDifficultyAt(BlockPos(witch)), NULL, L"");
    witch->setNoAi(isNoAi());

    if (hasCustomName())
    {
        witch->setCustomName(getCustomName(), getOwnerUUID(), wasNamedByRestrictedPlayer());
        witch->setCustomNameVisible(isCustomNameVisible());
    }

    mLevel->addEntity(witch);
    if (const auto villages = mLevel->getVillages())
    {
        villages->releaseHome(getUUID());
        villages->releaseJobSite(getUUID());
    }
    remove();
}

boost::shared_ptr<Entity> Villager::changeDimension(int dimension)
{
    const auto villages = mLevel->getVillages();
    auto moved = AgableMob::changeDimension(dimension);
    if (moved && moved->mLevel != mLevel && villages)
    {
        villages->releaseHome(getUUID());
        villages->releaseJobSite(getUUID());
    }
    return moved;
}

bool Villager::wantsItem(Item* item)
{
    return item == Items::bread || item == Items::potato || item == Items::carrot || item == Items::wheat
           || item == Items::wheatSeeds || item == Items::beetroot || item == Items::beetrootSeeds;
}

void Villager::pickUpItem(boost::shared_ptr<ItemEntity> itemEntity)
{
    not_null_ptr<ItemInstance> item = itemEntity->getItem();

    if (wantsItem(item->getItem()))
    {
        not_null_ptr<ItemInstance> remainder = mInventory->addItem(item);

        if (remainder->isEmpty())
        {
            itemEntity->remove();
        }
        else
        {
            item->setCount(remainder->getCount());
        }
    }
}

bool Villager::hasFarmSeeds()
{
    for (unsigned int slot = 0; slot < mInventory->getContainerSize(); slot++)
    {
        not_null_ptr<ItemInstance> item = mInventory->getItem(slot);

        if (!item->isEmpty()
            && (item->getItem() == Items::wheatSeeds || item->getItem() == Items::potato
                || item->getItem() == Items::carrot || item->getItem() == Items::beetrootSeeds))
        {
            return true;
        }
    }

    return false;
}

bool Villager::setSlot(int slot, const not_null_ptr<ItemInstance>& item)
{
    if (Mob::setSlot(slot, item))
    {
        return true;
    }

    int index = slot - 300;

    if (index >= 0 && index < mInventory->getContainerSize())
    {
        mInventory->setItem(index, item);
        return true;
    }

    return false;
}
