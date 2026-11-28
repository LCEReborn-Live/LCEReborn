#include "game/client/render/CampfireRenderer.hpp"

#include "game/client/render/ItemRenderer.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/Minecraft.hpp"
#include "game/world/block/CampfireBlock.hpp"

void CampfireRenderer::render(boost::shared_ptr<BlockEntity> entity, double x, double y, double z, float,
                              int breakProgress, bool, float alpha, bool)
{
    if (breakProgress >= 0)
        return;
    const auto fire = boost::static_pointer_cast<CampfireBlockEntity>(entity);
    auto* renderer = Minecraft::GetInstance()->getItemRenderer();
    const auto& food = fire->food();
    constexpr float positions[][2] = {{.25f, .25f}, {.75f, .25f}, {.75f, .75f}, {.25f, .75f}};
    for (size_t slot = 0; slot < food.size(); ++slot)
    {
        if (food[slot]->isEmpty())
            continue;
        GlStateManager::pushMatrix();
        GlStateManager::translatef(float(x + positions[slot][0]), float(y + 7.5 / 16), float(z + positions[slot][1]));
        GlStateManager::rotatef(float(slot) * 90.f, 0, 1, 0);
        GlStateManager::rotatef(90.f, 1, 0, 0);
        GlStateManager::scalef(.36f, .36f, .36f);
        renderer->renderStatic(food[slot], {}, ItemTransforms::NONE, alpha, false, nullptr);
        GlStateManager::popMatrix();
    }
}
