#ifndef GAME_CLIENT_RENDER_PANDARENDERER_HPP
#define GAME_CLIENT_RENDER_PANDARENDERER_HPP

#include "game/client/render/MobRenderer.hpp"

class PandaRenderer : public MobRenderer
{
public:
    explicit PandaRenderer(EntityRenderDispatcher* dispatcher);
    const ResourceLocation* getTextureLocation(boost::shared_ptr<Entity>& entity) override;
    Model* createModel(const ModelDefinition& definition) override;
};

#endif
