#ifndef GAME_CORE_CAT_HPP
#define GAME_CORE_CAT_HPP

#include "game/core/Ocelot.hpp"

class Cat : public Ocelot
{
public:
    enum Variant { Tuxedo = 1, Red, Siamese, Tabby, White, Ragdoll, Persian, British,
                   Calico, Jellie, AllBlack };

    explicit Cat(Level* level);
    static Entity* create(Level* level);
    unsigned long long GetType() override { return EntityType::CAT; }
    std::wstring getName() override;
    void registerGoals() override;
    void tick() override;
    bool isRelaxing() const;
    float getRelaxingProgress(float partialTick) const;
    void setRelaxing(bool relaxing);
    bool mobInteract(const boost::shared_ptr<Player>& player, InteractionHand::EInteractionHand hand) override;
    unsigned int GetInteractTooltip(const EntityTooltipDataHolder& holder) override;
    boost::shared_ptr<AgableMob> getBreedOffspring(boost::shared_ptr<AgableMob> partner) override;
    MobGroupData* finalizeMobSpawn(const DifficultyInstance& difficulty, MobGroupData* data,
                                   const std::wstring& dataString) override;

private:
    float mOldRelaxingProgress = 0.0f;
    float mRelaxingProgress = 0.0f;
};

#endif
