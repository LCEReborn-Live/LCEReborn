#include "game/world/generation/WanderingTrader.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "game/core/DifficultyInstance.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Pillager.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/entity/ai/AvoidEntityGoal.hpp"
#include "game/entity/ai/FloatGoal.hpp"
#include "game/entity/ai/LookControl.hpp"
#include "game/entity/ai/PanicGoal.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/ai/TargetGoal.hpp"
#include "game/entity/ai/WaterAvoidingRandomStrollGoal.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/entity/mob/Zombie.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/Potions.hpp"
#include "game/item/PotionUtils.hpp"
#include "game/item/MerchantRecipe.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/ui/EntityTooltipDataHolder.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/WorldBorder.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/LookAtPlayerGoal.hpp"
#include "game/world/generation/Player.hpp"

SoundEvent* WanderingTrader::sAmbientSound = nullptr;
SoundEvent* WanderingTrader::sTradingSound = nullptr;
SoundEvent* WanderingTrader::sTradeYesSound = nullptr;
SoundEvent* WanderingTrader::sTradeNoSound = nullptr;
SoundEvent* WanderingTrader::sHurtSound = nullptr;
SoundEvent* WanderingTrader::sDeathSound = nullptr;
SoundEvent* WanderingTrader::sDrinkPotionSound = nullptr;
SoundEvent* WanderingTrader::sDrinkMilkSound = nullptr;
SoundEvent* WanderingTrader::sDisappearedSound = nullptr;
SoundEvent* WanderingTrader::sReappearedSound = nullptr;

namespace
{
class DefendTraderGoal : public TargetGoal
{
public:
    explicit DefendTraderGoal(TraderLlama* llama) : TargetGoal(llama, true)
    {
        setRequiredControlFlags(1);
    }

    bool canUse() override
    {
        const auto trader = boost::dynamic_pointer_cast<WanderingTrader>(mMob->getLeashHolder());
        if (!trader || !trader->isAlive() || trader->mHurtByPlayerOnPeaceful || trader->getLastHurtByMobTimestamp() == mLastAttack)
            return false;
        const auto attacker = trader->getLastHurtByMob();
        if (!canAttack(attacker, false))
            return false;
        mLastAttack = trader->getLastHurtByMobTimestamp();
        mTarget = attacker;
        return true;
    }

    void start() override
    {
        mMob->setTarget(mTarget.lock());
        TargetGoal::start();
    }

private:
    int mLastAttack = 0;
};

struct Offer
{
    Item* item;
    int count;
    int data;
    int emeralds;
    int stock;
};

template <std::size_t N>
void addOffers(MerchantRecipeList& recipes, std::array<Offer, N> pool, Random& random, int count)
{
    for (int i = 0; i < count; ++i)
    {
        std::swap(pool[i], pool[i + random.nextInt(static_cast<int>(N) - i)]);
        const auto& offer = pool[i];
        recipes.push_back(new MerchantRecipe(
            not_null_ptr<ItemInstance>(new ItemInstance(Items::emerald, offer.emeralds)), ItemInstance::EMPTY,
            not_null_ptr<ItemInstance>(new ItemInstance(offer.item, offer.count, offer.data)), 0, offer.stock));
    }
}

bool findSurface(Level& level, const BlockPos& near, BlockPos& surface)
{
    if (!level.getWorldBorder()->isWithinBounds(near) || !level.hasChunkAt(near, false))
        return false;

    surface = level.getTopSolidBlockPos(near);
    if (std::abs(surface.y - near.y) > 6 || !level.isEmptyBlock(surface) || !level.isEmptyBlock(surface.above()))
        return false;

    const auto* floor = level.getBlockState(surface.below());
    return floor->getBlock()->isSolidBlockingCube(floor);
}
}

WanderingTrader::WanderingTrader(Level* level) : PathfinderMob(level)
{
    fjDerivedCtorCalls();
    setSize(0.6f, 1.95f);
    getAttribute(SharedMonsterAttributes::MOVEMENT_SPEED)->setBaseValue(0.5);
}

