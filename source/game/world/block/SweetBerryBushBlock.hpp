#ifndef GAME_WORLD_BLOCK_SWEETBERRYBUSHBLOCK_HPP
#define GAME_WORLD_BLOCK_SWEETBERRYBUSHBLOCK_HPP

#include <array>
#include "game/core/Bush.hpp"
#include "game/world/block/BonemealableBlock.hpp"

class SweetBerryBushBlock : public Bush, public BonemealableBlock
{
public:
    SweetBerryBushBlock();
    void tick(Level* level, const BlockPos& pos, const BlockState* state, Random* random) override;
    bool TestUse(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>) override;
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>,
             InteractionHand::EInteractionHand, const Direction*, float, float, float, bool) override;
    void entityInside(Level*, const BlockPos&, const BlockState*, const boost::shared_ptr<Entity>&) override;
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    void spawnResources(Level*, const BlockPos&, const BlockState*, float, int) override;
    not_null_ptr<ItemInstance> getCloneItemInstance(Level*, const BlockPos&, const BlockState*) override;
    bool isValidBonemealTarget(Level*, const BlockPos&, const BlockState*, bool) override;
    bool isBonemealSuccess(Level*, Random*, const BlockPos&, const BlockState*) override;
    void performBonemeal(Level*, Random*, const BlockPos&, const BlockState*) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState* state) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    TextureAtlasSprite* getTexture(const Direction*, const BlockState*) override;
    void registerIcons(IconRegister*) override;

private:
    std::array<TextureAtlasSprite*, 4> mStageTextures{};
};

#endif
