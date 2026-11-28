#ifndef GAME_CORE_PANDA_HPP
#define GAME_CORE_PANDA_HPP

#include "game/entity/mob/Animal.hpp"

class Panda : public Animal
{
public:
    enum Gene { Normal, Lazy, Worried, Playful, Brown, Weak, Aggressive };
    enum Pose { Walking, Sitting, Lying, Rolling, Eating, Sneezing, Scared };

    static constexpr int RollTicks = 32;
    static constexpr int SneezeTick = 20;
    static constexpr int SneezeRecoveryTicks = 5;
    static constexpr int ChewInterval = 10;

    explicit Panda(Level* level);
    static Entity* create(Level* level);
    static void staticCtor();
    unsigned long long GetType() override { return EntityType::PANDA; }
    std::wstring getName() override;

    Gene getGene() const;
    Pose getPose() const;
    int getPoseAge() const { return mPoseAge; }
    float getSittingProgress(float partialTick) const;
    float getLyingProgress(float partialTick) const;
    bool isFood(const not_null_ptr<ItemInstance>& item) override;
    unsigned int GetInteractTooltip(const EntityTooltipDataHolder& holder) override;
    bool mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand) override;
    boost::shared_ptr<AgableMob> getBreedOffspring(boost::shared_ptr<AgableMob> partner) override;
    MobGroupData* finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                   const std::wstring& spawnData) override;
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    void tick() override;

protected:
    void defineSynchedData() override;
    void registerGoals() override;
    void registerAttributes() override;
    void customServerAiStep() override;
    const SoundEvent* getAmbientSound() override;
    const SoundEvent* getHurtSound(const DamageSource* source) override;
    const SoundEvent* getDeathSound() override;
    void playStepSound(const BlockPos& pos, Block* block) override;
    bool canPickUpLoot() override;
    void pickUpItem(boost::shared_ptr<ItemEntity> item) override;
    bool doHurtTarget(boost::shared_ptr<Entity> target) override;
    void dropDeathLoot(bool hitByPlayer, int lootingLevel) override;

private:
    void setGenes(Gene main, Gene hidden);
    bool canEnterLoveMode();
    bool hasBambooNearby();
    void startPose(Pose pose, int duration);
    void tickPose();

    static const EntityDataAccessor<int>* DATA_GENES;
    static const EntityDataAccessor<int>* DATA_POSE;
    Pose mLastPose = Walking;
    int mPoseAge = 0;
    int mPoseDuration = 0;
    float mSittingProgress = 0.0f;
    float mOldSittingProgress = 0.0f;
    float mLyingProgress = 0.0f;
    float mOldLyingProgress = 0.0f;
    float mRollYaw = 0.0f;
};

#endif
