#ifndef GAME_WORLD_BLOCK_BELLBLOCK_HPP
#define GAME_WORLD_BLOCK_BELLBLOCK_HPP

#include "game/world/block/BaseEntityBlock.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/block/CuboidBlockModel.hpp"

class BellBlockEntity : public BlockEntity
{
public:
    static BlockEntity* create();
    unsigned long long GetType() override;
    boost::shared_ptr<BlockEntity> clone() override;
    void load(CompoundTag*) override;
    CompoundTag* save(CompoundTag*) override;
    bool triggerEvent(int id, int data) override;
    void tick() override;
    float swing(float partialTick) const;
    bool wasPowered = false;

private:
    int mRingTicks = 0;
    int mResonanceTicks = 0;
};

class BellBlock : public BaseEntityBlock
{
public:
    BellBlock();
    boost::shared_ptr<BlockEntity> newBlockEntity(Level*, int) override;
    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_BELL; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    std::wstring getTileItemIconName() override { return L"bell"; }
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>, InteractionHand::EInteractionHand,
             const Direction*, float, float, float, bool) override;
    void neighborChanged(const BlockState*, Level*, const BlockPos&, Block*, const BlockPos&) override;
    bool mayPlace(Level*, const BlockPos&, const Direction*) override;
    const BlockState* getStateForPlacement(Level*, const BlockPos&, const Direction*, float, float, float,
                                           int, boost::shared_ptr<LivingEntity>) override;
    const BlockState* getBlockState(int) override;
    int convertBlockStateToLegacyData(const BlockState*) override;
    const BlockState* rotate(const BlockState*, Rotation*) override;
    const BlockState* mirror(const BlockState*, Mirror*) override;
    BlockStateDefinition* createBlockStateDefinition() override;
    void registerIcons(IconRegister*) override;
    const std::vector<CuboidBlockModel::Part>& getModel(const BlockState*) const;
    TextureAtlasSprite* const* getModelTextures() const { return mTextures.data(); }
    static const Direction* facing(const BlockState*);
    static CuboidBlockModel::Point orientPoint(const BlockState*, CuboidBlockModel::Point);
    bool ring(Level*, const BlockPos&);

private:
    std::array<std::vector<CuboidBlockModel::Part>, 4> mModels;
    std::array<TextureAtlasSprite*, 2> mTextures{};
};

#endif
