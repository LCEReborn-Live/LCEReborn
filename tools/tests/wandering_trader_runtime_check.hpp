#pragma once

#include <cstdio>
#include <algorithm>
#include <array>

#include "game/client/render/EntityRenderDispatcher.hpp"
#include "game/client/render/EntityRenderer.hpp"
#include "game/client/render/Textures.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/GameType.hpp"
#include "game/core/Settings.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/MerchantRecipe.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/item/SpawnEggItem.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientboundAddMobPacket.hpp"
#include "game/network/PendingEntityLinks.hpp"
#include "game/network/ServerLevelListener.hpp"
#include "game/world/generation/MultiPlayerLevel.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/ServerPlayer.hpp"
#include "game/server/ServerPlayerGameMode.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/block/BedBlock.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/DirectionProperty.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/Villages.hpp"
#include "game/world/generation/WanderingTrader.hpp"

namespace
{
class TraderParticleProbe : public ServerLevelListener
{
public:
    TraderParticleProbe(MinecraftServer* server, ServerLevel* level) : ServerLevelListener(server, level) {}
    void addParticle(Level*, ePARTICLE_TYPE type, bool, double, double, double,
                     double, double, double, arrayWithLength<int>) override
    {
        if (type == PARTICLE_MOB_SPELL->getId())
            ++spellParticles;
    }
    int spellParticles = 0;
};

class TraderTestPlayer : public ServerPlayer
{
public:
    TraderTestPlayer(MinecraftServer* server, ServerLevel* level)
        : ServerPlayer(server, level, L"TraderTester", new ServerPlayerGameMode(level, nullptr))
    {
    }

    int openTrading(boost::shared_ptr<Merchant> merchant, const std::wstring&, int entityId) override
    {
        openedMerchant = merchant;
        openedEntityId = entityId;
        return 1;
    }

    boost::shared_ptr<Merchant> openedMerchant;
    int openedEntityId = -1;
};
}

