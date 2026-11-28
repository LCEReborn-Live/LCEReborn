#include "game/world/generation/Raids.hpp"
#include <algorithm>
#include <cmath>
#include "game/core/Difficulty.hpp"
#include "game/core/DifficultyInstance.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/GenericStats.hpp"
#include "game/core/L10N.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/ListTag.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/EntityEvent.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/entity/mob/NaturalMobSpawner.hpp"
#include "game/entity/mob/PatrolSpawner.hpp"
#include "game/entity/mob/Raider.hpp"
#include "game/network/ClientboundSoundPacket.hpp"
#include "game/network/ClientboundChatPacket.hpp"
#include "game/network/ServerGamePacketListenerImpl.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/ServerPlayer.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/Player.hpp"
#include "game/world/generation/RaidHeroGifts.hpp"
#include "game/world/generation/Villages.hpp"

namespace
{
constexpr int RaidTitle = 0x0d20ddea;
constexpr int RaidRemaining = 0x7117fdf8;
constexpr int WaveHornInterval = 20 * 20;
constexpr int PreparationTicks = 300;
}

SoundEvent* Raids::horn = nullptr;
Raids::Raids(const std::wstring& id) : SavedData(id) {}
Raids::~Raids() = default;
Raid* Raids::get(int id) const
{
    auto it = mRaids.find(id);
    return it == mRaids.end() ? nullptr : it->second.get();
}
Raid* Raids::getNearby(const BlockPos& pos, double distanceSqr) const
{
    Raid* result = nullptr;
    for (const auto& entry : mRaids)
    {
        auto* raid = entry.second.get();
        const double distance = raid->getCenter().distSqr(pos);
        if (raid->isActive() && distance < distanceSqr) { result = raid; distanceSqr = distance; }
    }
    return result;
}
bool Raids::isVillage(const BlockPos& pos) const
{
    // Java 1.14's village distance is measured in sections around occupied POIs.
    // Merely placing an unclaimed bed or workstation does not create a village.
    for (const auto& poi : level().getVillages()->getOccupiedPointsOfInterest())
        if (std::abs((poi.x >> 4) - (pos.x >> 4)) <= 1
            && std::abs((poi.y >> 4) - (pos.y >> 4)) <= 1
            && std::abs((poi.z >> 4) - (pos.z >> 4)) <= 1) return true;
    return false;
}
bool Raids::findVillageCenter(const BlockPos& pos, int range, BlockPos& center) const
{
    long long x = 0, y = 0, z = 0;
    int count = 0;
    for (const auto& poi : level().getVillages()->getOccupiedPointsOfInterest())
        if (poi.distSqr(pos) <= range * range) { x += poi.x; y += poi.y; z += poi.z; ++count; }
    if (!count) return false;
    center = BlockPos(static_cast<int>(std::floor(double(x) / count)),
        static_cast<int>(std::floor(double(y) / count)), static_cast<int>(std::floor(double(z) / count)));
    return true;
}
Raid* Raids::createOrExtend(const boost::shared_ptr<Player>& player)
{
    if (mDisabled || player->mLevel != mLevel || !player->isAlive() || player->isSpectator()
        || level().getDifficulty() == Difficulty::PEACEFUL || !player->hasEffect(MobEffects::BAD_OMEN)
        || !isVillage(BlockPos(player))) return nullptr;
    BlockPos center = BlockPos(player);
    findVillageCenter(center, 64, center);
    auto* raid = getNearby(center);
    if (!raid)
    {
        const int id = mNextId++;
        auto created = std::make_unique<Raid>(*this, id, center,
            Raid::groupsForDifficulty(level().getDifficulty()->getId()), level().mRandom->nextLong());
        raid = created.get();
        mRaids.emplace(id, std::move(created));
        player->awardStat(GenericStats::achievement(eAward_WereBeingAttacked), GenericStats::param_noArgs());
    }
    if (!raid->isOver()) raid->absorbBadOmen(player);
    else player->removeEffect(MobEffects::BAD_OMEN);
    setDirty();
    return raid;
}
void Raids::tick()
{
    ++mTick;
    for (const auto& player : level().mPlayers)
        if (player->hasEffect(MobEffects::BAD_OMEN)) createOrExtend(player);
    for (auto it = mRaids.begin(); it != mRaids.end();)
    {
        if (mDisabled) it->second->stop();
        else it->second->tick();
        if (it->second->getStatus() == Raid::Stopped) it = mRaids.erase(it);
        else ++it;
    }
    // Cooldowns, participants and PRNG state must survive an autosave mid-wave.
    setDirty();
}
void Raids::load(CompoundTag* tag)
{
    mNextId = std::max(1, tag->getInt(L"NextAvailableID"));
    mTick = tag->getInt(L"Tick");
    mDisabled = tag->getBoolean(L"DisableRaids");
    for (auto& entry : mRaids) entry.second->bossbar().removeAllPlayers();
    mRaids.clear();
    auto* raids = tag->getList(L"Raids", 10);
    for (int i = 0; i < raids->size(); ++i)
    {
        auto* data = raids->getCompound(i);
        const int id = data->getInt(L"Id");
        auto raid = std::make_unique<Raid>(*this, id, BlockPos(), 3, 0);
        raid->load(data);
        mRaids.emplace(id, std::move(raid));
        mNextId = std::max(mNextId, id + 1);
    }
}
CompoundTag* Raids::save(CompoundTag* tag)
{
    tag->putInt(L"NextAvailableID", mNextId); tag->putInt(L"Tick", mTick);
    tag->putBoolean(L"DisableRaids", mDisabled);
    auto* raids = new ListTag();
    for (auto& entry : mRaids) { auto* data = new CompoundTag(); entry.second->save(data); raids->add(data); }
    tag->put(L"Raids", raids);
    return tag;
}

