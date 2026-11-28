#include "game/client/render/LecternRenderer.hpp"

#include "game/client/render/BlockEntityRenderDispatcher.hpp"
#include "game/core/Direction.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/LecternBlock.hpp"

LecternRenderer::LecternRenderer()
    : mSpine(&mModel, 12, 0, 64, 32), mLeft(&mModel, 0, 0, 64, 32), mRight(&mModel, 16, 0, 64, 32)
{
    mSpine.addBox(-.5f, -5, 0, 1, 10, 1);

    mLeft.addBox(-6, -5, 0, 6, 10, 0);
    mLeft.texOffs(0, 10)->addBox(-5.5f, -4, .15f, 5, 8, 1);
    mLeft.mYRot = .13f;

    mRight.addBox(0, -5, 0, 6, 10, 0);
    mRight.texOffs(12, 10)->addBox(.5f, -4, .15f, 5, 8, 1);
    mRight.mYRot = -.13f;
}

void LecternRenderer::render(boost::shared_ptr<BlockEntity> entity, double x, double y, double z, float,
                             int breakProgress, bool, float, bool useDisplayList)
{
    const auto lectern = boost::static_pointer_cast<LecternBlockEntity>(entity);
    if (breakProgress >= 0 || !mDispatcher->mLevel || lectern->book()->isEmpty())
        return;
    const auto* state = mDispatcher->mLevel->getBlockState(entity->getBlockPos());
    if (state->getBlock() != Blocks::lectern)
        return;
    static ResourceLocation bookTexture(TN_ITEM_BOOK);
    bindTexture(&bookTexture);
    GlStateManager::pushMatrix();
    GlStateManager::translatef(float(x + .5), float(y + .5), float(z + .5));
    GlStateManager::rotatef(180.f - LecternBlock::facing(state)->get2DDataValue() * 90.f, 0, 1, 0);
    GlStateManager::rotatef(-22.5f, 1, 0, 0);
    GlStateManager::translatef(0, 8.05f / 16, 1.5f / 16);
    GlStateManager::rotatef(-90.f, 1, 0, 0);
    constexpr float modelScale = 1.0f / 16;
    mSpine.render(modelScale, useDisplayList, false);
    mLeft.render(modelScale, useDisplayList, false);
    mRight.render(modelScale, useDisplayList, false);
    GlStateManager::popMatrix();
}
