#ifndef GAME_CLIENT_RENDER_VILLAGERRENDERER_HPP
#define GAME_CLIENT_RENDER_VILLAGERRENDERER_HPP

#include <boost/shared_ptr.hpp>
#include <vector>

#include "game/client/render/MobRenderer.hpp"

class Entity;
class EntityRenderDispatcher;
class CustomHeadLayer;
class LivingEntity;
class Model;
class ModelDefinition;
class ResourceLocation;

class VillagerRenderer : public MobRenderer
{
public:
    VillagerRenderer(EntityRenderDispatcher* dispatcher);

    void render(boost::shared_ptr<Entity>& entity, double x, double y, double z, float yaw, float partialTick) override;
    const ResourceLocation* getTextureLocation(boost::shared_ptr<Entity>& entity) override;
    Model* getModel() override;
    Model* createModel(const ModelDefinition& definition) override;
    void setupRotations(boost::shared_ptr<LivingEntity>& entity, float ageInTicks, float yaw,
                        float partialTick) override;
    void scale(boost::shared_ptr<LivingEntity>& entity, float partialTicks) override;
    void setupForTexturePack(TexturePack* pack) override;
    std::wstring getModelName() override;

private:
    CustomHeadLayer* mHeadLayer;
    bool mUseTu80Appearance = false;
    std::vector<bool> mMasonOutfits;
};

class WanderingTraderRenderer : public MobRenderer
{
public:
    explicit WanderingTraderRenderer(EntityRenderDispatcher* dispatcher);
    const ResourceLocation* getTextureLocation(boost::shared_ptr<Entity>& entity) override;
    Model* createModel(const ModelDefinition& definition) override;
};

#endif
