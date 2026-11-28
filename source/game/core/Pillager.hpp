#ifndef GAME_CORE_PILLAGER_HPP
#define GAME_CORE_PILLAGER_HPP

#include "game/core/AbstractIllager.hpp"

class Pillager : public AbstractIllager
{
public:
    explicit Pillager(Level* level);
    static Entity* create(Level* level);
    unsigned long long GetType() override;
    std::wstring getName() override;
    void registerGoals() override;
    bool checkSpawnRules() override;
    int getMaxSpawnClusterSize() override { return 1; }
    int getArmPose() override;
    MobGroupData* finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                  const std::wstring& name) override;
    bool isAlliedTo(boost::shared_ptr<Entity> entity) override;
    bool isCaptain();
    void setCaptain(bool captain);
    void preparePatrol(bool captain);
    bool isPatrolling() const { return mPatrolling; }
    bool hasPatrolTarget() const { return mHasPatrolTarget; }
    const BlockPos& getPatrolTarget() const { return mPatrolTarget; }
    void setPatrolTarget(const BlockPos& pos);
    void findPatrolTarget();
    bool removeWhenFarAway() override;
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    bool shootCrossbow(const boost::shared_ptr<LivingEntity>& target);

protected:
    const SoundEvent* getAmbientSound() override;
    const SoundEvent* getHurtSound(const DamageSource* source) override;
    const SoundEvent* getDeathSound() override;

private:
    bool mPatrolling = false;
    bool mHasPatrolTarget = false;
    BlockPos mPatrolTarget;
    friend class SoundEvents;
    static SoundEvent* sAmbientSound;
    static SoundEvent* sHurtSound;
    static SoundEvent* sDeathSound;
};

#endif
