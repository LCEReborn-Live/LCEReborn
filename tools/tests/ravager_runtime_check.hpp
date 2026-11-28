#pragma once
#include <cstdio>
#include <cmath>
#include <memory>
#include "game/client/render/EntityRenderDispatcher.hpp"
#include "game/client/render/RavagerModel.hpp"
#include "game/client/render/ModelPart.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/client/render/Textures.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/EquipmentSlot.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/GameType.hpp"
#include "game/core/Pillager.hpp"
#include "game/core/Ravager.hpp"
#include "game/core/Settings.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/entity/ai/PathNavigation.hpp"
#include "game/entity/ai/NodeEvaluator.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/mob/SharedMonsterAttributes.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/SpawnEggItem.hpp"
#include "game/entity/mob/ItemEntity.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientboundAddMobPacket.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/ServerPlayer.hpp"
#include "game/server/ServerPlayerGameMode.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/BlockPathTypes.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/Villager.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"

#define RAVAGER_CHECK(condition, message) do { if (!(condition)) { std::printf("FAIL: %s (line %d)\n", message, __LINE__); return 1; } } while (0)

class RavagerTestPlayer : public Player
{
public:
    explicit RavagerTestPlayer(Level* level) : Player(level, L"RavagerShield") { fjDerivedCtorCalls(); }
    bool isSpectator() override { return false; }
    const GameType* GetGameType() override { return GameType::SURVIVAL; }
};

