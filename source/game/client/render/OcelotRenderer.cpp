#include "game/client/render/OcelotRenderer.hpp"

#include <array>

#include "game/client/render/ModelDefinition.hpp"
#include "game/client/render/OcelotModel.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/Cat.hpp"
#include "game/core/Mth.hpp"
#include "game/core/Ocelot.hpp"
#include "game/entity/mob/LivingEntity.hpp"
#include "game/util/ResourceLocation.hpp"

static ResourceLocation sOcelotTexture(TN_MOB_OZELOT);

OcelotRenderer::OcelotRenderer(EntityRenderDispatcher* dispatcher, bool cat)
    : MobRenderer(dispatcher, new OcelotModel(cat ? L"cat" : L"ocelot"), 0.4F), mCat(cat)
{
}

const ResourceLocation* OcelotRenderer::getTextureLocation(boost::shared_ptr<Entity>& entity)
{
    if (entity->GetType() != EntityType::CAT)
        return &sOcelotTexture;

    static const auto textures = [] {
        const wchar_t* names[] = {L"tuxedo", L"redtabby", L"siamesecat", L"tabby", L"white", L"ragdoll",
                                  L"persian", L"britishshorthair", L"calico", L"jellie", L"allblackcat"};
        std::array<std::array<ResourceLocation, 2>, Cat::AllBlack> result;
        for (int variant = 0; variant < Cat::AllBlack; ++variant)
        {
            const std::wstring path = std::wstring(L"mob/cat/") + names[variant];
            result[variant][0] = ResourceLocation(path);
            result[variant][1] = ResourceLocation(path + L"_tame");
        }
        return result;
    }();
    const auto cat = boost::static_pointer_cast<Cat>(entity);
    const int variant = Mth::clamp(cat->getCatType(), static_cast<int>(Cat::Tuxedo), static_cast<int>(Cat::AllBlack));
    return &textures[variant - Cat::Tuxedo][cat->isTame() ? 1 : 0];
}

void OcelotRenderer::scale(boost::shared_ptr<LivingEntity>& entity, float partialTicks)
{
    LivingEntityRenderer::scale(entity, partialTicks);
    if (entity->GetType() == EntityType::CAT)
        GlStateManager::scalef(0.8F, 0.8F, 0.8F);
}

Model* OcelotRenderer::createModel(const ModelDefinition& definition)
{
    return new OcelotModel(definition, mCat ? L"cat" : L"ocelot");
}
