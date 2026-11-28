#ifndef GAME_WORLD_BLOCK_COMPOSTERBLOCK_HPP
#define GAME_WORLD_BLOCK_COMPOSTERBLOCK_HPP

#include "game/world/block/Block.hpp"
#include "game/world/block/CuboidBlockModel.hpp"

class Container;

class ComposterBlock : public Block
{
public:
    static AbstractProperty<int>* LEVEL;
    static constexpr int Full = 7;
    static constexpr int Ready = 8;

    ComposterBlock();
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>,
             InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide) override;
    unsigned int GetInteractTooltip(const BlockTooltipDataHolder& data) override;
    void onPlace(Level*, const BlockPos&, const BlockState*) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float, int,
                                           boost::shared_ptr<LivingEntity>) override;
    void tick(Level*, const BlockPos&, const BlockState*, Random*) override;
    bool hasAnalogOutputSignal(const BlockState*) override { return true; }
    int getAnalogOutputSignal(const BlockState* state, Level*, const BlockPos&) override;
    const BlockState* getBlockState(int data) override;
    int convertBlockStateToLegacyData(const BlockState* state) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    bool isSolidRender(const BlockState*) const override { return false; }
    bool isCubeShaped(const BlockState*) override { return false; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    void addCollisionAABBs(const BlockState*, Level*, const BlockPos&, const AABB*, std::vector<AABB*>*,
                           boost::shared_ptr<Entity>, bool) override;
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_COMPOSTER; }
    void registerIcons(IconRegister* icons) override;
    TextureAtlasSprite* getTexture(const Direction* direction, const BlockState* state) override;
    const std::vector<CuboidBlockModel::Part>& getModel() const { return mModel; }
    const CuboidBlockModel::Part* getContents(const BlockState* state) const;
    TextureAtlasSprite* const* getModelTextures() const { return mTextures.data(); }

    static int compostChance(not_null_ptr<ItemInstance> item);
    bool addCompost(Level* level, const BlockPos& pos, not_null_ptr<ItemInstance> item);
    bool emptyCompost(Level* level, const BlockPos& pos);
    boost::shared_ptr<Container> hopperContainer(Level* level, const BlockPos& pos, const BlockState* state);

private:
    bool setFillLevel(Level* level, const BlockPos& pos, const BlockState* state, int fill);
    std::vector<CuboidBlockModel::Part> mModel;
    std::array<CuboidBlockModel::Part, Ready> mContents;
    std::array<TextureAtlasSprite*, 5> mTextures{};
};

#endif
