#include "game/client/render/OcelotModel.hpp"

#include "game/client/render/ModelDefinition.hpp"
#include "game/client/render/ModelPart.hpp"
#include "game/client/render/ModelPartSkin.hpp"
#include "game/client/render/Tesselator.hpp"
#include "game/client/render/TexturePack.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/core/BufferBuilder.hpp"
#include "game/core/Cat.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Ocelot.hpp"

namespace
{
constexpr float KittenHeadScale = 0.75f;
constexpr float KittenBodyScale = 0.5f;
constexpr float KittenHeadOffsetY = 10.0f;
constexpr float KittenHeadOffsetZ = 4.0f;
constexpr float KittenBodyOffsetY = 24.0f;
}

OcelotModel::OcelotModel(const std::wstring& modelName) : mModelName(modelName)
{
    TexturePack* texturePack = Minecraft::GetInstance()->mTexturePackRepository->defaultTexturePack;
    const auto& name = texturePack->HasCustomModel(modelName) ? modelName : std::wstring(L"ocelot");
    LoadOcelotModelDefinition(*texturePack->GetCustomModel(name));
}

OcelotModel::OcelotModel(const ModelDefinition& modelDefinition, const std::wstring& modelName) : mModelName(modelName)
{
    LoadOcelotModelDefinition(modelDefinition);
}

void OcelotModel::LoadOcelotModelDefinition(const ModelDefinition& modelDefinition)
{
    mPose = 1;
    BufferBuilder* builder = Tesselator::getInstance()->getBuilder();

    if (modelDefinition.DoesOverrideModel())
    {
        mBackLegL = new ModelPartSkin(this, L"backLegL", modelDefinition, NULL, 0.0f, NULL);
        mBackLegR = new ModelPartSkin(this, L"backLegR", modelDefinition, NULL, 0.0f, NULL);
        mFrontLegL = new ModelPartSkin(this, L"frontLegL", modelDefinition, NULL, 0.0f, NULL);
        mFrontLegR = new ModelPartSkin(this, L"frontLegR", modelDefinition, NULL, 0.0f, NULL);
        mTail1 = new ModelPartSkin(this, L"tail1", modelDefinition, NULL, 0.0f, NULL);
        mTail2 = new ModelPartSkin(this, L"tail2", modelDefinition, NULL, 0.0f, NULL);
        mHead = new ModelPartSkin(this, L"head", modelDefinition, NULL, 0.0f, NULL);
        mBody = new ModelPartSkin(this, L"body", modelDefinition, NULL, 0.0f, NULL);
    }

    compileSkinnedDrawlist();
    builder->useDefaultShaders();
}

void OcelotModel::render(boost::shared_ptr<Entity>& entity, float limbSwing, float limbSwingAmount, float ageInTicks,
                         float netHeadYaw, float headPitch, float scale, bool useDisplayList)
{
    setupAnim(limbSwing, limbSwingAmount, ageInTicks, netHeadYaw, headPitch, scale, entity, _SkinAdjustments());

    if (mRelaxing > 0.0f)
    {
        const float belly = (mYoung ? 18.5f : mBody->mWorldY) * scale;
        GlStateManager::pushMatrix();
        GlStateManager::translatef(0.0f, belly + (20.4f * scale - belly) * mRelaxing, 0.0f);
        GlStateManager::rotatef(90.0f * mRelaxing, 0.0f, 0.0f, 1.0f);
        GlStateManager::translatef(0.0f, -belly, 0.0f);
    }

    if (mYoung)
    {
        GlStateManager::pushMatrix();
        GlStateManager::scalef(KittenHeadScale, KittenHeadScale, KittenHeadScale);
        GlStateManager::translatef(0.0f, KittenHeadOffsetY * scale, KittenHeadOffsetZ * scale);
        mHead->render(scale, useDisplayList, false);
        GlStateManager::popMatrix();
        GlStateManager::pushMatrix();
        GlStateManager::scalef(KittenBodyScale, KittenBodyScale, KittenBodyScale);
        GlStateManager::translatef(0.0f, KittenBodyOffsetY * scale, 0.0f);
        mBody->render(scale, useDisplayList, false);
        mBackLegL->render(scale, useDisplayList, false);
        mBackLegR->render(scale, useDisplayList, false);
        mFrontLegL->render(scale, useDisplayList, false);
        mFrontLegR->render(scale, useDisplayList, false);
        mTail1->render(scale, useDisplayList, false);
        mTail2->render(scale, useDisplayList, false);
        GlStateManager::popMatrix();
        callSkinnedDrawlist();
    }
    else
    {
        mHead->render(scale, useDisplayList, false);
        mBody->render(scale, useDisplayList, false);
        mTail1->render(scale, useDisplayList, false);
        mTail2->render(scale, useDisplayList, false);
        mBackLegL->render(scale, useDisplayList, false);
        mBackLegR->render(scale, useDisplayList, false);
        mFrontLegL->render(scale, useDisplayList, false);
        mFrontLegR->render(scale, useDisplayList, false);
        callSkinnedDrawlist();
    }
    if (mRelaxing > 0.0f) GlStateManager::popMatrix();
}

