#ifndef GAME_CLIENT_RENDER_PILLAGERRENDERER_HPP
#define GAME_CLIENT_RENDER_PILLAGERRENDERER_HPP

#include "game/client/render/MobRenderer.hpp"

class PillagerRenderer : public MobRenderer
{
public:
    explicit PillagerRenderer(EntityRenderDispatcher* dispatcher);
    const ResourceLocation* getTextureLocation(boost::shared_ptr<Entity>& entity) override;
    std::wstring getModelName() override;
    Model* createModel(const ModelDefinition& definition) override;
};

#endif
