#ifndef GAME_WORLD_BLOCK_BARRELBLOCKENTITY_HPP
#define GAME_WORLD_BLOCK_BARRELBLOCKENTITY_HPP

#include "game/world/block/RandomizableLockableContainerBlockEntity.hpp"

class DataFixerUpper;

class BarrelBlockEntity : public RandomizableLockableContainerBlockEntity
{
public:
    BarrelBlockEntity();
    ~BarrelBlockEntity() override;
    static BlockEntity* create();
    static void addDataWalkers(DataFixerUpper* fixer);

    unsigned int getContainerSize() override { return 27; }
    int getMaxStackSize() override { return 64; }
    bool isEmpty() override;
    std::wstring getName() override;
    std::wstring getCustomName() override;
    NonNullList<not_null_ptr<ItemInstance>> getItems() override { return mItems; }
    void setItems(NonNullList<not_null_ptr<ItemInstance>> items) override;
    void setChanged() override { BlockEntity::setChanged(); }
    int getTypeId(bool large) override;
    AbstractContainerMenu* createMenu(boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Player> player) override;

    void load(CompoundTag* tag) override;
    CompoundTag* save(CompoundTag* tag) override;
    boost::shared_ptr<BlockEntity> clone() override;
    unsigned long long GetType() override;
    void startOpen(boost::shared_ptr<Player> player) override;
    void stopOpen(boost::shared_ptr<Player> player) override;
    void tick() override;

private:
    void setOpen(bool open, bool playSound);
    NonNullList<not_null_ptr<ItemInstance>> mItems;
    int mOpenCount = 0;
    bool mCheckLoadedState = true;
};

#endif
