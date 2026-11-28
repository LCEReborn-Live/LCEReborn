#include "game/client/render/PandaRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "game/client/render/ModelDefinition.hpp"
#include "game/client/render/ItemRenderer.hpp"
#include "game/client/render/QuadrupedModel.hpp"
#include "game/client/render/TexturePack.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Panda.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/util/ResourceLocation.hpp"

namespace
{
const ModelDefinition& defaultPandaModel()
{
    return *Minecraft::GetInstance()->mTexturePackRepository->defaultTexturePack->GetCustomModel(L"panda");
}

class PandaModel : public QuadrupedModel
{
public:
    explicit PandaModel(const ModelDefinition& definition) : QuadrupedModel(9, 0.0f, definition)
    {
        if (definition.DoesOverrideModel()) return;

        mTexWidth = mTexHeight = 64;
        delete mHead;
        delete mBody;
        delete mLeg0;
        delete mLeg1;
        delete mLeg2;
        delete mLeg3;

        mHead = new ModelPartSkin(this, 0, 6, nullptr);
        // Keep the neck inside the shoulders to avoid coplanar fur faces.
        mHead->addBox(-6.5f, -6.0f, -5.0f, 13, 10, 9, -0.3f);
        mHead->texOffs(45, 16)->addBox(-3.5f, -1.0f, -6.25f, 7, 5, 2, 0.0f);
        mHead->texOffs(43, 0)->addBox(-6.0f, -8.0f, -3.0f, 4, 3, 1, 0.0f);
        mHead->texOffs(51, 0)->addBox(2.0f, -8.0f, -3.0f, 4, 3, 1, 0.0f);
        mHead->setPos(0.0f, 11.25f, -8.75f);
        mHead->setWorldPos(0.0f, 11.25f, -8.75f);

        mBody = new ModelPartSkin(this, 0, 25, nullptr);
        mBody->addBox(-6.5f, -9.0f, -6.5f, 13, 18, 13, 0.0f);
        mBody->setPos(0.0f, 12.25f, 2.0f);
        mBody->setWorldPos(0.0f, 12.25f, 2.0f);

        mLeg0 = createLeg(-4.5f, 8.0f);
        mLeg1 = createLeg(4.5f, 8.0f);
        mLeg2 = createLeg(-4.5f, -5.0f);
        mLeg3 = createLeg(4.5f, -5.0f);
        mYHeadOffs = 10.0f;
        mZHeadOffs = 4.0f;
        compileSkinnedDrawlist();
    }

    std::wstring getName() override { return L"panda"; }

