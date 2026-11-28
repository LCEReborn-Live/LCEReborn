#include "game/client/render/RavagerModel.hpp"
#include <cmath>
#include "game/client/render/ModelPartSkin.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Ravager.hpp"

RavagerModel::RavagerModel(const ModelDefinition& definition)
{
    mTexWidth = mTexHeight = 128;
    if (definition.DoesOverrideModel())
    {
        mNeck = new ModelPartSkin(this, L"neck", definition, nullptr, 0, nullptr);
        mHead = new ModelPartSkin(this, L"head", definition, mNeck, 0, nullptr);
        mMouth = new ModelPartSkin(this, L"mouth", definition, mHead, 0, nullptr);
        new ModelPartSkin(this, L"horns", definition, mHead, 0, nullptr);
        mBody = new ModelPartSkin(this, L"body", definition, nullptr, 0, nullptr);
        for (int i = 0; i < 4; ++i)
            mLegs[i] = new ModelPartSkin(this, L"leg" + std::to_wstring(i), definition, nullptr, 0, nullptr);
    }
    else
    {
        // Java 1.14 geometry is the fallback when a texture pack supplies no model.
        mNeck = new ModelPartSkin(this, 68, 73, nullptr);
        mNeck->addBox(-5, -1, -18, 10, 10, 18, 0.0f);
        mNeck->setPos(0, -7, 5.5f);
        mHead = new ModelPartSkin(this, 0, 0, nullptr);
        mHead->addBox(-8, -20, -14, 16, 20, 16, 0.0f);
        mHead->addBox(-2, -6, -18, 4, 8, 4, 0.0f);
        mHead->setPos(0, 16, -17);
        mNeck->addChild(mHead);
        for (int i = 0; i < 2; ++i)
        {
            auto* horn = new ModelPartSkin(this, 74, 55, nullptr);
            horn->addBox(0, -14, -2, 2, 14, 4, 0.0f);
            horn->setPos(i == 0 ? -10.0f : 8.0f, -14, -8);
            horn->mXRot = 1.0995574f;
            mHead->addChild(horn);
        }
        mMouth = new ModelPartSkin(this, 0, 36, nullptr);
        mMouth->addBox(-8, 0, -16, 16, 3, 16, 0.0f);
        mMouth->setPos(0, -2, 2);
        mHead->addChild(mMouth);
        mBody = new ModelPartSkin(this, 0, 55, nullptr);
        mBody->addBox(-7, -10, -7, 14, 16, 20, 0.0f);
        mBody->texOffs(0, 91)->addBox(-6, 6, -7, 12, 13, 18, 0.0f);
        mBody->setPos(0, 1, 2);
        mBody->mXRot = Mth::PI / 2;
        for (int i = 0; i < 4; ++i)
        {
            mLegs[i] = new ModelPartSkin(this, i < 2 ? 96 : 64, 0, nullptr);
            mLegs[i]->addBox(-4, 0, -4, 8, 37, 8, 0.0f);
            mLegs[i]->setPos(i % 2 ? 8.0f : -8.0f, -13, i < 2 ? 18.0f : -5.0f);
        }
    }
    mNeckX = mNeck->mX; mNeckY = mNeck->mY; mNeckZ = mNeck->mZ;
    mNeckRot = mNeck->mXRot; mHeadRot = mHead->mXRot;
    compileSkinnedDrawlist();
}
void RavagerModel::render(boost::shared_ptr<Entity>& entity, float swing, float amount, float age, float yaw,
                          float pitch, float scale, bool useDisplayList)
{
    setupAnim(swing, amount, age, yaw, pitch, scale, entity, _SkinAdjustments());
    mNeck->render(scale, useDisplayList, false);
    mBody->render(scale, useDisplayList, false);
    for (auto* leg : mLegs) leg->render(scale, useDisplayList, false);
    callSkinnedDrawlist();
}
void RavagerModel::setupAnim(float swing, float amount, float, float yaw, float pitch, float,
                             boost::shared_ptr<Entity>, _SkinAdjustments)
{
    mHead->mXRot = mHeadRot + pitch * Mth::DEG_TO_RAD;
    mHead->mYRot = yaw * Mth::DEG_TO_RAD;
    const float a = std::cos(swing * 0.6662f) * amount * 0.4f;
    mLegs[0]->mXRot = mLegs[3]->mXRot = a;
    mLegs[1]->mXRot = mLegs[2]->mXRot = -a;
}
void RavagerModel::prepareMobModel(boost::shared_ptr<LivingEntity>& entity, float, float, float partial)
{
    const auto mob = boost::static_pointer_cast<Ravager>(entity);
    const int attack = mob->getAttackTick(), stun = mob->getStunTick(), roar = mob->getRoarTick();
    mNeck->mX = mNeckX; mNeck->mY = mNeckY; mNeck->mZ = mNeckZ;
    mNeck->mXRot = mNeckRot;
    if (attack > 0)
    {
        const float wave = (std::abs(std::fmod(attack - partial, 10.0f) - 5.0f) - 2.5f) / 2.5f;
        const float t = (1.0f + wave) * 0.5f;
        const float extension = t * t * t * 12.0f;
        mNeck->mZ += extension - 12.0f;
        mNeck->mY -= extension * std::sin(mNeck->mXRot);
        mMouth->mXRot = attack > 5 ? std::sin((attack - 4.0f - partial) / 4.0f) * Mth::PI * 0.4f
            : std::sin(Mth::PI * (attack - partial) / 10.0f) * 0.15707964f;
    }
    else
    {
        mNeck->mXRot += stun > 0 ? 0.21991149f : 0.0f;
        mMouth->mXRot = Mth::PI * (stun > 0 ? 0.05f : 0.01f);
        if (stun > 0) mNeck->mX += std::sin(stun / 40.0f * 10.0f) * 3.0f;
        else if (roar > 0)
            mMouth->mXRot = Mth::PI * 0.5f * std::sin((20 - roar - partial) / 20.0f * Mth::PI * 0.25f);
    }
}