inline int RunRavagerRegression()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CGameNetworkManager::sInstance.Initialise();
    const auto storage = boost::shared_ptr<MockedLevelStorage>(new MockedLevelStorage());
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));
    LevelSettings settings(8531, GameType::SURVIVAL, false, false, false, LevelType::FLAT, 54, 3, 128, false, 0, 0, 0);
    auto* data = new LevelData(&settings, L"Ravager regression");
    data->setDifficulty(Difficulty::NORMAL);
    auto* level = new ServerLevel(server, storage, data, 0);
    server->mLevels[0] = level;
    level->postConstruct();
    level->mListeners.clear();
    auto* chunks = static_cast<ServerChunkCache*>(level->getChunkSource());
    for (int x = -1; x <= 2; ++x) for (int z = -1; z <= 2; ++z) chunks->create(x, z);
    const auto ground = level->getTopSolidBlockPos(BlockPos(8, 4, 8));
    int spawnError = 0;
    auto entity = SpawnEggItem::spawnMobAt(level, ResourceLocation(L"ravager"), 8.5, ground.y, 8.5, &spawnError, L"");
    auto mob = boost::dynamic_pointer_cast<Ravager>(entity);
    RAVAGER_CHECK(mob && mob->getHealth() == 100 && mob->getMaxHealth() == 100, "spawn egg and 100 HP");
    RAVAGER_CHECK(std::abs(mob->mBbWidth - 1.95f) < 0.001 && mob->mMaxUpStep == 1, "collision size/step height");
    RAVAGER_CHECK(EntityIO::eTypeToIoid(EntityType::RAVAGER) == 117 && EntityType::RAVAGER != EntityType::PHANTOM, "unique entity ID");
    ClientboundAddMobPacket packet(mob, 0, 0, 0, 0, 0, 0);
    RAVAGER_CHECK(boost::dynamic_pointer_cast<Ravager>(EntityIO::newEntity(packet.getType(), level)), "network spawn factory");
    auto* minecraft = Minecraft::GetInstance();
    auto* renderer = minecraft->mEntityRenderDispatcher->getRenderer(entity);
    RAVAGER_CHECK(renderer && renderer->getModelName() == L"ravager", "renderer registration");
    auto* pack = TexturePackRepository::defaultTexturePack;
    renderer->setupForTexturePack(pack);
    auto* model = dynamic_cast<RavagerModel*>(renderer->getModel());
    RAVAGER_CHECK(model && model->mNeck && model->mHead && model->mMouth, "PS4 model hierarchy");
    std::unique_ptr<BufferedImage> texture(minecraft->mTextures->readImage(TN_UNINDEXED, L"mob/illager/ravager"));
    RAVAGER_CHECK(texture && texture->getWidth() == 128 && texture->getHeight() == 128, "PS4 texture");
    std::puts("PASS: spawn egg, health, collision, packet factory, PS4 model and texture");

    auto victim = boost::shared_ptr<Villager>(new Villager(level));
    victim->setNoAi(true);
    victim->moveTo(11.0, ground.y, 8.5, 0, 0);
    level->addEntity(victim);
    mob->setTarget(victim);
    mob->getRandom()->setSeed(8531);
    bool attacked = false;
    for (int i = 0; i < 200 && !attacked; ++i)
    {
        mob->tick();
        attacked = victim->getHealth() < 20;
    }
    RAVAGER_CHECK(attacked && victim->getHealth() == 8, "AI melee deals 12 damage");
    RAVAGER_CHECK(mob->getAttackTick() > 0, "attack animation triggered");
    boost::shared_ptr<LivingEntity> living = mob;
    model->prepareMobModel(living, 0, 0, 0.5);
    RAVAGER_CHECK(std::isfinite(model->mMouth->mXRot), "attack animation finite");
    mob->setTarget(boost::shared_ptr<LivingEntity>());
    mob->setNoAi(true);
    for (int i = 0; i < 12; ++i) mob->tick();
    std::puts("PASS: native AI approaches and attacks; 12 damage and attack animation");

    // Exercise the common shield hook through actual player damage handling.
    auto defender = boost::shared_ptr<RavagerTestPlayer>(new RavagerTestPlayer(level));
    defender->moveTo(mob->mX, ground.y, mob->mZ + 2, 180, 0);
    defender->mYHeadRot = defender->mYHeadRotO = 180;
    defender->setItemSlot(EquipmentSlot::OFFHAND, not_null_ptr<ItemInstance>(new ItemInstance(Item::byId(442))));
    defender->startUsingItem(InteractionHand::eInteractionHand_OffHand);
    const float defenderHealth = defender->getHealth();
    int stuns = 0, pushes = 0;
    for (int seed = 0; seed < 32; ++seed)
    {
        CompoundTag reset;
        mob->readAdditionContructionTag(&reset);
        mob->getRandom()->setSeed(seed * 131071 + 8531);
        defender->stopUsingItem();
        defender->setItemSlot(EquipmentSlot::OFFHAND, not_null_ptr<ItemInstance>(new ItemInstance(Item::byId(442))));
        defender->startUsingItem(InteractionHand::eInteractionHand_OffHand);
        defender->mInvulnerableTime = 0;
        defender->mXd = defender->mYd = defender->mZd = 0;
        std::unique_ptr<EntityDamageSource> source(DamageSource::CreateMobAttack(mob));
        RAVAGER_CHECK(defender->isDamageSourceBlocked(source.get()), "fixture faces attacker with raised shield");
        defender->hurt(source.get(), 12.0f);
        RAVAGER_CHECK(defender->getHealth() == defenderHealth, "shield damage blocked on each hit");
        if (mob->getStunTick() == 40) ++stuns;
        else if (defender->mYd > 0) ++pushes;
    }
    RAVAGER_CHECK(defender->getHealth() == defenderHealth && stuns > 0 && pushes > 0, "shield blocks damage, stun and knockback branches");
    std::printf("PASS: actual shield damage handling: %d stuns, %d knockbacks, no damage\n", stuns, pushes);
    auto ordinary = boost::shared_ptr<Pillager>(new Pillager(level));
    ordinary->moveTo(defender->mX, defender->mY, defender->mZ - 2, 0, 0);
    ordinary->mOnGround = true;
    ordinary->blockedByShield(defender);
    RAVAGER_CHECK(ordinary->mYd > 0 && std::isfinite(ordinary->mZd), "ordinary attackers retain shield recoil");

    mob->moveTo(8.5, ground.y, 8.5, 0, 0);
    mob->mXd = mob->mYd = mob->mZd = 0;
    victim->moveTo(11, ground.y, 8.5, 0, 0);
    victim->setHealth(20); victim->mInvulnerableTime = 0;
    auto ally = boost::shared_ptr<Pillager>(new Pillager(level));
    ally->setNoAi(true); ally->moveTo(8.5, ground.y, 11, 0, 0); level->addEntity(ally);
    auto other = boost::shared_ptr<Ravager>(new Ravager(level));
    other->setNoAi(true); other->moveTo(5.5, ground.y, 8.5, 0, 0); level->addEntity(other);
    mob->handleEntityEvent(39, 0);
    for (int i = 0; i < 39; ++i) mob->tick();
    RAVAGER_CHECK(mob->getStunTick() == 1 && mob->getRoarTick() == 0, "stun lasts 40 ticks");
    CompoundTag save;
    mob->saveWithoutId(&save);
    auto restored = boost::dynamic_pointer_cast<Ravager>(EntityIO::newEntity(EntityType::RAVAGER, level));
    restored->load(&save);
    RAVAGER_CHECK(restored->getStunTick() == 1 && restored->getHealth() == 100, "save/reload during stun");
    std::unique_ptr<CompoundTag> construction(mob->getAdditionContructionTag());
    other->readAdditionContructionTag(construction.get());
    RAVAGER_CHECK(other->getStunTick() == 1, "late-tracking animation snapshot");
    mob->tick();
    RAVAGER_CHECK(mob->getRoarTick() == 20, "stun enters 20 tick roar");
    for (int i = 0; i < 9; ++i) mob->tick();
    RAVAGER_CHECK(victim->getHealth() == 20, "no premature roar damage");
    mob->tick();
    RAVAGER_CHECK(victim->getHealth() == 14, "roar deals exactly 6 damage at tick 10");
    RAVAGER_CHECK(ally->getHealth() == 24 && ally->mYd > 0, "illager knocked back without damage");
    RAVAGER_CHECK(other->getHealth() == 100 && other->mYd == 0, "other ravagers immune to roar");
    std::puts("PASS: 40-tick stun, delayed roar, area damage, allies, save/reload and late tracking");

    // Navigation must distinguish leaves from solid blocks, and respect mobGriefing.
    mob->setNoAi(false);
    CompoundTag idle;
    mob->readAdditionContructionTag(&idle);
    const BlockPos leaves(16, ground.y + 1, 8);
    level->setBlock(leaves, Blocks::leaves->defaultBlockState(), 3, false);
    gApp.SetGameHostOption(eGameHostOption_MobGriefing, 1);
    auto* evaluator = mob->getNavigation()->getNodeEvaluator();
    RAVAGER_CHECK(evaluator->getBlockPathType(level, leaves.x, leaves.y, leaves.z) == BlockPathTypes::OPEN,
        "leaf navigation");
    gApp.SetGameHostOption(eGameHostOption_MobGriefing, 0);
    RAVAGER_CHECK(evaluator->getBlockPathType(level, leaves.x, leaves.y, leaves.z) != BlockPathTypes::OPEN,
        "mobGriefing false leaf navigation");
    mob->moveTo(15, ground.y, 8.5, 0, 0);
    mob->mXd = 0.5; mob->mYd = mob->mZd = 0;
    mob->tick();
    RAVAGER_CHECK(level->getBlockState(leaves)->getBlock() == Blocks::leaves, "mobGriefing false preserves leaves");
    gApp.SetGameHostOption(eGameHostOption_MobGriefing, 1);
    mob->moveTo(15, ground.y, 8.5, 0, 0);
    mob->mXd = 0.5; mob->mYd = mob->mZd = 0;
    mob->tick();
    RAVAGER_CHECK(level->getBlockState(leaves)->getBlock() != Blocks::leaves, "collision breaks leaves across chunk boundary");
    std::puts("PASS: leaf navigation and physical leaf breaking across chunk boundary respect mobGriefing");
    mob->dropDeathLoot(false, 0);
    const auto drops = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
        level->getEntitiesOfClass(typeid(ItemEntity), mob->getBoundingBox()->grow(2, 2, 2)));
    bool saddle = false;
    for (auto& drop : *drops)
        if (boost::static_pointer_cast<ItemEntity>(drop)->getItem()->getItem() == Items::saddle) saddle = true;
    RAVAGER_CHECK(saddle, "saddle drop even without player kill");

    auto* client = new MultiPlayerLevel(storage, nullptr, new LevelSettings(settings), 0, Difficulty::NORMAL, 63);
    client->mListeners.clear();
    auto visual = boost::dynamic_pointer_cast<Ravager>(EntityIO::newEntity(EntityType::RAVAGER, client));
    auto clientVictim = boost::shared_ptr<Villager>(new Villager(client));
    visual->moveTo(8.5, ground.y, 8.5, 0, 0);
    clientVictim->moveTo(11, ground.y, 8.5, 0, 0);
    client->putEntity(visual->getId(), visual);
    client->putEntity(clientVictim->getId(), clientVictim);
    visual->handleEntityEvent(39, 0);
    for (int i = 0; i < 60; ++i) visual->tick();
    RAVAGER_CHECK(visual->getStunTick() == 0 && visual->getRoarTick() == 0 && clientVictim->getHealth() == 20,
        "client animates stun and roar without applying damage");
    std::puts("PASS: ordinary shield recoil preserved; client animations do not duplicate server damage");
    std::puts("PASS: ravager regression complete");
    return 0;
}
#undef RAVAGER_CHECK
