#include "game/world/block/BellBlock.hpp"

#include <cmath>
#include <algorithm>
#include <memory>
#include "game/client/render/ParticleType.hpp"

#include "game/client/audio/SoundEvents.hpp"
#include "game/client/audio/SoundSource.hpp"
#include "game/core/Direction.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/IconRegister.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/entity/mob/Monster.hpp"
#include "game/entity/mob/Raider.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/util/AABB.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/block/BlockStateDefinition.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/block/HorizontalDirectionalBlock.hpp"
#include "game/world/block/IntegerProperty.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/Village.hpp"
#include "game/world/generation/Villages.hpp"
#include "game/world/generation/Villager.hpp"

namespace
{
enum Attachment { Floor, Ceiling, SingleWall, BetweenWalls };
auto* ATTACHMENT = static_cast<AbstractProperty<int>*>(IntegerProperty::create(L"attachment", 0, 3));
constexpr int RingEvent = 1;
constexpr int RingDuration = 50;
constexpr int HearingDistance = 32;

bool support(Level* level, const BlockPos& pos, const Direction* toward)
{
    return level->canProvideSupport(pos.relative(toward), toward->getOpposite(), BLOCK_SUPPORT_CENTER);
}

CuboidBlockModel::Part standPart(const CuboidBlockModel::Point& low, const CuboidBlockModel::Point& high, int texture)
{
    CuboidBlockModel::Part part{{low, high}, {}};
    const float width = high[0] - low[0];
    const float height = high[1] - low[1];
    const float depth = high[2] - low[2];
    const float top = texture == 1 ? 16 - height : 2;
    const float left = texture == 1 ? 0 : 2;
    part.faces = {{
        {{left, 0, left + width, depth}, texture, -1},
        {{left, 0, left + width, depth}, texture, -1},
        {{left, top, left + width, top + height}, texture, -1},
        {{left, top, left + width, top + height}, texture, -1},
        {{0, top, depth, top + height}, texture, -1},
        {{0, top, depth, top + height}, texture, -1},
    }};
    return part;
}
}

BlockEntity* BellBlockEntity::create() { return new BellBlockEntity(); }
unsigned long long BellBlockEntity::GetType() { return BlockEntityType::BELL; }

boost::shared_ptr<BlockEntity> BellBlockEntity::clone()
{
    auto copy = boost::shared_ptr<BellBlockEntity>(new BellBlockEntity());
    BlockEntity::clone(copy);
    copy->wasPowered = wasPowered;
    return copy;
}

void BellBlockEntity::load(CompoundTag* tag)
{
    BlockEntity::load(tag);
    wasPowered = tag->getBoolean(L"Powered");
}

CompoundTag* BellBlockEntity::save(CompoundTag* tag)
{
    BlockEntity::save(tag);
    tag->putBoolean(L"Powered", wasPowered);
    return tag;
}

bool BellBlockEntity::triggerEvent(int id, int)
{
    if (id != RingEvent)
        return false;
    mRingTicks = RingDuration;
    mResonanceTicks = 0;
    return true;
}

void BellBlockEntity::tick()
{
    if (mRingTicks > 0)
        --mRingTicks;
    if (!mLevel) return;
    if (mRingTicks == RingDuration - 5 && mResonanceTicks == 0)
    {
        std::vector<boost::shared_ptr<Entity>> entities;
        mLevel->getEntities({}, AABB::newTemp(mPos.x-32, mPos.y-32, mPos.z-32,
            mPos.x+33, mPos.y+33, mPos.z+33), &entities);
        for (auto& entity : entities)
            if (boost::dynamic_pointer_cast<Raider>(entity) && entity->isAlive() && entity->distanceToSqr(mPos) < 32*32)
            {
                mResonanceTicks = 40;
                if (!mLevel->mIsClientSide)
                    mLevel->playSound({}, mPos, SoundEvents::BELL_RESONATE, SoundSource::eSoundSource_Blocks, 1, 1, 16);
                break;
            }
    }
    else if (mResonanceTicks > 0 && --mResonanceTicks == 0)
    {
        std::vector<boost::shared_ptr<Entity>> entities;
        mLevel->getEntities({}, AABB::newTemp(mPos.x-48, mPos.y-48, mPos.z-48,
            mPos.x+49, mPos.y+49, mPos.z+49), &entities);
        int nearbyCount = 0;
        for (const auto& entity : entities)
            if (boost::dynamic_pointer_cast<LivingEntity>(entity) && entity->distanceToSqr(mPos) < 48*48) ++nearbyCount;
        int color = 16700985;
        for (auto& entity : entities)
            if (auto raider = boost::dynamic_pointer_cast<Raider>(entity))
                if (raider->isAlive() && raider->distanceToSqr(mPos) < 48*48)
                {
                    if (!mLevel->mIsClientSide) raider->addEffect(new MobEffectInstance(MobEffects::GLOWING, 60), {});
                    else
                    {
                        const double dx = raider->mX - mPos.x, dz = raider->mZ - mPos.z;
                        const double distance = std::sqrt(dx*dx + dz*dz);
                        const double scale = distance > 0 ? 1 / distance : 0;
                        const int count = std::max(3, std::min(15, (nearbyCount - 21) / -2));
                        for (int i = 0; i < count; ++i)
                        {
                            color += 5;
                            mLevel->addParticle(PARTICLE_MOB_SPELL, mPos.x + .5 + dx*scale,
                                mPos.y + .5, mPos.z + .5 + dz*scale,
                                ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0,
                                (color & 255) / 255.0, arrayWithLength<int>());
                        }
                    }
                }
    }
}