WanderingTrader::~WanderingTrader() = default;

Entity* WanderingTrader::create(Level* level)
{
    return new WanderingTrader(level);
}

unsigned long long WanderingTrader::GetType()
{
    return EntityType::WANDERING_TRADER;
}

bool WanderingTrader::spawnVisit(Level& level, const BlockPos& origin, long long expiresAt)
{
    if (level.mIsClientSide)
        return false;

    for (int attempt = 0; attempt < 12; ++attempt)
    {
        BlockPos spot;
        if (!findSurface(level, origin.offset(level.mRandom->nextInt(17) - 8, 0, level.mRandom->nextInt(17) - 8), spot))
            continue;

        std::array<BlockPos, 2> escortSpots;
        int count = 0;
        const BlockPos offsets[] = {BlockPos(2, 0, 0), BlockPos(-2, 0, 0), BlockPos(0, 0, 2), BlockPos(0, 0, -2)};
        for (const auto& offset : offsets)
        {
            BlockPos escort;
            if (findSurface(level, spot.offset(offset), escort) && std::abs(escort.y - spot.y) <= 1)
            {
                escortSpots[count++] = escort;
                if (count == static_cast<int>(escortSpots.size()))
                    break;
            }
        }
        if (count != static_cast<int>(escortSpots.size()))
            continue;

        auto trader = boost::static_pointer_cast<WanderingTrader>(EntityIO::newEntity(EntityType::WANDERING_TRADER, &level));
        trader->moveTo(spot.x + 0.5, spot.y, spot.z + 0.5, level.mRandom->nextFloat() * 360.0f, 0.0f);
        trader->mExpiresAt = expiresAt;
        trader->restrictTo(origin, 16);
        if (!level.isUnobstructed(trader->getBoundingBox()))
            continue;

        std::array<boost::shared_ptr<TraderLlama>, 2> escorts;
        for (std::size_t i = 0; i < escorts.size(); ++i)
        {
            escorts[i] = boost::static_pointer_cast<TraderLlama>(EntityIO::newEntity(EntityType::TRADER_LLAMA, &level));
            const auto& pos = escortSpots[i];
            escorts[i]->moveTo(pos.x + 0.5, pos.y, pos.z + 0.5, trader->mYRot, 0.0f);
        }
        if (!level.isUnobstructed(escorts[0]->getBoundingBox()) || !level.isUnobstructed(escorts[1]->getBoundingBox()))
            continue;

        delete trader->finalizeMobSpawn(level.getCurrentDifficultyAt(spot), nullptr, L"");
        if (!level.addEntity(trader))
            continue;
        bool added = true;
        for (std::size_t i = 0; i < escorts.size(); ++i)
        {
            auto& llama = escorts[i];
            delete llama->finalizeMobSpawn(level.getCurrentDifficultyAt(escortSpots[i]), nullptr, L"");
            llama->setAge(0);
            if (!level.addEntity(llama))
            {
                added = false;
                break;
            }
            llama->joinVisit(trader, expiresAt);
        }
        if (added)
            return true;

        for (const auto& llama : escorts)
        {
            if (llama->isLeashed())
            {
                llama->dropLeash(true, false);
                llama->remove();
            }
        }
        trader->remove();
    }
    return false;
}

void WanderingTrader::registerGoals()
{
    mGoalSelector.addGoal(0, new FloatGoal(this), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(Zombie), 8.0f, 0.5, 0.5), true);
    mGoalSelector.addGoal(1, new AvoidEntityGoal(this, typeid(Pillager), 12.0f, 0.5, 0.5), true);
    mGoalSelector.addGoal(1, new PanicGoal(this, 0.5), true);
    mGoalSelector.addGoal(2, new WaterAvoidingRandomStrollGoal(this, 0.35), true);
    mGoalSelector.addGoal(3, new LookAtPlayerGoal(this, typeid(Player), 8.0f), true);
}

