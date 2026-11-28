#pragma once

#include <cstdio>
#include <memory>

#include "game/client/render/EntityRenderDispatcher.hpp"
#include "game/client/render/IllagerModel.hpp"
#include "game/client/render/ModelPart.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "game/client/render/Textures.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/Difficulty.hpp"
#include "game/core/EquipmentSlot.hpp"
#include "game/core/GameType.hpp"
#include "game/core/Pillager.hpp"
#include "game/core/Settings.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/MockedLevelStorage.hpp"
#include "game/entity/mob/EntityIO.hpp"
#include "game/entity/projectile/Arrow.hpp"
#include "game/item/CrossbowItem.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/SpawnEggItem.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/ClientboundAddMobPacket.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/util/AABB.hpp"
#include "game/util/Random.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/LevelSettings.hpp"
#include "game/world/generation/PlayerList.hpp"
#include "game/world/generation/Villager.hpp"

inline int RunPillagerRegression()
{
    CGameNetworkManager::sInstance.Initialise();
    const auto storage = boost::shared_ptr<MockedLevelStorage>(new MockedLevelStorage());
    auto* server = new MinecraftServer();
    gMinecraftServer = server;
    server->mSettings = new Settings();
    server->mLevels = arrayWithLength<ServerLevel*>(3, true);
    server->setPlayers(new PlayerList(server, nullptr));

    LevelSettings settings(8531, GameType::SURVIVAL, false, false, false, LevelType::FLAT,
                           54, 3, 128, false, 0, 0, 0);
    auto* data = new LevelData(&settings, L"Pillager test");
    data->setDifficulty(Difficulty::NORMAL);
    auto* level = new ServerLevel(server, storage, data, 0);
    server->mLevels[0] = level;
    level->postConstruct();
    level->mListeners.clear();
    static_cast<ServerChunkCache*>(level->getChunkSource())->create(0, 0);

    const auto ground = level->getTopSolidBlockPos(BlockPos(4, 4, 7));
    level->mRandom->setSeed(8531);
    int spawnError = 0;
    auto entity = SpawnEggItem::spawnMobAt(level, ResourceLocation(L"pillager"), ground.x + 0.5, ground.y,
                                           ground.z + 0.5, &spawnError, L"");
    auto pillager = boost::dynamic_pointer_cast<Pillager>(entity);
    if (!pillager || pillager->getMainHandItem()->getItem() != Items::crossbow)
        return 120;
    pillager->getRandom()->setSeed(8531);
    int captains = 0;
    boost::shared_ptr<Pillager> spawnedCaptain;
    for (int seed = 0; seed < 256; ++seed)
    {
        auto candidate = boost::dynamic_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, level));
        candidate->getRandom()->setSeed(seed);
        delete candidate->finalizeMobSpawn(level->getCurrentDifficultyAt(ground), nullptr, L"");
        if (candidate->isCaptain())
        {
            ++captains;
            spawnedCaptain = candidate;
            if (candidate->getItemSlot(EquipmentSlot::HEAD)->getItem() != Items::banner)
                return 130;
        }
        else if (candidate->getItemSlot(EquipmentSlot::HEAD)->getItem() == Items::banner)
            return 131;
    }
    if (captains == 0 || captains == 256)
        return 132;
    CompoundTag captainTag;
    spawnedCaptain->saveWithoutId(&captainTag);
    auto savedCaptain = boost::dynamic_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, level));
    savedCaptain->load(&captainTag);
    if (!savedCaptain->isCaptain() || savedCaptain->getItemSlot(EquipmentSlot::HEAD)->getItem() != Items::banner)
        return 133;
    delete savedCaptain->finalizeMobSpawn(level->getCurrentDifficultyAt(ground), nullptr, L"");
    if (!savedCaptain->isCaptain())
        return 134;
    std::printf("PASS: spawn finalization creates captains with banners (%d/256 seeds) and preserves assigned leaders\n", captains);
    ClientboundAddMobPacket packet(pillager, 0, 0, 0, 0, 0, 0);
    if (!boost::dynamic_pointer_cast<Pillager>(EntityIO::newEntity(packet.getType(), level)))
        return 121;

    auto* minecraft = Minecraft::GetInstance();
    auto* renderer = minecraft->mEntityRenderDispatcher->getRenderer(entity);
    const auto* texture = renderer ? renderer->getTextureLocation(entity) : nullptr;
    if (!texture || texture->getPath() != L"mob/pillager" || renderer->getModelName() != L"pillager")
        return 122;
    auto* pack = TexturePackRepository::defaultTexturePack;
    const auto* definition = pack->GetCustomModel(L"pillager");
    if (!definition || !definition->hasModelPart(L"rightarm") || !definition->hasModelPart(L"leftLeg"))
        return 123;
    renderer->setupForTexturePack(pack);
    auto* model = dynamic_cast<IllagerModel*>(renderer->getModel());
    if (!model || !model->mHead || !model->getArm(HumanoidArm::RIGHT))
        return 124;
    std::unique_ptr<BufferedImage> skin(minecraft->mTextures->readImage(TN_UNINDEXED, texture->getPath()));
    if (!skin || skin->getWidth() != 64 || skin->getHeight() != 64)
        return 125;
    std::puts("PASS: pillager spawn egg, mob packet, TU80 model and texture");

    auto resident = boost::shared_ptr<Villager>(new Villager(level));
    resident->moveTo(ground.x + 6.5, ground.y, ground.z + 0.5, 0.0f, 0.0f);
    if (!level->addEntity(resident))
        return 126;
    bool acquired = false, charging = false, charged = false, fired = false;
    for (int tick = 0; tick < 160 && !fired; ++tick)
    {
        ++pillager->mTickCount;
        pillager->tick();
        acquired |= pillager->getTarget() == resident;
        charging |= pillager->getArmPose() == ILLAGER_ARM_POSE_CROSSBOW_CHARGE;
        const bool nowCharged = CrossbowItem::isCharged(pillager->getMainHandItem());
        fired = charged && !nowCharged;
        charged |= nowCharged;
    }
    std::unique_ptr<std::vector<boost::shared_ptr<Entity>>> arrows(level->getEntitiesOfClass(
        typeid(Arrow), AABB::newTemp(1, ground.y - 1, 2, 15, ground.y + 5, 14)));
    if (!acquired || !charging || !charged || !fired || arrows->size() != 1)
    {
        std::printf("Pillager attack: target=%d charging=%d loaded=%d fired=%d arrows=%zu\n",
                    acquired, charging, charged, fired, arrows->size());
        return 127;
    }
    std::puts("PASS: pillager acquires a villager, charges a crossbow and fires a real arrow");

    pillager->setCaptain(true);
    if (!pillager->isCaptain() || pillager->getItemSlot(EquipmentSlot::HEAD)->getItem() != Items::banner)
        return 128;
    CompoundTag saved;
    pillager->saveWithoutId(&saved);
    auto restored = boost::dynamic_pointer_cast<Pillager>(EntityIO::newEntity(EntityType::PILLAGER, level));
    restored->load(&saved);
    if (!restored->isCaptain() || restored->getItemSlot(EquipmentSlot::HEAD)->getItem() != Items::banner
        || restored->getMainHandItem()->getItem() != Items::crossbow)
        return 129;
    std::puts("PASS: captain role, banner and crossbow survive reload");
    return 0;
}