float BellBlockEntity::swing(float partialTick) const
{
    if (mRingTicks <= 0)
        return 0;
    const float elapsed = RingDuration - mRingTicks + partialTick;
    return 38.f * std::sin(elapsed * .40f) * (1.f - elapsed / RingDuration);
}

BellBlock::BellBlock() : BaseEntityBlock(Material::METAL)
{
    DerivedInit();
    registerDefaultState(mStateDefinition->any()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, Direction::NORTH)
                                              ->setValue(ATTACHMENT, int(Floor)));
    setLightBlock(0);
    mModels[Floor] = {standPart({2, 13, 7}, {14, 15, 9}, 0), standPart({0, 0, 6}, {2, 16, 10}, 1),
                      standPart({14, 0, 6}, {16, 16, 10}, 1)};
    mModels[Ceiling] = {standPart({7, 13, 7}, {9, 16, 9}, 0)};
    mModels[SingleWall] = {standPart({3, 13, 7}, {16, 15, 9}, 0)};
    mModels[BetweenWalls] = {standPart({0, 13, 7}, {16, 15, 9}, 0)};
}

boost::shared_ptr<BlockEntity> BellBlock::newBlockEntity(Level* level, int)
{
    auto entity = boost::shared_ptr<BellBlockEntity>(new BellBlockEntity());
    entity->setLevel(level);
    return entity;
}

EBlockFaceShape BellBlock::getBlockFaceShape(LevelSource*, const BlockState*, const BlockPos&, const Direction*)
{
    return BLOCK_FACE_UNDEFINED;
}

const AABB* BellBlock::getShape(const BlockState* state, LevelSource*, const BlockPos&)
{
    if (state->getValue(ATTACHMENT) == Floor)
    {
        if (facing(state)->getAxis() == Direction::Axis::X)
            return AABB::newTemp(.3125, 0, 0, .6875, 1, 1);
        return AABB::newTemp(0, 0, .3125, 1, 1, .6875);
    }
    return AABB::newTemp(.25, .25, .25, .75, 1, .75);
}

bool BellBlock::mayPlace(Level* level, const BlockPos& pos, const Direction* face)
{
    return Block::mayPlace(level, pos) && support(level, pos, face->getOpposite());
}

const BlockState* BellBlock::getStateForPlacement(Level* level, const BlockPos& pos, const Direction* face,
                                                  float, float, float, int, boost::shared_ptr<LivingEntity> placer)
{
    const auto* direction = placer ? placer->getDirection() : Direction::NORTH;
    int attachment = face == Direction::UP ? Floor : Ceiling;
    if (face->getAxis() != Direction::Axis::Y)
    {
        direction = face->getClockWise();
        attachment = support(level, pos, face) ? BetweenWalls : SingleWall;
    }
    return defaultBlockState()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, direction)
                              ->setValue(ATTACHMENT, attachment);
}

bool BellBlock::ring(Level* level, const BlockPos& pos)
{
    if (level->mIsClientSide)
        return false;
    auto bell = boost::dynamic_pointer_cast<BellBlockEntity>(level->getBlockEntity(pos));
    if (!bell)
        return false;
    if (const auto villages = level->getVillages())
        villages->registerMeetingBell(pos);
    level->blockEvent(pos, this, RingEvent, 0);
    level->playSound({}, pos, SoundEvents::BELL_RING, SoundSource::eSoundSource_Blocks, 2.0f, 1.0f, HearingDistance);
    const auto* area = AABB::newTemp(pos.x - HearingDistance, pos.y - HearingDistance, pos.z - HearingDistance,
                                     pos.x + HearingDistance + 1, pos.y + HearingDistance + 1, pos.z + HearingDistance + 1);
    const std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> villagers(level->getEntitiesOfClass(typeid(Villager), area));
    for (const auto& entity : *villagers)
        if (auto villager = boost::dynamic_pointer_cast<Villager>(entity))
            if (villager->distanceToSqr(pos) <= HearingDistance * HearingDistance)
                villager->hearBell();
    return true;
}