void WanderingTrader::tick()
{
    if (!mLevel->mIsClientSide)
    {
        if (mTradeSoundCooldown > 0)
            --mTradeSoundCooldown;
        if (!isAvailableToTrade())
            setTradingPlayer({});
        if (mExpiresAt > 0 && mLevel->getGameTime() >= mExpiresAt)
        {
            setTradingPlayer({});
            playSound(sDisappearedSound, getSoundVolume(), getVoicePitch());
            remove();
            return;
        }
    }
    PathfinderMob::tick();
}

void WanderingTrader::customServerAiStep()
{
    PathfinderMob::customServerAiStep();
    if (auto customer = getTradingPlayer())
    {
        getNavigation()->stop();
        getLookControl()->setLookAt(customer, 10.0f, 40.0f);
        return;
    }

    if (isUsingItem())
    {
        getNavigation()->stop();
        return;
    }

    const bool day = mLevel->isDay();
    const bool inDanger = getLastHurtByMob() && mTickCount - getLastHurtByMobTimestamp() < 40;
    // Dawn ends night camouflage. A defensive potion drunk during the day
    // must not be cancelled as soon as the recent-damage timer runs out.
    if (!day && hasEffect(MobEffects::INVISIBILITY))
        mUsedNightPotion = true;
    if (day && mUsedNightPotion && !inDanger && hasEffect(MobEffects::INVISIBILITY))
    {
        setItemInHand(InteractionHand::eInteractionHand_MainHand,
                      not_null_ptr<ItemInstance>(new ItemInstance(Items::milkBucket)));
        startUsingItem(InteractionHand::eInteractionHand_MainHand);
        playSound(sDrinkMilkSound, getSoundVolume(), getVoicePitch());
    }
    else if ((!day || inDanger) && !hasEffect(MobEffects::INVISIBILITY))
    {
        setItemInHand(InteractionHand::eInteractionHand_MainHand,
                      PotionUtils::setPotion(not_null_ptr<ItemInstance>(new ItemInstance(Items::potion)),
                                             Potions::INVISIBILITY));
        startUsingItem(InteractionHand::eInteractionHand_MainHand);
        playSound(sDrinkPotionSound, getSoundVolume(), getVoicePitch());
    }
}

void WanderingTrader::completeUsingItem()
{
    if (!isUsingItem() || getUseItem()->isEmpty())
        return;

    const auto item = getUseItem();
    const bool milk = item->getItem() == Items::milkBucket;
    // The mob uses the same duration and effects as the actual drink item.
    // ItemInstance's legacy finishUsingItem wrapper only accepts a Player.
    item->getItem()->finishUsingItem(item, mLevel,
                                    boost::static_pointer_cast<LivingEntity>(shared_from_this()));
    mUsedNightPotion = !milk && !mLevel->isDay();
    stopUsingItem();
    setItemInHand(InteractionHand::eInteractionHand_MainHand, ItemInstance::EMPTY);
    playSound(milk ? sReappearedSound : sDisappearedSound, getSoundVolume(), getVoicePitch());
}

bool WanderingTrader::mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand)
{
    const auto item = player->getItemInHand(hand);
    if (item->getItem() == Items::nameTag)
    {
        item->interactEnemy(player, boost::static_pointer_cast<LivingEntity>(shared_from_this()), hand);
        return true;
    }
    if (!isAvailableToTrade() || getTradingPlayer() || item->getItem() == Items::spawnEgg)
        return PathfinderMob::mobInteract(player, hand);

    if (!mLevel->mIsClientSide)
    {
        setTradingPlayer(player);
        auto merchant = boost::static_pointer_cast<WanderingTrader>(shared_from_this());
        if (!player->openTrading(merchant, getDisplayName(), getId()))
            setTradingPlayer({});
    }
    return true;
}