    void setupAnim(float swing, float amount, float age, float yaw, float pitch, float scale,
                   boost::shared_ptr<Entity> entity, _SkinAdjustments adjustments) override
    {
        QuadrupedModel::setupAnim(swing, amount, age, yaw, pitch, scale, entity, adjustments);
        for (ModelPart* part : {mBody, mHead, mLeg0, mLeg1, mLeg2, mLeg3})
            part->setPos(part->mWorldX, part->mWorldY, part->mWorldZ);
        mLeg0->mYRot = mLeg1->mYRot = 0.0f;

        const auto panda = boost::static_pointer_cast<Panda>(entity);
        const auto pose = panda->getPose();
        const float partial = age - std::floor(age);
        const float blend = ease(panda->getSittingProgress(partial));
        const auto seated = [blend](float from, float to) { return from + (to - from) * blend; };
        const float chew = pose == Panda::Eating ? chewMotion(*panda, partial) : 0.0f;
        const bool scared = pose == Panda::Scared;

        mBody->mY = seated(mBody->mY, 13.8f);
        mBody->mZ = seated(mBody->mZ, 1.8f);
        mBody->mXRot = seated(mBody->mXRot, -0.10f);
        mHead->mY = seated(mHead->mY, 1.9f + 0.43f * chew);
        mHead->mZ = seated(mHead->mZ, -6.0f);
        mHead->mXRot = seated(mHead->mXRot, scared ? 0.35f : 0.04f + 0.12f * chew);
        if (pose == Panda::Eating)
            mHead->mYRot = seated(mHead->mYRot, mHead->mYRot * 0.22f);
        for (ModelPart* leg : {mLeg0, mLeg1})
        {
            leg->mX = seated(leg->mX, std::copysign(6.1f, leg->mX));
            leg->mY = seated(leg->mY, 20.7f);
            leg->mZ = seated(leg->mZ, -0.7f);
            leg->mXRot = seated(leg->mXRot, -1.56f);
            leg->mYRot = -std::copysign(0.31f, leg->mX) * blend;
        }
        for (ModelPart* leg : {mLeg2, mLeg3})
        {
            if (!scared) leg->mX = seated(leg->mX, std::copysign(3.8f, leg->mX));
            leg->mY = seated(leg->mY, 8.6f - 0.65f * chew);
            leg->mZ = seated(leg->mZ, -1.2f);
            leg->mXRot = seated(leg->mXRot, scared ? -2.1f : -1.49f - 0.23f * chew);
        }

        const float lying = ease(panda->getLyingProgress(partial));
        if (lying > 0.0f)
        {
            const float sway = std::sin(age * 0.23f) * 0.035f;
            mHead->mXRot *= 1.0f - lying * 0.8f;
            mLeg0->mXRot -= (0.28f + sway) * lying;
            mLeg1->mXRot -= (0.28f - sway) * lying;
            mLeg2->mXRot += (0.15f + sway) * lying;
            mLeg3->mXRot += (0.15f - sway) * lying;
        }
        if (pose == Panda::Rolling)
        {
            const float time = panda->getPoseAge() + partial;
            const float tuck = ease(std::min(time, Panda::RollTicks - time) / 6.0f);
            mHead->mZ += (mModelOverridden ? 5.5f : 2.5f) * tuck;
            mHead->mY += 0.8f * tuck;
            mHead->mXRot = 0.32f * tuck;
            mLeg0->mXRot = mLeg1->mXRot = -0.8f * tuck;
            mLeg2->mXRot = mLeg3->mXRot = 0.8f * tuck;
        }
    }

    void render(boost::shared_ptr<Entity>& entity, float swing, float amount, float age, float yaw, float pitch,
                float scale, bool useDisplayList) override
    {
        const auto panda = boost::static_pointer_cast<Panda>(entity);
        const Panda::Pose pose = panda->getPose();
        const float partial = age - std::floor(age);
        const float time = panda->getPoseAge() + partial;
        const float lying = ease(panda->getLyingProgress(partial));
        GlStateManager::pushMatrix();
        if (pose == Panda::Rolling)
        {
            tilt(time * (360.0f / Panda::RollTicks), scale);
            yaw = pitch = 0.0f;
        }
        else if (lying > 0.0f)
        {
            tilt(-180.0f * lying, scale);
        }
        else if (pose == Panda::Sneezing)
        {
            const float windup = ease(time / (Panda::SneezeTick - 2));
            const float snap = ease((time - (Panda::SneezeTick - 2)) / 2.0f);
            const float recovery = ease((time - Panda::SneezeTick) / Panda::SneezeRecoveryTicks);
            pitch += (-19.0f * windup + 50.0f * snap) * (1.0f - recovery);
        }
        else if (pose == Panda::Scared)
        {
            GlStateManager::rotatef(std::sin(age * 1.8f) * 1.4f, 0.0f, 1.0f, 0.0f);
        }
        const float walkingAmount = pose == Panda::Walking ? amount : 0.0f;
        if (mYoung)
        {
            setupAnim(swing, walkingAmount, age, yaw, pitch, scale, entity, _SkinAdjustments());
            renderCub(scale, useDisplayList);
        }
        else
            QuadrupedModel::render(entity, swing, walkingAmount, age, yaw, pitch, scale, useDisplayList);
        if (pose == Panda::Eating)
        {
            const auto bamboo = not_null_ptr<ItemInstance>(new ItemInstance(Items::bamboo));
            const float chew = chewMotion(*panda, partial);
            const float reach = 8.8f;
            const float handY = mLeg2->mY + std::cos(mLeg2->mXRot) * reach;
            const float handZ = mLeg2->mZ + std::sin(mLeg2->mXRot) * reach;
            GlStateManager::translatef(0.0f, (handY + 0.65f) * scale, (handZ - 1.7f) * scale);
            GlStateManager::rotatef(47.0f + 2.0f * chew, 0.0f, 0.0f, 1.0f);
            GlStateManager::scalef(0.34f, 0.34f, 0.34f);
            Minecraft::GetInstance()->getItemRenderer()->renderStatic(bamboo, panda, ItemTransforms::FIXED,
                                                                      1.0f, false, nullptr);
        }
        GlStateManager::popMatrix();
    }

private:
    static float ease(float progress)
    {
        progress = Mth::clamp(progress, 0.0f, 1.0f);
        return progress * progress * (3.0f - 2.0f * progress);
    }