Raid::Raid(Raids& owner, int id, const BlockPos& center, int groups, long long seed)
    : mOwner(owner), mId(id), mCenter(center), mNumGroups(groups), mRandom(seed),
      mBossbar(*L10N::GetString(RaidTitle) ? RaidTitle : IDS_LCE_RAID, L"", BossEvent::PURPLE, BossEvent::PROGRESS)
{
    mBossbar.setPercent(0);
}
ServerLevel& Raid::level() const { return mOwner.level(); }
int Raid::groupsForDifficulty(int difficulty) { return difficulty == 1 ? 3 : difficulty == 2 ? 5 : difficulty == 3 ? 7 : 0; }
int Raid::baseCount(int type, int wave)
{
    static constexpr int counts[5][8] = {
        {0,0,2,0,1,4,2,5}, {0,0,0,0,0,1,1,2}, {0,4,3,3,4,4,4,2},
        {0,0,0,0,3,0,0,1}, {0,0,0,1,0,1,0,2}
    };
    return type >= 0 && type < 5 && wave >= 0 && wave <= 7 ? counts[type][wave] : 0;
}
int Raid::bonusCount(int type, int wave, int difficulty, bool bonusWave, Random& random)
{
    if (type == 3) return difficulty != 1 && wave > 2 && wave != 4 ? random.nextInt(2) : 0;
    if (type == 4) return difficulty != 1 && bonusWave ? random.nextInt(2) : 0;
    if (type == 1) return 0;
    int maximum = difficulty == 1 ? random.nextInt(2) : difficulty == 2 ? 1 : 2;
    return maximum > 0 ? random.nextInt(maximum + 1) : 0;
}
float Raid::getEnchantOdds() const
{
    static constexpr float odds[] = {0, 0, .1f, .25f, .5f, .75f};
    return odds[std::max(0, std::min(5, mBadOmenLevel))];
}
bool Raid::hasMoreWaves() const { return mGroupsSpawned < mNumGroups + (mBadOmenLevel > 1 ? 1 : 0); }
void Raid::absorbBadOmen(const boost::shared_ptr<Player>& player)
{
    if (auto* effect = player->getEffect(MobEffects::BAD_OMEN))
        mBadOmenLevel = std::min(5, mBadOmenLevel + effect->getAmplifier() + 1);
    player->removeEffect(MobEffects::BAD_OMEN);
}
void Raid::join(const boost::shared_ptr<Raider>& mob, int wave, bool countArrival)
{
    if (mStatus == Stopped || !mob->isAlive()) return;
    auto inserted = mMembers.emplace(mob->getUUID(), Member());
    auto& member = inserted.first->second;
    member.entity = mob; member.pos = BlockPos(mob);
    member.wave = wave; member.captain = mob->isCaptain(); member.missingTicks = 0;
    if (inserted.second && countArrival) ++mWaveTotal;
    if (mob->getRaidId() != mId || mob->getWave() != wave) mob->setRaid(mId, wave);
}
void Raid::remove(Raider& mob)
{
    mMembers.erase(mob.getUUID());
    mob.setRaid(0, 0);
    updateBossbar();
}
Raider* Raid::getLeader(int wave)
{
    for (auto& entry : mMembers)
        if (entry.second.wave == wave)
            if (auto mob = entry.second.entity.lock())
                if (mob->isAlive() && !mob->mRemoved && mob->isCaptain()) return mob.get();
    return nullptr;
}
void Raid::addHero(const std::wstring& uuid) { mHeroes.insert(uuid); }
void Raid::stop()
{
    mStatus = Stopped; mActive = false;
    mBossbar.removeAllPlayers();
    for (auto& entry : mMembers) if (auto mob = entry.second.entity.lock()) mob->setRaid(0, 0);
    mMembers.clear();
}
void Raid::updateMembers()
{
    for (auto it = mMembers.begin(); it != mMembers.end();)
    {
        auto& member = it->second;
        auto mob = member.entity.lock();
        if (!mob || mob->mRemoved)
        {
            // Entity NBT and raid SavedData are loaded independently. Keep the UUID
            // until its last chunk is loaded, rather than treating unload as a kill.
            mob = boost::dynamic_pointer_cast<Raider>(level().getEntityByUUID(it->first));
            if (mob && !mob->mRemoved) member.entity = mob;
            else
            {
                if (level().hasChunkAt(member.pos, false)) member.missingTicks += 20;
                else member.missingTicks = 0;
                if (member.missingTicks < 600) { ++it; continue; }
                it = mMembers.erase(it); continue;
            }
        }
        member.pos = BlockPos(mob); member.captain = mob->isCaptain();
        const bool escaped = mob->mLevel != &level() || mCenter.distSqr(member.pos) >= 112 * 112
            || (mob->mTickCount > 600 && !mOwner.isVillage(member.pos) && mob->getNoActionTime() > 2400
                && ++mob->mTicksOutsideRaid >= 30);
        if (!mob->isAlive() || escaped)
        {
            mob->setRaid(0, 0); it = mMembers.erase(it);
        }
        else ++it;
    }
}
void Raid::updatePlayers()
{
    std::set<boost::shared_ptr<ServerPlayer>> eligible;
    for (const auto& player : level().mPlayers)
        if (player->isAlive() && mOwner.getNearby(BlockPos(player)) == this)
            if (auto serverPlayer = boost::dynamic_pointer_cast<ServerPlayer>(player)) eligible.insert(serverPlayer);
    const auto previous = mBossbar.getPartyPlayers();
    for (const auto& player : previous) if (!eligible.count(player.first)) mBossbar.removePlayer(player.first);
    for (const auto& player : eligible) mBossbar.addPlayer(player, 0);
}
void Raid::updateBossbar()
{
    if (mStatus == Victory)
    {
        mBossbar.setVisible(false);
        mBossbar.removeAllPlayers();
        return;
    }
    if (mStatus == Loss)
    {
        mBossbar.setName(L10N::GetString(IDS_LCE_RAID_DEFEAT));
        return;
    }
    const int remaining = getRemaining();
    if (remaining)
        mBossbar.setPercent(static_cast<float>(remaining) / std::max(remaining, mWaveTotal));
    else
        mBossbar.setPercent(mCooldown > 0 ? (PreparationTicks - mCooldown) / float(PreparationTicks) : 0.0f);
    std::wstring name;
    if (remaining <= 2 && (remaining > 0 || (mStarted && !hasMoreWaves())))
    {
        name = L10N::GetString(RaidRemaining);
        const auto placeholder = name.find(L"%d");
        if (placeholder != std::wstring::npos)
            name.replace(placeholder, 2, std::to_wstring(remaining));
        else
            name = std::wstring(L10N::GetString(mBossbar.getNameId())) + L" - "
                + L10N::GetString(IDS_LCE_RAID_REMAINING) + std::to_wstring(remaining);
    }
    mBossbar.setName(name);
}
bool Raid::findSpawnPos(int stage, int attempts, BlockPos& result)
{
    const int radius = stage == 0 ? 64 : (2 - stage) * 32;
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        const float angle = mRandom.nextFloat() * 6.28318530718f;
        const int x = mCenter.x + static_cast<int>(std::floor(std::cos(angle) * radius)) + mRandom.nextInt(5);
        const int z = mCenter.z + static_cast<int>(std::floor(std::sin(angle) * radius)) + mRandom.nextInt(5);
        BlockPos pos;
        if (!level().hasChunksAt(x-10, 0, z-10, x+10, level().getHeight()-1, z+10, false)
            || !PatrolSpawner::getSurface(level(), BlockPos(x, 0, z), pos)
            || (stage < 2 && mOwner.isVillage(pos))) continue;
        if (NaturalMobSpawner::isSpawnPositionOk(Mob::SPAWN_PLACEMENT_TYPE_ON_GROUND, &level(), pos, 1, 1)
            || (level().getBlockState(pos.below())->getBlock() == Blocks::snow_layer && level().isEmptyBlock(pos)))
        { result = pos; return true; }
    }
    return false;
}
void Raid::playHorn(const BlockPos& pos)
{
    if (!Raids::horn) return;
    for (const auto& entry : level().mPlayers)
    {
        auto player = boost::dynamic_pointer_cast<ServerPlayer>(entry);
        if (!player || !player->mConnection || mOwner.getNearby(BlockPos(player)) != this) continue;
        const double dx = pos.x + 0.5 - player->mX;
        const double dy = pos.y - player->mY;
        const double dz = pos.z + 0.5 - player->mZ;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double scale = distance > 13 ? 13 / distance : 1;
        player->mConnection->send(boost::shared_ptr<Packet>(new ClientboundSoundPacket(Raids::horn,
            SoundSource::eSoundSource_Neutral, player->mX + dx * scale, player->mY + dy * scale,
            player->mZ + dz * scale, 64, 1)));
    }
}

