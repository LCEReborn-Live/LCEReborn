#pragma once
#include "game/entity/mob/Monster.hpp"

class Raid;
class ItemEntity;

// Shared lifecycle for the five Village & Pillage raid participants. Entity IDs
// and existing SynchedEntityData layouts remain unchanged for old worlds.
class Raider : public Monster
{
public:
    explicit Raider(Level* level);
    void registerGoals() override;
    void aiStep() override;
    void die(const DamageSource* source) override;
    bool removeWhenFarAway() override;
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    CompoundTag* getAdditionContructionTag() override;
    void readAdditionContructionTag(CompoundTag* tag) override;
    void handleEntityEvent(unsigned char event, int data) override;
    void pickUpItem(boost::shared_ptr<ItemEntity> item) override;
    void finalizeSpawnEggSpawn(int data) override;
    virtual bool isCaptain();
    virtual void setCaptain(bool captain);
    virtual bool canLeadRaid();
    virtual void applyRaidBuffs(int wave);
    Raid* getCurrentRaid() const;
    bool hasActiveRaid() const;
    int getRaidId() const { return mRaidId; }
    int getWave() const { return mWave; }
    void setRaid(int id, int wave);
    bool canJoinRaid() const { return mCanJoinRaid; }
    void setCanJoinRaid(bool value) { mCanJoinRaid = value; }
    bool isCelebrating() const { return mCelebrating; }
    void setCelebrating(bool value);
    int mTicksOutsideRaid = 0;
    static bool isLeaderBanner(const not_null_ptr<ItemInstance>& item);
    static SoundEvent* celebrationSounds[5];

private:
    int mRaidId = 0;
    int mWave = 0;
    bool mCaptain = false;
    bool mCanJoinRaid = true;
    bool mCelebrating = false;
};
