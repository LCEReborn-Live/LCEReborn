#ifndef GAME_WORLD_BLOCK_CAMPFIREBLOCK_HPP
#define GAME_WORLD_BLOCK_CAMPFIREBLOCK_HPP

#include "game/item/ItemInstance.hpp"
#include "game/world/block/BaseEntityBlock.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/block/CuboidBlockModel.hpp"

class CampfireBlockEntity : public BlockEntity
{
public:
    static constexpr size_t SlotCount = 4;
    static constexpr int CookingTime = 600;
    static constexpr int UpdatePacketType = 14;
    CampfireBlockEntity();
    static BlockEntity* create();
    unsigned long long GetType() override;
    boost::shared_ptr<BlockEntity> clone() override;
    void load(CompoundTag*) override;
    CompoundTag* save(CompoundTag*) override;
    CompoundTag* getUpdateTag() override;
    boost::shared_ptr<Packet> getUpdatePacket() override;
    void tick() override;
    bool hasSpace() const;
    bool placeFood(not_null_ptr<ItemInstance> item);
    void dropFood();
    const std::array<not_null_ptr<ItemInstance>, SlotCount>& food() const { return mFood; }

private:
    void notifyFoodChanged();
    std::array<not_null_ptr<ItemInstance>, SlotCount> mFood;
    std::array<int, SlotCount> mProgress{};
    int mSmokeDelay = 0;
};

class CampfireBlock : public BaseEntityBlock
{
public:
    CampfireBlock();
    static bool isLit(const BlockState*);
    static CuboidBlockModel::Point orientPoint(const BlockState*, CuboidBlockModel::Point);
    boost::shared_ptr<BlockEntity> newBlockEntity(Level*, int) override;
    bool isCubeShaped(const BlockState*) override { return false; }
    bool isSolidRender(const BlockState*) const override { return false; }
    int getRenderLayer() override { return 0; }
    bool canContainLiquid(const BlockState*) const override { return true; }
    std::wstring getTileItemIconName() override { return L"campfire"; }
    int getLightEmission(const BlockState*) override;
    EBlockRenderShape getRenderShape(const BlockState*) override { return BLOCK_RENDER_CAMPFIRE; }
    EBlockFaceShape getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*) override;
    const AABB* getShape(const BlockState*, LevelSource*, const BlockPos&) override;
    bool TestUse() override { return true; }
    bool use(Level*, const BlockPos&, const BlockState*, boost::shared_ptr<Player>, InteractionHand::EInteractionHand,
             const Direction*, float, float, float, bool) override;
    void entityInside(Level*, const BlockPos&, const BlockState*, const boost::shared_ptr<Entity>&) override;
    void animateTick(const BlockState*, Level*, const BlockPos&, Random*) override;
    void onRemove(Level*, const BlockPos&, const BlockState*) override;
    Item* getResource(const BlockState*, Random*, int) override;
    int getSpawnResourcesAuxValue(const BlockState*) override { return 1; }
    int getResourceCount(Random*) override { return 2; }
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

private:
    std::vector<CuboidBlockModel::Part> mLitModel, mUnlitModel;
    std::array<TextureAtlasSprite*, 3> mTextures{};
};

#endif
