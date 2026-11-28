#include "game/client/render/PillagerRenderer.hpp"

#include "game/client/render/IllagerModel.hpp"
#include "game/client/render/ModelPart.hpp"
#include "game/util/ResourceLocation.hpp"
#include "game/world/generation/CustomHeadLayer.hpp"
#include "game/world/generation/ItemInHandLayer.hpp"

namespace
{
const ResourceLocation pillagerTexture(L"mob/pillager");

class PillagerCrossbowLayer : public ItemInHandLayer
{
public:
    explicit PillagerCrossbowLayer(PillagerRenderer* renderer) : ItemInHandLayer(renderer) {}

    void translateToHand(const HumanoidArm* arm) override
    {
        static_cast<IllagerModel*>(mLivingEntityRenderer->getModel())->getArm(arm)->translateTo(0.0625f);
    }
};

class PillagerHeadLayer : public CustomHeadLayer
{
public:
    explicit PillagerHeadLayer(PillagerRenderer* renderer) : CustomHeadLayer(nullptr), mRenderer(renderer) {}

    void render(boost::shared_ptr<LivingEntity>& entity, float swing, float swingAmount, float partialTicks,
                float age, float yaw, float pitch, float scale, bool useDisplayList) override
    {
        mHead = static_cast<IllagerModel*>(mRenderer->getModel())->mHead;
        CustomHeadLayer::render(entity, swing, swingAmount, partialTicks, age, yaw, pitch, scale, useDisplayList);
    }

private:
    PillagerRenderer* mRenderer;
};
}

PillagerRenderer::PillagerRenderer(EntityRenderDispatcher* dispatcher)
    : MobRenderer(dispatcher, new IllagerModel(0.0f, 0.0f, 64, 64), 0.5f)
{
    addLayer(new PillagerCrossbowLayer(this));
    addLayer(new PillagerHeadLayer(this));
}

const ResourceLocation* PillagerRenderer::getTextureLocation(boost::shared_ptr<Entity>&)
{
    return &pillagerTexture;
}

std::wstring PillagerRenderer::getModelName()
{
    return L"pillager";
}

Model* PillagerRenderer::createModel(const ModelDefinition& definition)
{
    return new IllagerModel(0.0f, 0.0f, 64, 64, definition);
}