void OcelotModel::render(OcelotModel* model, float scale, bool useDisplayList)
{
    mHead->mYRot = model->mHead->mYRot;
    mHead->mXRot = model->mHead->mXRot;
    mHead->mY = model->mHead->mY;
    mHead->mX = model->mHead->mX;
    mBody->mYRot = model->mBody->mYRot;
    mBody->mXRot = model->mBody->mXRot;
    mTail1->mYRot = model->mBody->mYRot;
    mTail1->mY = model->mBody->mY;
    mTail1->mX = model->mBody->mX;
    mTail1->render(scale, useDisplayList, false);
    mTail2->mYRot = model->mBody->mYRot;
    mTail2->mY = model->mBody->mY;
    mTail2->mX = model->mBody->mX;
    mTail2->render(scale, useDisplayList, false);
    mBackLegL->mXRot = model->mBackLegL->mXRot;
    mBackLegR->mXRot = model->mBackLegR->mXRot;
    mBackLegL->render(scale, useDisplayList, false);
    mBackLegR->render(scale, useDisplayList, false);
    mFrontLegL->mXRot = model->mFrontLegL->mXRot;
    mFrontLegR->mXRot = model->mFrontLegR->mXRot;
    mFrontLegL->render(scale, useDisplayList, false);
    mFrontLegR->render(scale, useDisplayList, false);
    mHead->render(scale, useDisplayList, false);
    mBody->render(scale, useDisplayList, false);
}

