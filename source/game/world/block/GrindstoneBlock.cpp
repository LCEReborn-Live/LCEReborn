#include "game/world/block/GrindstoneBlock.hpp"

#include <algorithm>
#include "game/core/Direction.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/core/StringIds.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/ui/BlockTooltipDataHolder.hpp"
#include "game/ui/GrindstoneMenu.hpp"
#include "game/util/AABB.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/LocalBlockInteraction.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"

AbstractProperty<int>* GrindstoneBlock::MOUNT =
    static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"mount", 0, 2));

namespace
{
enum Mount { Floor, Wall, Ceiling };

std::vector<GrindstoneBlock::ModelPart> fallbackModel()
{
    std::vector<GrindstoneBlock::ModelPart> parts = {
        {{{ {2, 0, 6}, {4, 10, 10} }}, {}},
        {{{ {12, 0, 6}, {14, 10, 10} }}, {}},
        {{{ {4, 4, 2}, {12, 16, 14} }}, {}},
    };
    for (auto& part : parts)
        for (auto& face : part.faces)
            face = {{1, 1, 15, 15}, 3, -1};
    for (size_t side = 0; side < parts.back().faces.size(); ++side)
        parts.back().faces[side].texture = side >= 4 ? 1 : 2;
    return parts;
}

class GrindstoneInteraction : public LocalBlockInteraction
{
public:
    GrindstoneInteraction(Level* level, const BlockPos& pos)
        : LocalBlockInteraction(ClientboundContainerOpenPacket::GRINDSTONE, L""), mLevel(level), mPos(pos) {}

    AbstractContainerMenu* createMenu(boost::shared_ptr<Inventory> inventory, boost::shared_ptr<Player>) override
    {
        return new GrindstoneMenu(inventory, mLevel, mPos);
    }
private:
    Level* mLevel;
    BlockPos mPos;
};

const AABB* partBox(const BlockState* state, const GrindstoneBlock::ModelBox& box)
{
    auto low = GrindstoneBlock::orientPoint(state, box[0]);
    auto high = GrindstoneBlock::orientPoint(state, box[1]);
    for (int axis = 0; axis < 3; ++axis)
    {
        if (low[axis] > high[axis])
            std::swap(low[axis], high[axis]);
    }
    return AABB::newTemp(low[0] / 16, low[1] / 16, low[2] / 16, high[0] / 16, high[1] / 16, high[2] / 16);
}
}

GrindstoneBlock::GrindstoneBlock() : HorizontalDirectionalBlock(Material::STONE)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(FACING, Direction::NORTH)->setValue(MOUNT, int(Floor)));
    setLightBlock(0);
    mModel = CuboidBlockModel::read(L"Common/res/TitleUpdate/res/grindstone_model.bin", 4);
    mImportedModel = !mModel.empty();
    if (!mImportedModel)
        mModel = fallbackModel();
}

GrindstoneBlock::ModelPoint GrindstoneBlock::orientPoint(const BlockState* state, ModelPoint point)
{
    const int mount = state->getValue(MOUNT);
    if (mount == Ceiling)
        point = {point[0], 16 - point[1], 16 - point[2]};
    else if (mount == Wall)
        point = {point[0], 16 - point[2], point[1]};

    const auto* facing = state->getValue(FACING);
    if (facing == Direction::EAST)
        return {point[2], point[1], 16 - point[0]};
    if (facing == Direction::NORTH)
        return {16 - point[0], point[1], 16 - point[2]};
    if (facing == Direction::WEST)
        return {16 - point[2], point[1], point[0]};
    return point;
}

const AABB* GrindstoneBlock::getShape(const BlockState* state, LevelSource*, const BlockPos&)
{
    return partBox(state, {{ {2, 0, 2}, {14, 16, 14} }});
}

void GrindstoneBlock::addCollisionAABBs(const BlockState* state, Level*, const BlockPos& pos, const AABB* query,
                                       std::vector<AABB*>* colliding, boost::shared_ptr<Entity>, bool)
{
    for (const auto& part : mModel)
        addCollisionAABB(pos, query, colliding, partBox(state, part.bounds));
}

bool GrindstoneBlock::use(Level* level, const BlockPos& pos, const BlockState*, boost::shared_ptr<Player> player,
                         InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide)
{
    if (clientSide)
        return false;
    if (!level->mIsClientSide)
    {
        GrindstoneInteraction interaction(level, pos);
        player->startBlockInteraction(interaction);
    }
    return true;
}

unsigned int GrindstoneBlock::GetInteractTooltip(const BlockTooltipDataHolder& data)
{
    return (data.m_bUseBlock || data.m_bUseItemOnBlock) && data.m_bCanPlace
        ? IDS_TOOLTIPS_USE : Block::GetInteractTooltip(data);
}

const BlockState* GrindstoneBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(FACING, Direction::from2DDataValue(data & 3))
        ->setValue(MOUNT, std::min(2, (data >> 2) & 3));
}

int GrindstoneBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return state->getValue(FACING)->get2DDataValue() | (state->getValue(MOUNT) << 2);
}

const BlockState* GrindstoneBlock::getStateForPlacement(Level*, const BlockPos&, const Direction* face, float, float,
                                                       float, int, boost::shared_ptr<LivingEntity> placer)
{
    const int mount = face == Direction::UP ? Floor : face == Direction::DOWN ? Ceiling : Wall;
    const auto* facing = mount == Wall ? face : placer->getDirection()->getOpposite();
    return defaultBlockState()->setValue(FACING, facing)->setValue(MOUNT, mount);
}

const BlockState* GrindstoneBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(FACING, rotation->rotate(state->getValue(FACING)));
}

const BlockState* GrindstoneBlock::mirror(const BlockState* state, Mirror* mirror)
{
    return rotate(state, mirror->getRotation(state->getValue(FACING)));
}

BlockStateDefinition* GrindstoneBlock::createBlockStateDefinition()
{
    const Property* properties[] = {FACING, MOUNT};
    return new BlockStateDefinition(this, properties, 2);
}

void GrindstoneBlock::registerIcons(IconRegister* icons)
{
    const wchar_t* javaTextures[] = {L"grindstone_java_pivot", L"grindstone_java_side",
                                    L"grindstone_java_round", L"grindstone_java_leg"};
    const wchar_t* fallbackTextures[] = {L"grindstone_axle", L"grindstone_wheel", L"grindstone_rim", L"log_big_oak"};
    for (size_t i = 0; i < mModelTextures.size(); ++i)
        mModelTextures[i] = icons->registerIcon(mImportedModel ? javaTextures[i] : fallbackTextures[i]);
    mIcon = mModelTextures[1];
}