inline int RunWanderingTraderRegression()
{
    CGameNetworkManager::sInstance.Initialise();
    const auto storage = boost::shared_ptr<MockedLevelStorage>(new MockedLevelStorage());
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    LevelSettings settings(98837, GameType::ADVENTURE, false, false, false, LevelType::FLAT,
                           54, 3, 128, false, 0, 0, 0);
    auto* data = new LevelData(&settings, L"Wandering trader test");
    data->setDifficulty(Difficulty::NORMAL);
    data->setSpawn(BlockPos(15, 4, 8));
    auto* level = new ServerLevel(server, storage, data, 0);
    server->mLevels[0] = level;
    level->postConstruct();
    level->mListeners.clear();
    auto* chunks = static_cast<ServerChunkCache*>(level->getChunkSource());
    chunks->create(0, 0);
    chunks->create(1, 0);
    int spawnError = 0;
    auto creativeTrader = SpawnEggItem::getMob(level, ResourceLocation(L"wandering_trader"), 4.5, 4.0, 4.5,
                                               &spawnError, L"");
    if (!boost::dynamic_pointer_cast<WanderingTrader>(creativeTrader)
        || !boost::dynamic_pointer_cast<Creature>(creativeTrader))
        return 120;

    const auto player = boost::shared_ptr<TraderTestPlayer>(new TraderTestPlayer(server, level));
    const auto origin = level->getTopSolidBlockPos(BlockPos(15, 4, 8));
    player->moveTo(origin, 0.0f, 0.0f);
    level->mPlayers.push_back(player);
    const auto villages = level->getVillages();
    const auto foot = origin.east();
    const auto head = foot.south();
    const auto bell = head.east();
    const auto* bed = Blocks::bed->defaultBlockState()->setValue<const Direction*>(BedBlock::FACING, Direction::SOUTH);
    level->setBlock(foot, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedFoot), 0, false);
    level->setBlock(head, bed->setValue<const BedBlock::Part*>(BedBlock::PART, &gBedHead), 0, false);
    level->setBlock(bell, Blocks::bell->defaultBlockState(), 0, false);
    villages->claimHome(L"Resident", head);
    villages->registerMeetingBell(bell);
    level->mRandom->setSeed(12);
    level->setGameTime(1);
    gApp.SetGameHostOption(eGameHostOption_DoMobSpawning, 1);
    villages->tick();
    CompoundTag pending;
    villages->save(&pending);
    const auto attempt = pending.getLong(L"NextTraderAttempt");
    if (attempt <= level->getGameTime())
        return 100;

    const auto* area = AABB::newTemp(0, 0, 0, 32, 128, 16);
    level->setGameTime(attempt);
    gApp.SetGameHostOption(eGameHostOption_DoMobSpawning, 0);
    villages->tick();
    auto disabled = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
        level->getEntitiesOfClass(typeid(WanderingTrader), area));
    if (!disabled->empty())
        return 101;

    gApp.SetGameHostOption(eGameHostOption_DoMobSpawning, 1);
    villages->tick();
    auto traders = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
        level->getEntitiesOfClass(typeid(WanderingTrader), area));
    auto llamas = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
        level->getEntitiesOfClass(typeid(TraderLlama), area));
    if (traders->size() != 1 || llamas->size() != 2)
    {
        std::printf("Trader visit: %zu traders, %zu llamas\n", traders->size(), llamas->size());
        return 102;
    }
    const auto trader = boost::static_pointer_cast<WanderingTrader>(traders->front());
    if (trader->getRestrictCenter() != bell)
        return 116;
    for (const auto& entity : *llamas)
    {
        if (entity->getLeashHolder() != trader)
            return 103;
        auto llama = boost::static_pointer_cast<TraderLlama>(entity);
        ClientboundAddMobPacket packet(llama, 0, 0, 0, 0, 0, 0);
        if (!boost::dynamic_pointer_cast<TraderLlama>(EntityIO::newEntity(packet.getType(), level)))
            return 104;
    }
    ClientboundAddMobPacket packet(trader, 0, 0, 0, 0, 0, 0);
    if (!boost::dynamic_pointer_cast<WanderingTrader>(EntityIO::newEntity(packet.getType(), level)))
        return 105;

    auto* minecraft = Minecraft::GetInstance();
    auto* renderer = minecraft->mEntityRenderDispatcher->getRenderer(trader);
    auto renderEntity = boost::static_pointer_cast<Entity>(trader);
    const auto* texture = renderer ? renderer->getTextureLocation(renderEntity) : nullptr;
    if (!texture || texture->getPath() != L"mob/wandering_trader")
        return 117;
    std::unique_ptr<BufferedImage> robes(minecraft->mTextures->readImage(TN_UNINDEXED, texture->getPath()));
    std::unique_ptr<BufferedImage> carpet(minecraft->mTextures->readImage(TN_UNINDEXED, L"mob/llama/decor/trader_llama_decor"));
    if (!robes || robes->getWidth() != 64 || robes->getHeight() != 64 ||
        !carpet || carpet->getWidth() != 128 || carpet->getHeight() != 64)
        return 118;
    std::puts("PASS: renderer resolves both TU80 trader textures");

    CompoundTag visit;
    villages->save(&visit);
    const auto expires = visit.getLong(L"TraderVisitUntil");
    if (expires < attempt + 48000 || expires > attempt + 72000)
        return 106;
    Villages loadedVillages(level);
    loadedVillages.load(&visit);
    loadedVillages.tick();
    auto later = std::unique_ptr<std::vector<boost::shared_ptr<Entity>>>(
        level->getEntitiesOfClass(typeid(WanderingTrader), area));
    if (later->size() != 1)
        return 107;
    std::puts("PASS: natural visit obeys spawn rule, restores scheduling and creates two leashed, network-registered llamas");

    player->moveTo(BlockPos(trader.get()).east(), 0.0f, 0.0f);
    if (!trader->mobInteract(player, InteractionHand::eInteractionHand_MainHand) || player->openedMerchant.get() != trader.get()
        || player->openedEntityId != trader->getId())
        return 108;
    auto* offers = trader->getOffers(player);
    if (offers->size() != 6)
        return 109;
    for (auto* recipe : *offers)
    {
        if (recipe->getBuyAItem()->getItem() != Items::emerald || recipe->getSellItem()->isEmpty()
            || recipe->hasSecondaryBuyItem())
            return 110;
    }
    trader->notifyTrade(offers->at(0));
    CompoundTag traderTag;
    trader->saveWithoutId(&traderTag);
    auto restored = boost::static_pointer_cast<WanderingTrader>(EntityIO::newEntity(EntityType::WANDERING_TRADER, level));
    restored->load(&traderTag);
    if (restored->getOffers({})->size() != 6 || restored->getOffers({})->at(0)->getUses() != 1
        || !ItemInstance::isSame(restored->getOffers({})->at(0)->getSellItem(), offers->at(0)->getSellItem()))
        return 111;
    trader->setTradingPlayer({});
    std::puts("PASS: interaction opens merchant and offer stock survives entity NBT reload");

    level->setSkyDarken(11);
    trader->serverAiStep();
    if (trader->hasEffect(MobEffects::INVISIBILITY) || !trader->isUsingItem()
        || trader->getMainHandItem()->getItem() != Items::potion)
        return 112;
    for (int i = 0; i < 31; ++i)
        trader->updatingUsingItem();
    if (trader->hasEffect(MobEffects::INVISIBILITY) || !trader->isUsingItem())
        return 121;
    trader->updatingUsingItem();
    if (!trader->hasEffect(MobEffects::INVISIBILITY) || trader->isUsingItem()
        || !trader->getMainHandItem()->isEmpty())
        return 122;
    level->setSkyDarken(0);
    trader->serverAiStep();
    if (!trader->hasEffect(MobEffects::INVISIBILITY) || !trader->isUsingItem()
        || trader->getMainHandItem()->getItem() != Items::milkBucket)
        return 113;
    for (int i = 0; i < 31; ++i)
        trader->updatingUsingItem();
    if (!trader->hasEffect(MobEffects::INVISIBILITY))
        return 123;
    trader->updatingUsingItem();
    if (trader->hasEffect(MobEffects::INVISIBILITY) || trader->isUsingItem()
        || !trader->getMainHandItem()->isEmpty())
        return 124;
    level->setSkyDarken(11);
    trader->serverAiStep();
    trader->stopUsingItem();
    if (trader->hasEffect(MobEffects::INVISIBILITY))
        return 125;
    trader->serverAiStep();
    for (int i = 0; i < 32; ++i)
        trader->updatingUsingItem();
    trader->removeEffect(MobEffects::INVISIBILITY);
    trader->serverAiStep();
    if (!trader->isUsingItem())
        return 126;
    trader->stopUsingItem();
    trader->setItemInHand(InteractionHand::eInteractionHand_MainHand, ItemInstance::EMPTY);
    level->setSkyDarken(0);
    std::puts("PASS: potion and milk each take 32 ticks, interrupted drinking has no effect, night potion renews");

    auto* client = new MultiPlayerLevel(storage, nullptr, new LevelSettings(settings), 0, Difficulty::NORMAL, 63);
    client->mListeners.clear();
    std::array<int, 3> order = {0, 1, 2};
    do
    {
        PendingEntityLinks links;
        auto holder = EntityIO::newEntity(EntityType::WANDERING_TRADER, client);
        auto llama = EntityIO::newEntity(EntityType::TRADER_LLAMA, client);
        auto link = boost::shared_ptr<ClientboundSetEntityLinkPacket>(new ClientboundSetEntityLinkPacket(llama, holder));
        for (int step : order)
        {
            if (step == 2)
                links.receive(*client, link);
            else
            {
                const auto entity = step == 0 ? llama : holder;
                client->putEntity(entity->getId(), entity);
                links.entityAdded(*client, entity->getId());
            }
        }
        if (llama->getLeashHolder() != holder)
            return 127;
        holder->setInvisible(true);
        if (llama->getLeashHolder() != holder || !llama->isLeashed())
            return 128;
    } while (std::next_permutation(order.begin(), order.end()));

    PendingEntityLinks links;
    auto lateHolder = EntityIO::newEntity(EntityType::WANDERING_TRADER, client);
    auto linked = EntityIO::newEntity(EntityType::TRADER_LLAMA, client);
    client->putEntity(linked->getId(), linked);
    auto attach = boost::shared_ptr<ClientboundSetEntityLinkPacket>(new ClientboundSetEntityLinkPacket(linked, lateHolder));
    links.receive(*client, attach);
    links.receive(*client, boost::shared_ptr<ClientboundSetEntityLinkPacket>(new ClientboundSetEntityLinkPacket(linked, {})));
    client->putEntity(lateHolder->getId(), lateHolder);
    links.entityAdded(*client, lateHolder->getId());
    if (linked->isLeashed())
        return 129;

    auto clientTrader = boost::static_pointer_cast<WanderingTrader>(lateHolder);
    clientTrader->getEntityData()->set(LivingEntity::DATA_LIVING_ENTITY_FLAGS, static_cast<unsigned char>(1), true);
    clientTrader->updatingUsingItem();
    clientTrader->setItemInHand(InteractionHand::eInteractionHand_MainHand,
                               not_null_ptr<ItemInstance>(new ItemInstance(Items::potion)));
    clientTrader->updatingUsingItem();
    if (clientTrader->getUseItem()->isEmpty() || clientTrader->getUseItemRemainingTicks() != 31)
        return 130;
    std::puts("PASS: client leashes resolve all six spawn/link orders, detach supersedes pending attach, delayed equipment animates");

    auto defensiveTrader = boost::static_pointer_cast<WanderingTrader>(EntityIO::newEntity(EntityType::WANDERING_TRADER, level));
    defensiveTrader->moveTo(trader->mX, trader->mY, trader->mZ, 0.0f, 0.0f);
    auto threat = boost::static_pointer_cast<LivingEntity>(EntityIO::newEntity(EntityType::ZOMBIE, level));
    defensiveTrader->mTickCount = 100;
    defensiveTrader->setLastHurtByMob(threat);
    defensiveTrader->serverAiStep();
    for (int i = 0; i < 272; ++i)
    {
        ++defensiveTrader->mTickCount;
        defensiveTrader->tick();
    }
    auto* defensiveEffect = defensiveTrader->getEffect(MobEffects::INVISIBILITY);
    if (!defensiveEffect || defensiveEffect->getDuration() < 3300 || !defensiveEffect->isVisible()
        || defensiveTrader->isUsingItem() || !defensiveTrader->isInvisible())
        return 131;
    if (defensiveTrader->getEntityData()->get(LivingEntity::DATA_EFFECT_COLOR_ID) == 0)
        return 132;

    auto* effectMetadata = defensiveTrader->getEntityData()->getAll();
    clientTrader->getEntityData()->assignValues(effectMetadata);
    for (auto* entry : *effectMetadata)
        delete entry;
    delete effectMetadata;
    TraderParticleProbe probe(server, level);
    client->mListeners.push_back(&probe);
    clientTrader->getRandom()->setSeed(12345);
    for (int i = 0; i < 240; ++i)
        clientTrader->tickEffects();
    client->mListeners.clear();
    if (!clientTrader->isInvisible() || probe.spellParticles == 0)
        return 133;

    MobEffectInstance oldHiddenEffect(MobEffects::INVISIBILITY, defensiveEffect->getDuration(), 0, false, false);
    defensiveEffect->update(&oldHiddenEffect);
    CompoundTag defensiveSave;
    defensiveTrader->saveWithoutId(&defensiveSave);
    auto reloadedDefensive = boost::static_pointer_cast<WanderingTrader>(EntityIO::newEntity(EntityType::WANDERING_TRADER, level));
    reloadedDefensive->load(&defensiveSave);
    reloadedDefensive->serverAiStep();
    if (reloadedDefensive->isUsingItem() || !reloadedDefensive->getEffect(MobEffects::INVISIBILITY)->isVisible())
        return 134;
    std::printf("PASS: daytime defensive invisibility survives 12 seconds and reload; client emits %d potion particles; old hidden effects migrate\n", probe.spellParticles);
    auto freed = boost::static_pointer_cast<TraderLlama>(llamas->front());
    auto escort = boost::static_pointer_cast<TraderLlama>(llamas->back());
    auto attacker = boost::static_pointer_cast<LivingEntity>(EntityIO::newEntity(EntityType::ZOMBIE, level));
    attacker->moveTo(escort->mX + 1, escort->mY, escort->mZ, 0.0f, 0.0f);
    ++trader->mTickCount;
    trader->setLastHurtByMob(attacker);
    for (int tick = 0; tick < 4 && escort->getTarget() != attacker; ++tick)
        escort->serverAiStep();
    if (escort->getTarget() != attacker)
        return 119;
    std::puts("PASS: escort targets the trader's attacker");

    CompoundTag llamaTag;
    escort->saveWithoutId(&llamaTag);
    auto reloadedEscort = boost::static_pointer_cast<TraderLlama>(EntityIO::newEntity(EntityType::TRADER_LLAMA, level));
    reloadedEscort->load(&llamaTag);
    reloadedEscort->tickLeash();
    if (reloadedEscort->getLeashHolder() != trader)
        return 114;
    freed->dropLeash(true, false);
    level->setGameTime(expires);
    trader->tick();
    escort->tick();
    reloadedEscort->tick();
    freed->tick();
    if (!trader->mRemoved || !escort->mRemoved || !reloadedEscort->mRemoved || freed->mRemoved)
        return 115;
    std::puts("PASS: trader and attached escort expire, released llamas survive, and invisibility follows night and dawn");
    return 0;
}
