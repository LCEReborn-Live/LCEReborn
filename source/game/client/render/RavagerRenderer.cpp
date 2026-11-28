#include "game/client/render/RavagerRenderer.hpp"
#include "game/client/render/RavagerModel.hpp"
#include "game/util/ResourceLocation.hpp"

namespace { const ResourceLocation texture(L"mob/illager/ravager"); }
RavagerRenderer::RavagerRenderer(EntityRenderDispatcher* dispatcher)
    : MobRenderer(dispatcher, new RavagerModel(), 1.1f) {}
const ResourceLocation* RavagerRenderer::getTextureLocation(boost::shared_ptr<Entity>&) { return &texture; }
Model* RavagerRenderer::createModel(const ModelDefinition& definition) { return new RavagerModel(definition); }
