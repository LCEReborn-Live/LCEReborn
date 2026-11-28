#ifndef GAME_WORLD_BLOCK_BAMBOOBLOCK_HPP
#define GAME_WORLD_BLOCK_BAMBOOBLOCK_HPP

#include "game/core/Bush.hpp"
#include "game/world/block/BonemealableBlock.hpp"
#include "game/world/block/CuboidBlockModel.hpp"

class BambooBlock : public Bush, public BonemealableBlock
{
public:
    BambooBlock();
    bool mayPlaceOn(Level*, const BlockPos&, const BlockState*) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                           int, boost::shared_ptr<LivingEntity>) override;
    void tick(Level*, const BlockPos&, const BlockState*, Random*) override;
    void neighborChanged(const BlockState*, Level*, const BlockPos&, Block*, const BlockPos&) override;
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    const AABB* getClipAABB(const BlockState*, LevelSource*, const BlockPos&) override;
    EBlockRenderShape getRenderShape(const BlockState*) override;
    Item* getResource(const BlockState*, Random*, int) override;
    not_null_ptr<ItemInstance> getCloneItemInstance(Level*, const BlockPos&, const BlockState*) override;
    bool isValidBonemealTarget(Level*, const BlockPos&, const BlockState*, bool) override;
    bool isBonemealSuccess(Level*, Random*, const BlockPos&, const BlockState*) override;
    void performBonemeal(Level*, Random*, const BlockPos&, const BlockState*) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    TextureAtlasSprite* getTexture(const Direction*, const BlockState*) override;
    void registerIcons(IconRegister*) override;

    bool placeStalk(Level*, const BlockPos& base, int height);
    const std::vector<CuboidBlockModel::Part>& getModel(const BlockState*) const;
    TextureAtlasSprite* const* getModelTextures() const { return mTextures.data(); }

private:
    BlockPos findTop(Level*, const BlockPos&);
    bool canGrow(Level*, const BlockPos& top);
    bool grow(Level*, const BlockPos& top);
    std::array<TextureAtlasSprite*, 4> mTextures{};
    std::array<std::vector<CuboidBlockModel::Part>, 6> mModels;
};

#endif
