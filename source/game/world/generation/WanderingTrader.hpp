#ifndef GAME_WORLD_GENERATION_WANDERINGTRADER_HPP
#define GAME_WORLD_GENERATION_WANDERINGTRADER_HPP

#include "game/core/Llama.hpp"
#include "game/core/Merchant.hpp"
#include "game/entity/mob/Npc.hpp"
#include "game/entity/mob/PathfinderMob.hpp"

#include <memory>

class WanderingTrader : public PathfinderMob, public Merchant, public Npc
{
public:
    explicit WanderingTrader(Level* level);
    ~WanderingTrader() override;
    static Entity* create(Level* level);
    static bool spawnVisit(Level& level, const BlockPos& origin, long long expiresAt);
    unsigned long long GetType() override;
    void registerGoals() override;
    void tick() override;
    void completeUsingItem() override;
    bool mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand) override;
    unsigned int GetInteractTooltip(const EntityTooltipDataHolder& tooltipData) override;
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    bool canBeLeashed(const boost::shared_ptr<Player>& player) override;
    bool removeWhenFarAway() override;
    std::wstring getName() override;

    void setTradingPlayer(const boost::shared_ptr<Player>& player) override;
    boost::shared_ptr<Player> getTradingPlayer() override;
    MerchantRecipeList* getOffers(const boost::shared_ptr<Player>& player) override;
    void overrideOffers(MerchantRecipeList* offers) override;
    void notifyTrade(MerchantRecipe* recipe) override;
    void notifyTradeUpdated(const not_null_ptr<ItemInstance>& item) override;
    std::wstring getDisplayName() override;
    bool isAvailableToTrade() override;
    Level* getLevel() override;
    BlockPos getPosition() override;

protected:
    const SoundEvent* getAmbientSound() override;
    const SoundEvent* getHurtSound(const DamageSource* source) override;
    const SoundEvent* getDeathSound() override;
    void customServerAiStep() override;

private:
    friend class SoundEvents;
    static SoundEvent* sAmbientSound;
    static SoundEvent* sTradingSound;
    static SoundEvent* sTradeYesSound;
    static SoundEvent* sTradeNoSound;
    static SoundEvent* sHurtSound;
    static SoundEvent* sDeathSound;
    static SoundEvent* sDrinkPotionSound;
    static SoundEvent* sDrinkMilkSound;
    static SoundEvent* sDisappearedSound;
    static SoundEvent* sReappearedSound;

    std::unique_ptr<MerchantRecipeList> mOffers;
    boost::weak_ptr<Player> mCustomer;
    long long mExpiresAt = 0;
    int mTradeSoundCooldown = 0;
    bool mUsedNightPotion = false;
};

class TraderLlama : public Llama
{
public:
    explicit TraderLlama(Level* level);
    static Entity* create(Level* level);
    unsigned long long GetType() override;
    std::wstring getName() override;
    void tick() override;
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    void joinVisit(const boost::shared_ptr<WanderingTrader>& trader, long long expiresAt);

private:
    long long mExpiresAt = 0;
};

#endif