    void renderCub(float scale, bool useDisplayList)
    {
        GlStateManager::pushMatrix();
        GlStateManager::scalef(0.5f, 0.5f, 0.5f);
        GlStateManager::translatef(0.0f, 24.0f * scale, 0.0f);

        GlStateManager::pushMatrix();
        GlStateManager::translatef(mHead->mX * scale, mHead->mY * scale, mHead->mZ * scale);
        GlStateManager::scalef(1.28f, 1.28f, 1.28f);
        GlStateManager::translatef(-mHead->mX * scale, -mHead->mY * scale, -mHead->mZ * scale);
        mHead->render(scale, useDisplayList, false);
        GlStateManager::popMatrix();

        for (ModelPart* part : {mBody, mLeg0, mLeg1, mLeg2, mLeg3})
            part->render(scale, useDisplayList, false);
        GlStateManager::popMatrix();
        callSkinnedDrawlist();
    }

    void tilt(float degrees, float scale)
    {
        const float radians = degrees * Mth::DEG_TO_RAD;
        const float angleSin = std::sin(radians);
        const float angleCos = std::cos(radians);
        constexpr float ground = 24.0f;
        constexpr float adultPivot = 13.7f;
        const float pivot = mYoung ? ground * 0.5f + adultPivot * 0.5f : adultPivot;
        // The long front and back of the body sweep lower than the feet during a turn.
        const float end = angleSin > 0.0f && degrees >= 0.0f ? 16.5f : 13.0f;
        const float support = (mBody->mWorldY - adultPivot) * angleCos + 6.5f * std::abs(angleCos)
            + end * std::abs(angleSin);
        const float lift = std::max(0.0f, adultPivot + support - ground) * (mYoung ? 0.5f : 1.0f);
        GlStateManager::translatef(0.0f, (pivot - lift) * scale, 0.0f);
        GlStateManager::rotatef(degrees, 1.0f, 0.0f, 0.0f);
        GlStateManager::translatef(0.0f, -pivot * scale, 0.0f);
    }

    static float chewMotion(const Panda& panda, float partial)
    {
        const float phase = (panda.getPoseAge() + partial) / Panda::ChewInterval;
        return 0.5f + 0.5f * std::cos(phase * 2.0f * Mth::PI);
    }

    ModelPartSkin* createLeg(float x, float z)
    {
        auto* leg = new ModelPartSkin(this, 40, 0, nullptr);
        leg->addBox(-3.0f, 0.0f, -3.0f, 6, 9, 6, -0.5f);
        leg->setWorldPos(x, 15.5f, z);
        leg->setPos(x, 15.5f, z);
        return leg;
    }
};
}

PandaRenderer::PandaRenderer(EntityRenderDispatcher* dispatcher)
    : MobRenderer(dispatcher, new PandaModel(defaultPandaModel()), 0.65f)
{
}

const ResourceLocation* PandaRenderer::getTextureLocation(boost::shared_ptr<Entity>& entity)
{
    static const ResourceLocation textures[] = {
        ResourceLocation(L"mob/panda/panda"), ResourceLocation(L"mob/panda/panda_lazy"),
        ResourceLocation(L"mob/panda/panda_worried"), ResourceLocation(L"mob/panda/panda_playful"),
        ResourceLocation(L"mob/panda/panda_brown"), ResourceLocation(L"mob/panda/panda_sneezy"),
        ResourceLocation(L"mob/panda/panda_aggressive")};
    return &textures[boost::static_pointer_cast<Panda>(entity)->getGene()];
}

Model* PandaRenderer::createModel(const ModelDefinition& definition)
{
    return new PandaModel(definition);
}