bool BellBlock::use(Level* level, const BlockPos& pos, const BlockState*, boost::shared_ptr<Player> player,
                    InteractionHand::EInteractionHand, const Direction*, float, float, float, bool clientSide)
{
    if (!clientSide && ring(level, pos) && level->getVillages())
    {
        const auto village = level->getVillages()->getClosestVillage(pos, 0);
        if (village)
        {
            const auto& center = *village->getCenter();
            const int radius = village->getRadius();
            const auto* area = AABB::newTemp(center.x - radius, center.y - radius, center.z - radius,
                                            center.x + radius + 1, center.y + radius + 1, center.z + radius + 1);
            std::vector<boost::shared_ptr<Entity>> mobs;
            level->getEntities({}, area, &mobs);
            for (const auto& mob : mobs)
                if (dynamic_cast<Enemy*>(mob.get()) && mob->isAlive() && village->isInside(BlockPos(mob)))
                {
                    player->awardStat(GenericStats::achievement(eAward_SoundTheAlarm), GenericStats::param_noArgs());
                    break;
                }
        }
    }
    return true;
}

void BellBlock::neighborChanged(const BlockState* state, Level* level, const BlockPos& pos, Block*, const BlockPos&)
{
    if (level->mIsClientSide)
        return;
    const int attachment = state->getValue(ATTACHMENT);
    const auto* right = facing(state)->getClockWise();
    const auto* left = right->getOpposite();
    const bool first = support(level, pos, left);
    const bool second = support(level, pos, right);
    if (attachment == BetweenWalls && first != second)
    {
        const auto* direction = second ? facing(state) : facing(state)->getOpposite();
        state = state->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, direction)
                     ->setValue(ATTACHMENT, int(SingleWall));
        level->setBlock(pos, state, Block::UPDATE_ALL, false);
    }
    else if ((attachment == Floor && !support(level, pos, Direction::DOWN))
             || (attachment == Ceiling && !support(level, pos, Direction::UP))
             || (attachment == BetweenWalls && !first && !second)
             || (attachment == SingleWall && !second))
    {
        level->destroyBlock(pos, true);
        return;
    }
    auto bell = boost::dynamic_pointer_cast<BellBlockEntity>(level->getBlockEntity(pos));
    if (!bell)
        return;
    const bool powered = level->hasNeighborSignal(pos);
    if (powered != bell->wasPowered)
    {
        bell->wasPowered = powered;
        level->blockEntityChanged(pos, bell);
        if (powered)
            ring(level, pos);
    }
}

const BlockState* BellBlock::getBlockState(int data)
{
    return defaultBlockState()->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, Direction::from2DDataValue(data & 3))
                              ->setValue(ATTACHMENT, (data >> 2) & 3);
}

int BellBlock::convertBlockStateToLegacyData(const BlockState* state)
{
    return facing(state)->get2DDataValue() | (state->getValue(ATTACHMENT) << 2);
}

const BlockState* BellBlock::rotate(const BlockState* state, Rotation* rotation)
{
    return state->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, rotation->rotate(facing(state)));
}

const BlockState* BellBlock::mirror(const BlockState* state, Mirror* mirror)
{
    const auto* direction = facing(state);
    if (state->getValue(ATTACHMENT) == SingleWall)
    {
        const auto* supportFace = direction->getClockWise();
        const auto* mirrored = mirror->getRotation(supportFace)->rotate(supportFace);
        direction = mirrored->getCounterClockWise();
        return state->setValue(HorizontalDirectionalBlock::FACING_PROPERTY, direction);
    }
    return rotate(state, mirror->getRotation(direction));
}

BlockStateDefinition* BellBlock::createBlockStateDefinition()
{
    const Property* properties[] = {HorizontalDirectionalBlock::FACING_PROPERTY, ATTACHMENT};
    return new BlockStateDefinition(this, properties, 2);
}

const Direction* BellBlock::facing(const BlockState* state)
{
    return state->getValue(HorizontalDirectionalBlock::FACING_PROPERTY);
}

CuboidBlockModel::Point BellBlock::orientPoint(const BlockState* state, CuboidBlockModel::Point point)
{
    const auto* direction = facing(state);
    if (direction == Direction::EAST)
        return {16 - point[2], point[1], point[0]};
    if (direction == Direction::SOUTH)
        return {16 - point[0], point[1], 16 - point[2]};
    if (direction == Direction::WEST)
        return {point[2], point[1], 16 - point[0]};
    return point;
}

void BellBlock::registerIcons(IconRegister* icons)
{
    mTextures = {icons->registerIcon(L"planks_big_oak"), icons->registerIcon(L"stone")};
    mIcon = mTextures[1];
}

const std::vector<CuboidBlockModel::Part>& BellBlock::getModel(const BlockState* state) const
{
    return mModels[state->getValue(ATTACHMENT)];
}
