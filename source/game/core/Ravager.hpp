#pragma once

#include "game/entity/mob/Raider.hpp"

class Ravager : public Raider
{
public:
    explicit Ravager(Level* level);
    static Entity* create(Level* level);
    unsigned long long GetType() override;
    std::wstring getName() override;
    void registerGoals() override;
    PathNavigation* createNavigation(Level* level) override;
    void aiStep() override;
    bool isImmobile() override;
    bool canSee(boost::shared_ptr<Entity> entity) override;
    bool isAlliedTo(boost::shared_ptr<Entity> entity) override;
    bool doHurtTarget(boost::shared_ptr<Entity> target) override;
    void blockedByShield(const boost::shared_ptr<LivingEntity>& defender) override;
    void handleEntityEvent(unsigned char event, int data) override;
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    CompoundTag* getAdditionContructionTag() override;
    void readAdditionContructionTag(CompoundTag* tag) override;
    void dropDeathLoot(bool hitByPlayer, int lootingLevel) override;
    double getRideHeight() override { return 2.1; }
    int getMaxHeadYRot() override { return 45; }
    boost::shared_ptr<Entity> getControllingPassenger() override;
    bool canBeControlledByRider() override;
    int getAttackTick() const { return mAttackTick; }
    int getStunTick() const { return mStunTick; }
    int getRoarTick() const { return mRoarTick; }

protected:
    const SoundEvent* getAmbientSound() override;
    const SoundEvent* getHurtSound(const DamageSource* source) override;
    const SoundEvent* getDeathSound() override;
    void playStepSound(const BlockPos& pos, Block* block) override;

private:
    void roar();
    void strongKnockback(const boost::shared_ptr<Entity>& target);
    void saveAnimation(CompoundTag* tag);
    void loadAnimation(CompoundTag* tag);
    int mAttackTick = 0;
    int mStunTick = 0;
    int mRoarTick = 0;
    friend class SoundEvents;
    static SoundEvent *sAmbient, *sBite, *sDeath, *sHurt, *sRoar, *sStep, *sStun;
};
