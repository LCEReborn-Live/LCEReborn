#pragma once
#include "game/client/render/MobRenderer.hpp"

class RavagerRenderer : public MobRenderer
{
public:
    explicit RavagerRenderer(EntityRenderDispatcher* dispatcher);
    const ResourceLocation* getTextureLocation(boost::shared_ptr<Entity>& entity) override;
    std::wstring getModelName() override { return L"ravager"; }
    Model* createModel(const ModelDefinition& definition) override;
};