void Raid::tickWaveHorn()
{
    if (mTicksActive < mNextHornTick)
        return;
    const auto raidLeader = getLeader(mGroupsSpawned);
    if (raidLeader)
        playHorn(BlockPos(raidLeader));
    else
    {
        for (const auto& entry : mMembers)
        {
            if (auto mob = entry.second.entity.lock())
            {
                if (!mob->isAlive() || mob->mRemoved)
                    continue;
                playHorn(BlockPos(mob));
                break;
            }
        }
    }
    mNextHornTick = mTicksActive + WaveHornInterval;
}

void Raid::expire()
{
    for (const auto& entity : level().mPlayers)
    {
        auto player = boost::dynamic_pointer_cast<ServerPlayer>(entity);
        if (player && player->mConnection && mOwner.getNearby(BlockPos(player)) == this)
            player->sendMessage(ClientboundChatPacket::eChatPacketMessage_RaidExpired, nullptr, 0, nullptr, 0);
    }
    stop();
}

bool Raid::spawnWave(const BlockPos& pos)
{
    if (!mMembers.empty() || !hasMoreWaves()) return false;
    static const unsigned long long types[] = {EntityType::VINDICATION_ILLAGER, EntityType::EVOCATION_ILLAGER,
        EntityType::PILLAGER, EntityType::WITCH, EntityType::RAVAGER};
    const int wave = mGroupsSpawned + 1;
    const bool bonus = wave > mNumGroups;
    const int difficulty = level().getDifficulty()->getId();
    bool captain = false;
    mWaveTotal = 0;
    auto spawn = [&](unsigned long long type, bool mayLead) {
        auto mob = boost::dynamic_pointer_cast<Raider>(EntityIO::newEntity(type, &level()));
        if (!mob) return mob;
        mob->setRaid(mId, wave);
        mob->moveTo(pos.x + .5, pos.y + 1, pos.z + .5, 0, 0);
        delete mob->finalizeMobSpawn(level().getCurrentDifficultyAt(pos), nullptr, L"");
        if (mayLead && !captain && mob->canLeadRaid()) { mob->setCaptain(true); captain = true; }
        mob->applyRaidBuffs(wave);
        mob->mOnGround = true;
        if (!level().addEntity(mob)) return boost::shared_ptr<Raider>();
        join(mob, wave);
        return mob;
    };
    for (int type = 0; type < 5; ++type)
    {
        const int count = baseCount(type, bonus ? mNumGroups : wave)
            + bonusCount(type, wave, difficulty, bonus, mRandom);
        for (int i = 0; i < count; ++i)
        {
            auto mob = spawn(types[type], true);
            if (!mob || type != 4 || (wave != 5 && wave < 7)) continue;
            auto rider = spawn(wave == 5 ? EntityType::PILLAGER : i == 0 ? EntityType::EVOCATION_ILLAGER
                : EntityType::VINDICATION_ILLAGER, false);
            if (rider) rider->startRiding(mob, true);
        }
    }
    mStarted = true; ++mGroupsSpawned; mHasSpawnPos = false;
    mNextHornTick = mTicksActive + WaveHornInterval;
    updateBossbar();
    if (!mMembers.empty()) playHorn(pos);
    return !mMembers.empty();
}
void Raid::tick()
{
    if (mStatus == Stopped) return;
    if (level().getDifficulty() == Difficulty::PEACEFUL) { stop(); return; }
    mActive = level().hasChunkAt(mCenter, false);
    mBossbar.setVisible(mActive && mStatus != Victory);
    if (!mActive) return;
    if (mStatus == Victory || mStatus == Loss)
    {
        if (++mCelebrationTicks >= 600) { stop(); return; }
        if (mStatus == Loss && mCelebrationTicks % 20 == 0) { updatePlayers(); updateBossbar(); }
        return;
    }
    if (++mTicksActive >= 48000) { expire(); return; }
    if (!mOwner.isVillage(mCenter))
    {
        BlockPos closest;
        double distance = 1e30;
        for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy) for (int dz = -2; dz <= 2; ++dz)
        {
            const BlockPos candidate(((mCenter.x >> 4) + dx) * 16 + 8,
                ((mCenter.y >> 4) + dy) * 16 + 8, ((mCenter.z >> 4) + dz) * 16 + 8);
            if (mOwner.isVillage(candidate) && candidate.distSqr(mCenter) < distance)
            { closest = candidate; distance = candidate.distSqr(mCenter); }
        }
        if (distance < 1e30) mCenter = closest;
        else { if (mStarted) { mStatus = Loss; updateBossbar(); } else stop(); return; }
    }
    if (mTicksActive % 20 == 0) { updateMembers(); updatePlayers(); updateBossbar(); }
    if (!mMembers.empty()) { tickWaveHorn(); return; }
    if (!hasMoreWaves())
    {
        if (++mPostRaidTicks >= 40)
        {
            mStatus = Victory;
            updateBossbar();
            for (const auto& uuid : mHeroes)
                if (auto player = level().getPlayerByUUID(uuid))
                    if (player->isAlive() && !player->isSpectator())
                    {
                        player->addEffect(new MobEffectInstance(MobEffects::HERO_OF_THE_VILLAGE, 48000,
                            std::max(0, mBadOmenLevel - 1), false, false), {});
                        level().broadcastEntityEvent(player, EntityEvent::RAID_HERO_REWARD, 0);
                    }
            announceRaidVictory(level(), mCenter);
        }
        return;
    }
    if (mCooldown == 0 && mStarted) { mCooldown = PreparationTicks; updateBossbar(); return; }
    if (mCooldown > 0)
    {
        if ((!mHasSpawnPos || !level().hasChunkAt(mSpawnPos, false)) && mCooldown % 5 == 0)
            mHasSpawnPos = findSpawnPos(mCooldown < 100 ? 1 : 0, 3, mSpawnPos);
        --mCooldown;
        mBossbar.setPercent((PreparationTicks - mCooldown) / float(PreparationTicks));
        if (mCooldown > 0) return;
    }
    if (mHasSpawnPos && !level().hasChunksAt(mSpawnPos, 10, false)) mHasSpawnPos = false;
    for (int stage = 0; !mHasSpawnPos && stage <= 3; ++stage) mHasSpawnPos = findSpawnPos(stage, 20, mSpawnPos);
    if (!mHasSpawnPos || !spawnWave(mSpawnPos)) stop();
}
void Raid::save(CompoundTag* tag)
{
    tag->putInt(L"Id", mId); tag->putBoolean(L"Started", mStarted); tag->putBoolean(L"Active", mActive);
    tag->putLong(L"TicksActive", mTicksActive); tag->putInt(L"BadOmenLevel", mBadOmenLevel);
    tag->putInt(L"GroupsSpawned", mGroupsSpawned); tag->putInt(L"PreRaidTicks", mCooldown);
    tag->putInt(L"PostRaidTicks", mPostRaidTicks); tag->putInt(L"CelebrationTicks", mCelebrationTicks);
    tag->putInt(L"WaveTotal", mWaveTotal); tag->putInt(L"NumGroups", mNumGroups);
    tag->putLong(L"NextHornTick", mNextHornTick);
    tag->putInt(L"Status", mStatus); tag->putInt(L"CX", mCenter.x); tag->putInt(L"CY", mCenter.y); tag->putInt(L"CZ", mCenter.z);
    tag->putLong(L"RandomState", mRandom.getUniformSeed());
    tag->putBoolean(L"HasSpawnPos", mHasSpawnPos);
    tag->putInt(L"SpawnX", mSpawnPos.x); tag->putInt(L"SpawnY", mSpawnPos.y); tag->putInt(L"SpawnZ", mSpawnPos.z);
    auto* members = new ListTag();
    for (auto& entry : mMembers)
    {
        auto& member = entry.second;
        if (auto mob = member.entity.lock()) member.pos = BlockPos(mob);
        auto* data = new CompoundTag(); data->putString(L"UUID", entry.first);
        data->putInt(L"X", member.pos.x); data->putInt(L"Y", member.pos.y); data->putInt(L"Z", member.pos.z);
        data->putInt(L"Wave", member.wave);
        data->putBoolean(L"Captain", member.captain); members->add(data);
    }
    tag->put(L"Members", members);
    auto* heroes = new ListTag();
    for (const auto& uuid : mHeroes) { auto* data = new CompoundTag(); data->putString(L"UUID", uuid); heroes->add(data); }
    tag->put(L"HeroesOfTheVillage", heroes);
}
void Raid::load(CompoundTag* tag)
{
    mStarted = tag->getBoolean(L"Started"); mActive = tag->getBoolean(L"Active");
    mTicksActive = tag->getLong(L"TicksActive"); mBadOmenLevel = std::max(0, std::min(5, tag->getInt(L"BadOmenLevel")));
    mGroupsSpawned = tag->getInt(L"GroupsSpawned"); mCooldown = tag->getInt(L"PreRaidTicks");
    mPostRaidTicks = tag->getInt(L"PostRaidTicks"); mCelebrationTicks = tag->getInt(L"CelebrationTicks");
    mNumGroups = tag->getInt(L"NumGroups");
    mNextHornTick = tag->contains(L"NextHornTick") ? tag->getLong(L"NextHornTick") : mTicksActive + WaveHornInterval;
    mStatus = static_cast<Status>(std::max(0, std::min(3, tag->getInt(L"Status"))));
    mCenter = BlockPos(tag->getInt(L"CX"), tag->getInt(L"CY"), tag->getInt(L"CZ"));
    mRandom.setSeed(tag->getLong(L"RandomState"));
    mHasSpawnPos = tag->getBoolean(L"HasSpawnPos");
    mSpawnPos = BlockPos(tag->getInt(L"SpawnX"), tag->getInt(L"SpawnY"), tag->getInt(L"SpawnZ"));
    mMembers.clear(); mHeroes.clear();
    auto* members = tag->getList(L"Members", 10);
    for (int i = 0; i < members->size(); ++i)
    {
        auto* data = members->getCompound(i); Member member;
        member.pos = BlockPos(data->getInt(L"X"), data->getInt(L"Y"), data->getInt(L"Z"));
        member.wave = data->getInt(L"Wave"); member.captain = data->getBoolean(L"Captain");
        mMembers[data->getString(L"UUID")] = member;
    }
    mWaveTotal = std::max(getRemaining(), tag->getInt(L"WaveTotal"));
    auto* heroes = tag->getList(L"HeroesOfTheVillage", 10);
    for (int i = 0; i < heroes->size(); ++i) mHeroes.insert(heroes->getCompound(i)->getString(L"UUID"));
}
