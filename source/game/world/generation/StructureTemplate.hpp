#ifndef GAME_WORLD_GENERATION_STRUCTURETEMPLATE_HPP
#define GAME_WORLD_GENERATION_STRUCTURETEMPLATE_HPP

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

#include "game/world/block/BlockPos.hpp"

class BlockState;
class BoundingBox;
class CompoundTag;
class DataFixerUpper;
class Direction;
class Level;
class Mirror;
class Rotation;
class StructurePlaceSettings;
class StructureProcessor;
class Vec3;


class StructureTemplate
{
public:
    class StructureBlockInfo
    {
    public:
        struct byColumnSorter
        {
            bool operator()(const StructureBlockInfo* left, const StructureBlockInfo* right) const
            {
                return (left->mPos.x << 16 | (left->mPos.z & 0xFFFF))
                       < (right->mPos.x << 16 | (right->mPos.z & 0xFFFF));
            }
        };

        StructureBlockInfo(const BlockPos& pos, const BlockState* state, CompoundTag* nbt,
                           const BlockState* extraState = nullptr)
        {
            mPos = pos;
            mState = state;
            mNbt = nbt;
            mExtraState = extraState;
        }

        ~StructureBlockInfo();

        BlockPos mPos;
        const BlockState* mState;
        CompoundTag* mNbt;
        const BlockState* mExtraState;
    };

    class StructureEntityInfo
    {
    public:
        StructureEntityInfo(Vec3* pos, const BlockPos& blockPos, CompoundTag* nbt)
        {
            mPos = pos;
            mBlockPos = blockPos;
            mNbt = nbt;
        }

        ~StructureEntityInfo();

        Vec3* mPos;
        BlockPos mBlockPos;
        CompoundTag* mNbt;
    };

    typedef boost::unordered_map<const BlockPos, std::wstring, boost::hash<const BlockPos>,
                                 std::equal_to<const BlockPos>,
                                 std::allocator<std::pair<const BlockPos, std::wstring> > >
        MarkerMap;

    StructureTemplate();
    virtual ~StructureTemplate();

    BlockPos getSize() const;
    BoundingBox* getTransformedBounds(const BlockPos& origin, Mirror* mirror, Rotation* rotation) const;

    struct Connector
    {
        BlockPos pos;
        const Direction* facing;
        std::wstring type;
        std::wstring pool;
        std::wstring target;
    };
    const std::vector<Connector>& getConnectors() const { return mConnectors; }
    const std::vector<StructureBlockInfo*>& getBlocks() const { return mBlocks; }
    const std::vector<StructureEntityInfo*>& getEntities() const { return mEntities; }

    // Takes ownership of the native piece data. No file format is involved.
    void setNative(const BlockPos& size, std::vector<StructureBlockInfo*> blocks,
                   std::vector<StructureEntityInfo*> entities, std::vector<Connector> connectors);

    void setAuthor(const std::wstring& author);
    std::wstring getAuthor();

    static BlockPos transform(const BlockPos& pos, Mirror* mirror, Rotation* rotation);
    static BlockPos transform(const BlockPos& pos, const Direction* direction);
    static Vec3* transform(Vec3* pos, Mirror* mirror, Rotation* rotation);

    static BlockPos calculateRelativePosition(const StructurePlaceSettings* settings, const BlockPos& pos);

    MarkerMap getMarkers(const BlockPos& pos, StructurePlaceSettings* settings) const;

    BlockPos calculateConnectedPosition(const StructurePlaceSettings* settings, const BlockPos& pos,
                                        const StructurePlaceSettings* connectedSettings,
                                        const BlockPos& connectedPos) const;

    void placeInWorld(Level* level, const BlockPos& pos, StructureProcessor* processor,
                      StructurePlaceSettings* settings, int flags) const;
    void placeInWorld(Level* level, const BlockPos& pos, StructurePlaceSettings* settings) const;
    void placeInWorldChunk(Level* level, const BlockPos& pos, StructurePlaceSettings* settings) const;
    void placeInWorld(Level* level, const BlockPos& pos, StructurePlaceSettings* settings, int flags) const;

    BlockPos getSize(Rotation* rotation) const;

    static BlockPos getZeroPositionWithTransform(const BlockPos& pos, Mirror* mirror, Rotation* rotation, int sizeX,
                                                 int sizeZ);
    BlockPos getZeroPositionWithTransform(const BlockPos& pos, Mirror* mirror, Rotation* rotation) const;

    static void addWalkers(DataFixerUpper* fixerUpper);

    bool loadLocalVillage(CompoundTag* tag, std::wstring& problem);
    void load(CompoundTag* tag, bool flattened = false, bool localVillage = false);

private:
    void placeEntities(Level* level, const BlockPos& pos, StructurePlaceSettings* settings) const;

    std::vector<const BlockState*> mPalette;
    std::vector<StructureBlockInfo*> mBlocks;
    std::vector<StructureEntityInfo*> mEntities;
    std::vector<Connector> mConnectors;
    BlockPos mSize;
    std::wstring mAuthor;
};

#endif