unsigned int WanderingTrader::GetInteractTooltip(const EntityTooltipDataHolder& tooltipData)
{
    if (tooltipData.m_pItemInstance->getItem() == Items::spawnEgg)
        return IDS_NONE;
    if (tooltipData.m_pItemInstance->getItem() == Items::nameTag)
        return tooltipData.m_pItemInstance->hasCustomHoverName() ? IDS_TOOLTIPS_NAME : IDS_NONE;
    return isAvailableToTrade() ? IDS_TOOLTIPS_TRADE : IDS_NONE;
}

void WanderingTrader::addAdditonalSaveData(CompoundTag* tag)
{
    PathfinderMob::addAdditonalSaveData(tag);
    tag->putLong(L"VisitExpiresAt", mExpiresAt);
    tag->putBoolean(L"UsedNightPotion", mUsedNightPotion);
    if (mOffers)
        tag->put(L"Offers", mOffers->createTag());
    if (hasRestriction())
    {
        const auto center = getRestrictCenter();
        tag->putInt(L"VisitX", center.x);
        tag->putInt(L"VisitY", center.y);
        tag->putInt(L"VisitZ", center.z);
    }
}

void WanderingTrader::readAdditionalSaveData(CompoundTag* tag)
{
    PathfinderMob::readAdditionalSaveData(tag);
    mExpiresAt = tag->getLong(L"VisitExpiresAt");
    mUsedNightPotion = tag->getBoolean(L"UsedNightPotion");
    // Early trader saves disabled potion particles. Preserve the effect's
    // remaining duration while restoring normal visible spell particles.
    if (auto* effect = getEffect(MobEffects::INVISIBILITY))
    {
        if (!effect->isVisible())
        {
            MobEffectInstance visible(MobEffects::INVISIBILITY, effect->getDuration(),
                                      effect->getAmplifier(), effect->isAmbient(), true);
            effect->update(&visible);
            updateInvisibilityStatus();
        }
    }
    if (tag->contains(L"Offers", Tag::TAG_COMPOUND))
        mOffers.reset(new MerchantRecipeList(tag->getCompound(L"Offers")));
    if (tag->contains(L"VisitX", Tag::TAG_ANY_NUMERIC))
        restrictTo(BlockPos(tag->getInt(L"VisitX"), tag->getInt(L"VisitY"), tag->getInt(L"VisitZ")), 16);
}

bool WanderingTrader::canBeLeashed(const boost::shared_ptr<Player>&)
{
    return false;
}

bool WanderingTrader::removeWhenFarAway()
{
    return false;
}

std::wstring WanderingTrader::getName()
{
    return hasCustomName() ? getCustomName() : L10N::GetString(IDS_LCE_WANDERING_TRADER);
}

void WanderingTrader::setTradingPlayer(const boost::shared_ptr<Player>& player)
{
    mCustomer = player;
}

boost::shared_ptr<Player> WanderingTrader::getTradingPlayer()
{
    return mCustomer.lock();
}

MerchantRecipeList* WanderingTrader::getOffers(const boost::shared_ptr<Player>&)
{
    if (!mOffers)
    {
        mOffers.reset(new MerchantRecipeList());
        const std::array<Offer, 16> common{{
            {Item::byBlock(Blocks::sapling), 1, 0, 5, 8},
            {Item::byBlock(Blocks::sapling), 1, 1, 5, 8},
            {Item::byBlock(Blocks::sapling), 1, 3, 5, 8},
            {Item::byBlock(Blocks::sapling), 1, 4, 5, 8},
            {Item::byBlock(Blocks::yellow_flower), 1, 0, 1, 12},
            {Item::byBlock(Blocks::red_flower), 1, 1, 1, 12},
            {Item::byBlock(Blocks::red_flower), 1, 4, 1, 12},
            {Item::byBlock(Blocks::cactus), 1, 0, 3, 8},
            {Item::byBlock(Blocks::pumpkin), 1, 0, 1, 4},
            {Item::byBlock(Blocks::vine), 1, 0, 1, 12},
            {Item::byBlock(Blocks::waterlily), 2, 0, 1, 5},
            {Item::byBlock(Blocks::sea_pickle), 1, 0, 2, 5},
            {Items::dye, 3, 1, 1, 12},
            {Items::dye, 3, 13, 1, 12},
            {Items::reeds, 1, 0, 1, 8},
            {Items::kelp, 1, 0, 3, 12},
        }};
        const std::array<Offer, 5> rare{{
            {Items::nautilus, 1, 0, 5, 5},
            {Items::slimeBall, 1, 0, 4, 5},
            {Item::byBlock(Blocks::coral_block), 1, 0, 3, 8},
            {Item::byBlock(Blocks::packed_ice), 1, 0, 3, 6},
            {Item::byBlock(Blocks::blue_ice), 1, 0, 6, 6},
        }};
        addOffers(*mOffers, common, *mRandom, 5);
        addOffers(*mOffers, rare, *mRandom, 1);
    }
    return mOffers.get();
}

