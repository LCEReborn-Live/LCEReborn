#pragma once
#include "game/client/render/Model.hpp"
#include "game/client/render/ModelDefinition.hpp"

class RavagerModel : public Model
{
public:
    explicit RavagerModel(const ModelDefinition& definition = ModelDefinition());
    void render(boost::shared_ptr<Entity>& entity, float swing, float amount, float age, float yaw,
                float pitch, float scale, bool useDisplayList) override;
    void setupAnim(float swing, float amount, float age, float yaw, float pitch, float scale,
                   boost::shared_ptr<Entity> entity, _SkinAdjustments adjustments) override;
    void prepareMobModel(boost::shared_ptr<LivingEntity>& entity, float swing, float amount, float partial) override;
    ModelPart *mNeck, *mHead, *mMouth, *mBody, *mLegs[4];
private:
    float mNeckX, mNeckY, mNeckZ, mNeckRot, mHeadRot;
};
