#ifndef GAME_WORLD_BLOCK_SCAFFOLDINGBLOCK_HPP
#define GAME_WORLD_BLOCK_SCAFFOLDINGBLOCK_HPP

#include "game/world/block/BlockItem.hpp"
#include "game/world/block/CuboidBlockModel.hpp"
#include "game/world/block/FallingBlock.hpp"

class ScaffoldingBlock : public FallingBlock
{
public:
    ScaffoldingBlock();
    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    int getRenderLayer() override { return 0; }
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_SCAFFOLDING; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    bool canProvideSupport(const BlockState*, const Direction*, EBlockSupportType) override;
    const AABB* getClipAABB(const BlockState*, LevelSource*, const BlockPos&) override { return nullptr; }
    void addCollisionAABBs(const BlockState*, Level*, const BlockPos&, const AABB*, std::vector<AABB*>*,
                          boost::shared_ptr<Entity>, bool) override;
    void tick(Level*, const BlockPos&, const BlockState*, Random*) override;
    void onLand(Level*, const BlockPos&, const BlockState*, const BlockState*) override;
    bool canSurvive(Level*, const BlockPos&, const BlockState*) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                          int, boost::shared_ptr<LivingEntity>) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    void registerIcons(IconRegister*) override;
    TextureAtlasSprite* getTexture(const Direction*, const BlockState*) override;
    const std::vector<CuboidBlockModel::Part>& getModel(const BlockState* state) const;
    TextureAtlasSprite* const* getModelTextures() const { return mTextures.data(); }

private:
    int supportDistance(Level*, const BlockPos&);
    const BlockState* stateForPosition(Level*, const BlockPos&);
    std::array<TextureAtlasSprite*, 3> mTextures{};
    std::vector<CuboidBlockModel::Part> mStable, mBottom;
};

class ScaffoldingItem : public BlockItem
{
public:
    explicit ScaffoldingItem(Block* block) : BlockItem(block) {}
    bool mayPlace(Level*, const BlockPos&, const Direction*, boost::shared_ptr<Player>,
                  not_null_ptr<ItemInstance>) override;
    InteractionResult::EInteractionResult useOn(boost::shared_ptr<Player>, Level*, const BlockPos&,
                                                InteractionHand::EInteractionHand, const Direction*,
                                                float, float, float, bool) override;

private:
    bool findExtension(Level*, const BlockPos&, const Direction*, Player&, BlockPos&, const Direction*&);
};

#endif
