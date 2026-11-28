#ifndef GAME_WORLD_BLOCK_LANTERNBLOCK_HPP
#define GAME_WORLD_BLOCK_LANTERNBLOCK_HPP

#include "game/world/block/Block.hpp"
#include "game/world/block/CuboidBlockModel.hpp"

class LanternBlock : public Block
{
public:
    LanternBlock();
    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_LANTERN; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    bool mayPlace(Level*, const BlockPos&) override;
    bool canSurvive(Level*, const BlockPos&, const BlockState*) override;
    void neighborChanged(const BlockState*, Level*, const BlockPos&, Block*, const BlockPos&) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float, int,
                                           boost::shared_ptr<LivingEntity>) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    const std::vector<CuboidBlockModel::Part>& getModel(const BlockState* state) const;

private:
    std::vector<CuboidBlockModel::Part> mStanding;
    std::vector<CuboidBlockModel::Part> mHanging;
};

#endif
