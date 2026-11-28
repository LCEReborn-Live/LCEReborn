#ifndef GAME_WORLD_BLOCK_STONECUTTERBLOCK_HPP
#define GAME_WORLD_BLOCK_STONECUTTERBLOCK_HPP

#include "game/world/block/CuboidBlockModel.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"

class StonecutterBlock : public HorizontalDirectionalBlock
{
public:
    StonecutterBlock();
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>,
             InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide) override;
    unsigned int GetInteractTooltip(const BlockTooltipDataHolder&) override;

    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_STONECUTTER; }
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float, int,
                                          boost::shared_ptr<LivingEntity>) override;
    const BlockState* rotate(const BlockState*, Rotation*) override;
    const BlockState* mirror(const BlockState*, Mirror*) override;
    BlockStateDefinition* createBlockStateDefinition() override;

    void registerIcons(IconRegister*) override;
    const std::vector<CuboidBlockModel::Part>& getModel() const { return mModel; }
    TextureAtlasSprite* const* getModelTextures() const { return mTextures.data(); }
    static CuboidBlockModel::Point orientPoint(const BlockState*, CuboidBlockModel::Point);

private:
    std::vector<CuboidBlockModel::Part> mModel;
    std::array<TextureAtlasSprite*, 4> mTextures{};
    bool mImportedModel;
};

#endif