void OcelotModel::setupAnim(float limbSwing, float limbSwingAmount, float ageInTicks, float netHeadYaw, float headPitch,
                            float scale, boost::shared_ptr<Entity> entity, _SkinAdjustments skinAdjustments)
{
    int pose = mPose;
    mHead->mXRot = headPitch * Mth::DEG_TO_RAD;
    mHead->mYRot = netHeadYaw * Mth::DEG_TO_RAD;

    if (pose != 3)
    {
        mBackLegL->mXRot = Mth::cos(limbSwing * 0.6662f) * 1.0f * limbSwingAmount;
        if (pose == 2)
        {
            mBackLegR->mXRot = Mth::cos(limbSwing * 0.6662f + 0.3f) * 1.0f * limbSwingAmount;
            mFrontLegL->mXRot = Mth::cos(limbSwing * 0.6662f + 3.4415927f) * 1.0f * limbSwingAmount;
            mFrontLegR->mXRot = Mth::cos(limbSwing * 0.6662f + 3.1415927f) * 1.0f * limbSwingAmount;
            mTail2->mXRot = 0.20f + 0.25f * Mth::cos(limbSwing) * limbSwingAmount;
        }
        else
        {
            mBackLegR->mXRot = Mth::cos(limbSwing * 0.6662f + 3.1415927f) * 1.0f * limbSwingAmount;
            mFrontLegL->mXRot = Mth::cos(limbSwing * 0.6662f + 3.1415927f) * 1.0f * limbSwingAmount;
            mFrontLegR->mXRot = Mth::cos(limbSwing * 0.6662f) * 1.0f * limbSwingAmount;
            mTail2->mXRot = mTail1->mXRot + 0.32f + 0.3f * Mth::cos(limbSwing) * limbSwingAmount;
        }
    }

    if (mRelaxing > 0.0f)
    {
        mHead->mXRot += (0.1f - mHead->mXRot) * mRelaxing;
        mHead->mYRot += (-0.23f - mHead->mYRot) * mRelaxing;
        mFrontLegL->mXRot += (-1.28f - mFrontLegL->mXRot) * mRelaxing;
        mFrontLegR->mXRot += (-1.12f - mFrontLegR->mXRot) * mRelaxing;
        mBackLegL->mXRot += (-1.15f - mBackLegL->mXRot) * mRelaxing;
        mBackLegR->mXRot += (-1.02f - mBackLegR->mXRot) * mRelaxing;
        mTail1->mXRot += (-0.18f - mTail1->mXRot) * mRelaxing;
        mTail2->mXRot += (0.08f - mTail2->mXRot) * mRelaxing;
    }

    if (pose == 3)
    {
        mHead->mXRot = Mth::clamp(mHead->mXRot, -50.0f * Mth::DEG_TO_RAD, 35.0f * Mth::DEG_TO_RAD);
        // Keep the back of the head at the front of the tilted body as it turns.
        const float bodyFront = -8.0f;
        float neckY = mBody->mY - Mth::sin(mBody->mXRot) * bodyFront - 0.8f;
        float neckZ = mBody->mZ + Mth::cos(mBody->mXRot) * bodyFront;
        if (mYoung)
        {
            neckY = (neckY + KittenBodyOffsetY) * KittenBodyScale / KittenHeadScale - KittenHeadOffsetY;
            neckZ = neckZ * KittenBodyScale / KittenHeadScale - KittenHeadOffsetZ;
        }
        const float headBack = 1.5f;
        const float depth = Mth::cos(mHead->mXRot) * headBack;
        mHead->setPos(mHead->mWorldX - Mth::sin(mHead->mYRot) * depth,
                      neckY + Mth::sin(mHead->mXRot) * headBack,
                      neckZ - Mth::cos(mHead->mYRot) * depth);
    }

    // TU80 encodes the tail boxes along Z. Keep the tip attached as the base swings.
    const float tailLength = mTail2->mWorldZ - mTail1->mWorldZ;
    mTail2->mY = mTail1->mY - Mth::sin(mTail1->mXRot) * tailLength;
    mTail2->mZ = mTail1->mZ + Mth::cos(mTail1->mXRot) * tailLength;
}

void OcelotModel::prepareMobModel(boost::shared_ptr<LivingEntity>& entity, float limbSwing, float limbSwingAmount,
                                  float partialTicks)
{
    boost::shared_ptr<Ocelot> ocelot = boost::dynamic_pointer_cast<Ocelot>(entity);
    const auto cat = boost::dynamic_pointer_cast<Cat>(entity);
    mRelaxing = cat ? cat->getRelaxingProgress(partialTicks) : 0.0f;
    mRelaxing = mRelaxing * mRelaxing * (3.0f - 2.0f * mRelaxing);

    for (ModelPart* part : {mBody, mHead, mTail1, mTail2, mFrontLegL, mFrontLegR, mBackLegL, mBackLegR})
        part->setPos(part->mWorldX, part->mWorldY, part->mWorldZ);
    mTail1->mXRot = 0.35f;
    mBody->mXRot = 0.0f;

    if (ocelot->isSneaking())
    {
        mBody->mY += 1.0f;
        mHead->mY += 2.0f;
        mTail1->mY += 1.0f;
        mTail1->mXRot = 0.16f;
        mPose = 0;
    }
    else if (ocelot->isSprinting())
    {
        mTail1->mXRot = 0.05f;
        mPose = 2;
    }
    else if (ocelot->isSitting())
    {
        mBody->mXRot = -0.78f;
        mBody->mY -= 0.6f;
        mBody->mZ += 1.0f;
        mTail1->mY += 7.5f;
        mTail1->mZ -= 0.5f;
        mTail1->mXRot = 0.08f;
        mTail2->mXRot = 0.17f;
        mFrontLegL->mXRot = 0.0f;
        mFrontLegL->mZ += 0.25f;
        mFrontLegR->mXRot = 0.0f;
        mFrontLegR->mZ += 0.25f;
        mBackLegL->mXRot = -1.5707964f;
        mBackLegL->mY += 5.0f;
        mBackLegL->mZ -= 1.0f;
        mBackLegR->mXRot = -1.5707964f;
        mBackLegR->mY += 5.0f;
        mBackLegR->mZ -= 1.0f;
        mPose = 3;
    }
    else
    {
        mPose = 1;
    }
}
