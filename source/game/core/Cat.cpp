#include "game/core/Cat.hpp"

#include "game/client/audio/SoundEvents.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Mth.hpp"
#include "game/core/StringIds.hpp"
#include "game/entity/ai/Goal.hpp"
#include "game/entity/ai/LookControl.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/entity/mob/SynchedEntityData.hpp"
#include "game/item/Items.hpp"
#include "game/ui/EntityTooltipDataHolder.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/generation/Player.hpp"

namespace
{
class CatSleepGoal : public Goal
{
public:
    explicit CatSleepGoal(Cat* cat) : mCat(cat) { setRequiredControlFlags(3); }

    bool canUse() override
    {
        const auto owner = boost::dynamic_pointer_cast<Player>(mCat->getOwner());
        if (!owner || !owner->isSleeping() || !owner->mBedPosition || mCat->isSitting()
            || mCat->distanceToSqr(owner) > 100.0)
            return false;
        mBed = *owner->mBedPosition;
        const auto* state = mCat->mLevel->getBlockState(mBed);
        if (state->getBlock() != Blocks::bed) return false;
        if (state->getValue(BedBlock::PART) == &gBedHead)
            mBed = mBed.relative(state->getValue(BedBlock::FACING)->getOpposite());
        if (mCat->mLevel->getBlock(mBed) != Blocks::bed) return false;
        mOwner = owner;
        return true;
    }

    bool canContinueToUse() override
    {
        const auto owner = mOwner.lock();
        return owner && owner->isSleeping() && !mCat->isSitting() && mCat->distanceToSqr(owner) <= 100.0
            && mCat->mLevel->getBlock(mBed) == Blocks::bed;
    }

    void tick() override
    {
        const auto owner = mOwner.lock();
        if (!owner) return;
        const double x = mBed.x + 0.5;
        const double z = mBed.z + 0.5;
        const double dx = mCat->mX - x;
        const double dz = mCat->mZ - z;
        const double mattress = mCat->mLevel->getBlockState(mBed)->getShape(mCat->mLevel, mBed)->mMaxY + mBed.y;
        if (dx * dx + dz * dz > 0.26 || mCat->mY < mattress - 0.05 || mCat->mY > mattress + 0.1)
        {
            mCat->setRelaxing(false);
            mSleepTicks = 0;
            mCat->getNavigation()->moveTo(x, mBed.y + 1.0, z, 1.05);
            return;
        }
        mCat->getNavigation()->stop();
        mCat->getLookControl()->setLookAt(owner, 20.0f, 20.0f);
        mCat->setRelaxing(++mSleepTicks >= 16);
        if (owner->getSleepTimer() >= 80)
            mOwnerSlept = true;
    }

    void stop() override
    {
        const auto owner = mOwner.lock();
        const auto dayTime = mCat->mLevel->getDayTime() % 24000;
        if (owner && !owner->isSleeping() && mOwnerSlept && mCat->isRelaxing()
            && dayTime < 1200 && mCat->mLevel->mRandom->nextInt(10) < 7)
        {
            Item* gifts[] = {Items::rabbitFoot, Items::rabbitHide, Items::string,
                             Items::rottenFlesh, Items::feather, Items::chicken, Items::phantomMembrane};
            if (auto gift = mCat->spawnAtLocation(gifts[mCat->mLevel->mRandom->nextInt(62) / 10], 1))
                gift->setCatGiftRecipient(owner->getUUID());
        }
        mCat->setRelaxing(false);
        mCat->getNavigation()->stop();
        mOwner.reset();
        mSleepTicks = 0;
        mOwnerSlept = false;
    }

    std::wstring getName() override { return L"Sleep near owner"; }

private:
    Cat* mCat;
    boost::weak_ptr<Player> mOwner;
    BlockPos mBed;
    int mSleepTicks = 0;
    bool mOwnerSlept = false;
};
}

Cat::Cat(Level* level) : Ocelot(level, true)
{
    fjDerivedCtorCalls();
    setSize(0.55f, 0.7f);
}

Entity* Cat::create(Level* level)
{
    return new Cat(level);
}

std::wstring Cat::getName()
{
    return hasCustomName() ? getCustomName() : L10N::GetString(IDS_LCE_CAT);
}

void Cat::registerGoals()
{
    Ocelot::registerGoals();
    mGoalSelector.addGoal(3, new CatSleepGoal(this), true);
}

bool Cat::isRelaxing() const
{
    return (mEntityData->get(DATA_FLAGS) & 8) != 0;
}

void Cat::tick()
{
    Ocelot::tick();
    mOldRelaxingProgress = mRelaxingProgress;
    mRelaxingProgress = Mth::clamp(mRelaxingProgress + (isRelaxing() ? 0.20f : -0.20f), 0.0f, 1.0f);
}

float Cat::getRelaxingProgress(float partialTick) const
{
    return mOldRelaxingProgress + (mRelaxingProgress - mOldRelaxingProgress) * partialTick;
}

void Cat::setRelaxing(bool relaxing)
{
    const auto flags = mEntityData->get(DATA_FLAGS);
    mEntityData->set(DATA_FLAGS, static_cast<unsigned char>(relaxing ? flags | 8 : flags & ~8), true);
}

bool Cat::mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand)
{
    const auto item = player->getItemInHand(hand);
    if (!isTame() || !isFood(item) || getHealth() >= getMaxHealth())
        return Ocelot::mobInteract(player, hand);

    if (!mLevel->mIsClientSide)
    {
        usePlayerItem(player, item);
        heal(2.0f);
        playSound(SoundEvents::CAT_EAT, 0.5f, 1.0f);
    }
    return true;
}

unsigned int Cat::GetInteractTooltip(const EntityTooltipDataHolder& holder)
{
    if (isTame() && isFood(holder.m_pItemInstance) && getHealth() < getMaxHealth())
        return IDS_TOOLTIPS_FEED;
    return Ocelot::GetInteractTooltip(holder);
}

boost::shared_ptr<AgableMob> Cat::getBreedOffspring(boost::shared_ptr<AgableMob> partner)
{
    auto kitten = boost::shared_ptr<Cat>(new Cat(mLevel));
    const auto other = boost::dynamic_pointer_cast<Cat>(partner);
    kitten->setCatType(other && mLevel->mRandom->nextBoolean() ? other->getCatType() : getCatType());
    kitten->setOwnerUUID(getOwnerUUID());
    kitten->setTame(isTame());
    return kitten;
}

MobGroupData* Cat::finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data, const std::wstring& name)
{
    setCatType(Tuxedo + mLevel->mRandom->nextInt(AllBlack));
    return Ocelot::finalizeMobSpawn(difficulty, data, name);
}
