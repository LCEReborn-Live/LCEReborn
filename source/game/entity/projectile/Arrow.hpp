#ifndef GAME_ENTITY_PROJECTILE_ARROW_HPP
#define GAME_ENTITY_PROJECTILE_ARROW_HPP

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>

#include "game/entity/projectile/AbstractRotatedItemProjectile.hpp"

class CompoundTag;
class DataFixerUpper;
class HitResult;
class Level;
class LivingEntity;

class Arrow : public AbstractRotatedItemProjectile
{
public:
    Arrow(Level* level);
    Arrow(Level* level, boost::shared_ptr<LivingEntity> owner);
    Arrow(Level* level, double x, double y, double z);
    virtual ~Arrow() {}

    static void addDataWalkers(DataFixerUpper* fixer, unsigned long long version);
    static void addDataWalkers(DataFixerUpper* fixer);
    static void addDataWalkers(DataFixerUpper* fixer, const std::wstring& type);

    void onHitEntity(const HitResult* result) override;
    void _init();
    void addAdditonalSaveData(CompoundTag* tag) override;
    void readAdditionalSaveData(CompoundTag* tag) override;
    void setKnockback(int knockback);
    void setEnchantPower(int power);
    void setEnchantmentEffectsFromEntity(const boost::shared_ptr<LivingEntity>& entity, float damage);
    int getKnockback() const;
    int getEnchantPower() const;
    void setCrossbowShot(int piercing);
    int getPierceLevel() const;

protected:
    bool onHit(const HitResult* result) override;
    bool canHitEntity(const boost::shared_ptr<Entity>& entity) override;
    bool canPierceEntities() const override;

    int mEnchantPower;
    int mKnockback;

private:
    std::vector<int> mPiercedEntityIds;
};

#endif