void WanderingTrader::overrideOffers(MerchantRecipeList* offers)
{
    if (offers != mOffers.get())
        mOffers.reset(offers);
}

void WanderingTrader::notifyTrade(MerchantRecipe* recipe)
{
    recipe->increaseUses();
    playSound(sTradeYesSound, getSoundVolume(), getVoicePitch());
    mTradeSoundCooldown = 20;
}

void WanderingTrader::notifyTradeUpdated(const not_null_ptr<ItemInstance>& item)
{
    if (!mLevel->mIsClientSide && getTradingPlayer() && mTradeSoundCooldown == 0)
    {
        playSound(item->isEmpty() ? sTradeNoSound : sTradeYesSound, getSoundVolume(), getVoicePitch());
        mTradeSoundCooldown = 20;
    }
}

std::wstring WanderingTrader::getDisplayName()
{
    return getName();
}

bool WanderingTrader::isAvailableToTrade()
{
    auto customer = getTradingPlayer();
    return isAlive() && !isUsingItem() && (!customer || distanceToSqr(customer) <= 16.0);
}

Level* WanderingTrader::getLevel()
{
    return mLevel;
}

BlockPos WanderingTrader::getPosition()
{
    return BlockPos(mX, mY, mZ);
}

const SoundEvent* WanderingTrader::getAmbientSound()
{
    return getTradingPlayer() ? sTradingSound : sAmbientSound;
}

const SoundEvent* WanderingTrader::getHurtSound(const DamageSource*)
{
    return sHurtSound;
}

const SoundEvent* WanderingTrader::getDeathSound()
{
    return sDeathSound;
}

TraderLlama::TraderLlama(Level* level) : Llama(level)
{
    mTargetSelector.addGoal(0, new DefendTraderGoal(this), true);
}

Entity* TraderLlama::create(Level* level)
{
    return new TraderLlama(level);
}

unsigned long long TraderLlama::GetType()
{
    return EntityType::TRADER_LLAMA;
}

std::wstring TraderLlama::getName()
{
    return hasCustomName() ? getCustomName() : L10N::GetString(IDS_LCE_TRADER_LLAMA);
}

void TraderLlama::joinVisit(const boost::shared_ptr<WanderingTrader>& trader, long long expiresAt)
{
    mExpiresAt = expiresAt;
    setLeashedTo(trader, true);
}

void TraderLlama::tick()
{
    if (!mLevel->mIsClientSide && mExpiresAt > 0)
    {
        const auto holder = getLeashHolder();
        if (!isLeashed() || (holder && holder->GetType() != EntityType::WANDERING_TRADER) || isTamed())
            mExpiresAt = 0;
        else if (mLevel->getGameTime() >= mExpiresAt)
        {
            dropLeash(true, false);
            remove();
            return;
        }
    }
    Llama::tick();
}

void TraderLlama::addAdditonalSaveData(CompoundTag* tag)
{
    Llama::addAdditonalSaveData(tag);
    tag->putLong(L"VisitExpiresAt", mExpiresAt);
}

void TraderLlama::readAdditionalSaveData(CompoundTag* tag)
{
    Llama::readAdditionalSaveData(tag);
    mExpiresAt = tag->getLong(L"VisitExpiresAt");
}
