#include "game/client/render/BellRenderer.hpp"

#include "game/client/render/BlockEntityRenderDispatcher.hpp"
#include "game/client/render/Textures.hpp"
#include "game/core/Direction.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BellBlock.hpp"
#include "game/world/block/BlockState.hpp"

BellRenderer::BellRenderer() : mBell(&mModel, 0, 0, 32, 32)
{
    mBell.addBox(-3, 0, -3, 6, 7, 6);
    mBell.texOffs(0, 13)->addBox(-4, 6, -4, 8, 2, 8);
}

void BellRenderer::render(boost::shared_ptr<BlockEntity> entity, double x, double y, double z, float partialTick,
                          int breakProgress, bool, float, bool useDisplayList)
{
    if (!mDispatcher->mTextures || !mDispatcher->mLevel || breakProgress >= 0)
        return;
    const auto bell = boost::static_pointer_cast<BellBlockEntity>(entity);
    const auto* state = mDispatcher->mLevel->getBlockState(entity->getBlockPos());
    if (!dynamic_cast<BellBlock*>(state->getBlock()))
        return;
    mDispatcher->mTextures->bindTexture(L"item/bell");
    GlStateManager::pushMatrix();
    GlStateManager::translatef(float(x + .5), float(y + 13.0 / 16), float(z + .5));
    const auto* face = BellBlock::facing(state);
    GlStateManager::rotatef(180.f - face->get2DDataValue() * 90.f, 0, 1, 0);
    GlStateManager::rotatef(bell->swing(partialTick), 1, 0, 0);
    GlStateManager::scalef(1, -1, -1);
    mBell.render(1.0f / 16.0f, useDisplayList, false);
    GlStateManager::popMatrix();
}
