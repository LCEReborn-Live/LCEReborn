#include "game/server/ServerPlayer.hpp"
#include "game/core/StringIds.hpp"
#include "game/core/EntityType.hpp"
#include "game/core/GameMode.hpp"
#include "game/core/Mth.hpp"
#include "game/core/VideoSettings.hpp"
#include "game/core/ResultSlot.hpp"
#include "game/item/ArmorItem.hpp"
#include "game/core/Stat.hpp"
#include "game/item/Inventory.hpp"
#include "game/network/ClientboundAnimatePacket.hpp"
#include "game/network/ClientboundAwardStatPacket.hpp"
#include "game/network/ClientboundContainerSetContentPacket.hpp"
#include "game/network/ClientboundContainerSetSlotPacket.hpp"
#include "game/network/ClientboundContainerSetDataPacket.hpp"
#include "game/network/ClientboundChatPacket.hpp"
#include "game/network/GameModePacket.hpp"
#include "game/network/ClientboundPlayerAbilitiesPacket.hpp"
#include "game/network/ClientboundRemoveEntitiesPacket.hpp"
#include "game/network/ClientboundRemoveMobEffectPacket.hpp"
#include "game/network/ClientboundSetExperiencePacket.hpp"
#include "game/network/ClientboundSetHealthPacket.hpp"
#include "game/network/ClientboundUpdateMobEffectPacket.hpp"
#include "game/network/ClientboundContainerClosePacket.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/network/ClientboundCustomPayloadPacket.hpp"
#include "game/network/ClientboundEntityEventPacket.hpp"
#include "game/network/ClientboundGameEventPacket.hpp"
#include "game/network/ClientboundLevelEventPacket.hpp"
#include "game/network/ClientboundOpenSignEditorPacket.hpp"
#include "game/network/ClientboundPlayerCombatPacket.hpp"
#include "game/network/ClientboundPlayerSleepPacket.hpp"
#include "game/network/ClientboundSoundPacket.hpp"
#include "game/network/ServerGamePacketListenerImpl.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/ServerChunkCache.hpp"
#include "game/world/generation/LargeFeature.hpp"
#include "game/world/Dimension.hpp"
#include "game/ui/AbstractContainerMenu.hpp"
#include "game/ui/ChestMenu.hpp"
#include "game/ui/CraftingMenu.hpp"
#include "game/ui/FireworksMenu.hpp"
#include "game/ui/HorseInventoryMenu.hpp"
#include "game/ui/MerchantMenu.hpp"
#include "game/entity/mob/AbstractHorse.hpp"
#include "game/entity/mob/Entity.hpp"
#include "game/entity/mob/EntityTracker.hpp"
#include "game/entity/mob/SynchedEntityData.hpp"
#include "game/item/Container.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/item/LockableContainer.hpp"
#include "game/item/MerchantContainer.hpp"
#include "game/item/MerchantRecipeList.hpp"
#include "game/client/audio/SoundEvents.hpp"
#include "game/client/render/PlayerEnderChestContainer.hpp"
#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/InteractionObject.hpp"
#include "game/core/LockCode.hpp"
#include "game/core/Merchant.hpp"
#include "game/world/Level.hpp"
#include "game/world/LevelEvent.hpp"
#include "game/world/block/BlockPos.hpp"
#include "game/world/block/SignBlockEntity.hpp"
#include "game/client/render/LevelRenderer.hpp"
#include "game/core/System.hpp"
#include "game/core/GameType.hpp"
#include "game/server/LeaderboardSavedData.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/server/ServerPlayerGameMode.hpp"
#include "game/server/ServerItemCooldowns.hpp"
#include "game/world/LevelData.hpp"
#include "game/world/WorldBorder.hpp"
#include "game/world/block/BlockState.hpp"
#include "game/util/Random.hpp"
#include "game/core/GameRule.hpp"
#include "game/core/GameRulesInstance.hpp"
#include "game/network/INetworkPlayer.hpp"
#include "game/core/BaseAttributeMap.hpp"
#include "game/core/ModifiableAttributeInstance.hpp"
#include "game/core/PIXNamedEvents.hpp"
#include "game/entity/mob/MobEffects.hpp"
#include "game/entity/mob/MobEffectInstance.hpp"
#include "game/core/DataWalker.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/DataFixer.hpp"
#include "game/data/DataFixerUpper.hpp"
#include "game/data/DataFixTypes.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/MasterGameMode.hpp"
#include "game/network/IChunkHistory.hpp"
#include "game/network/BlockRegionUpdatePacket.hpp"
#include "game/network/CGameNetworkManager.hpp"
#include "game/network/Connection.hpp"
#include "game/minigame/EMiniGameId.hpp"
#include "game/world/block/BlockEntity.hpp"
#include "game/world/chunk/LevelChunk.hpp"
#include "game/world/DimensionType.hpp"
#include "game/client/render/LevelRenderer.hpp"
#include "game/core/DamageSource.hpp"
#include "game/core/GameRules.hpp"
#include "game/core/Team.hpp"
#include "game/core/CombatTracker.hpp"
#include "game/core/L10N.hpp"
#include "game/entity/mob/EntityDamageSource.hpp"
#include "game/entity/projectile/Arrow.hpp"
#include "game/item/Item.hpp"
#include "game/core/GenericStats.hpp"
#include "game/minigame/MiniGameDef.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/block/FenceBlock.hpp"
#include "game/world/block/FenceGateBlock.hpp"
#include "game/world/block/Material.hpp"
#include "game/world/block/WallBlock.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/world/generation/PlayerList.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <climits>
#include <limits>
#include <set>
#include <vector>
#include "game/item/ComplexItem.hpp"

namespace
{
const int CHUNK_SIZE = 16;
const int CHUNK_HEIGHT = 256;
const int MAX_DELAYED_CHUNK_PACKETS = 3;
const int MAX_REMOVED_ENTITIES_PER_PACKET = INT_MAX;
}

ServerPlayer::ServerPlayer(MinecraftServer* server, ServerLevel* level, const std::wstring& name,
                           ServerPlayerGameMode* gameMode)
    : Player((Level*)level, name),
      mConnection(),
      mServer(server),
      mGameMode(gameMode),
      mLoadedChunks(),
      mEntitiesToRemove(),
      mTrackedChunks(),
      mState(0),
      mChunkRequestTime(0),
      mChunkViewDistance(VideoSettings::RENDER_DISTANCE_DEFAULT),
      mLevitationSourceY(NULL),
      mLastRecordedHealth(std::numeric_limits<float>::min()),
      mLastRecordedFood(std::numeric_limits<int>::min()),
      mLastRecordedAir(std::numeric_limits<int>::min()),
      mLastRecordedArmor(std::numeric_limits<int>::min()),
      mLastRecordedExperienceLevel(std::numeric_limits<int>::min()),
      mLastRecordedTotalExperience(std::numeric_limits<int>::min()),
      mLastSentHealth(-100000000.0f),
      mLastSentFood(-99999999),
      mLastSentSaturationWasZero(true),
      mLastSentTotalExperience(-99999999),
      mSpawnInvulnerableTime(0),
      mLastActionTime(0),
      mLastChunkSendingTick(0),
      mSharedState(),
      mChangingDimension(0),
      mFallStartPosition(NULL),
      mDisconnected(false),
      mContainerCounter(0),
      mIsChangingQuantityOnly(false),
      latency(0),
      mRespawnState(false),
      mWonGame(false),
      mChangedDimension(0),
      mCauseOfDeathId(-1),
      mPlayerWeaponId(-1),
      mChunkRadius(0)
{
    LivingEntity::fjDerivedCtorCalls();
    mGameMode->mPlayer = this;
    mDisconnected = false;
    mLastSentX = 0.0;
    mLastSentZ = 0.0;
    mSpawnInvulnerableTime = Minecraft::InMiniGame(eMiniGameId_Glide, true) ? 0 : 60;

    BlockPos spawn = level->getSharedSpawnPos();
    if (!level->mDimension->isHasCeiling() && level->getLevelData()->getGameType() != GameType::ADVENTURE)
    {
        level->setFindingSpawn(true);
        int radius = std::max(0, server->getSpawnRadius(level));
        int borderDistance = Mth::floor(level->getWorldBorder()->getDistanceToBorder(spawn.x, spawn.z));
        radius = std::min(radius, borderDistance);
        if (borderDistance < 2)
            radius = 1;
        int minCoordinate = -level->mDimension->getXZSize() * 16 / 2;
        int maxCoordinate = level->mDimension->getXZSize() * 16 / 2 - 1;
        double originalX = spawn.x + 0.5;
        double originalY = spawn.y;
        double originalZ = spawn.z + 0.5;
        int attempts = 0;
        int waterDepth;
        bool playerNearby;
        do
        {
            do
            {
                int dx = mRandom->nextInt(radius * 2 + 1) - radius;
                int dz = mRandom->nextInt(radius * 2 + 1) - radius;
                spawn = level->getTopSolidBlockPos(spawn.offset(dx, 0, dz));
            } while (spawn.x > maxCoordinate || spawn.x < minCoordinate || spawn.z > maxCoordinate
                     || spawn.z < minCoordinate);
            waterDepth = 0;
            for (int y = 0; y < 128; ++y)
            {
                if (level->getBlockState(BlockPos(spawn.x, y, spawn.z))->getBlock() != Blocks::flowing_water
                    && level->getBlockState(BlockPos(spawn.x, y, spawn.z))->getBlock() != Blocks::water)
                    break;
                ++waterDepth;
            }
            ++attempts;
            playerNearby = level->getNearestPlayer(originalX, originalY, originalZ, 3.0, false, -1.0) != NULL;
        } while (waterDepth > 1 && !playerNearby && attempts < 20);
        level->setFindingSpawn(false);
    }
    mMaxUpStep = 1.0f;
    moveTo(spawn, 0.0f, 0.0f);
    mField0x738 = new ServerItemCooldowns(this);
}

ServerPlayer::~ServerPlayer()
{
    delete mFallStartPosition;
    delete mGameMode;
    delete mLevitationSourceY;
}

void ServerPlayer::sendMessage(ClientboundChatPacket::EChatPacketMessage message, int32_t* intParams,
                               uint32_t numIntParams, std::wstring* stringParams, uint32_t numStringParams)
{
    mConnection->send(boost::shared_ptr<ClientboundChatPacket>(
        new ClientboundChatPacket(message, intParams, numIntParams, stringParams, numStringParams)));
}

bool ServerPlayer::hasPermission(EGameCommand)
{
    PlayerList* players = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
    return players->isOp(boost::dynamic_pointer_cast<ServerPlayer>(shared_from_this()));
}

BlockPos ServerPlayer::getCommandSenderBlockPosition()
{
    return BlockPos(Mth::floor(mX), Mth::floor(mY + 0.5), Mth::floor(mZ));
}

void ServerPlayer::refreshContainer(AbstractContainerMenu* menu, std::vector<not_null_ptr<ItemInstance> >* items)
{
    mConnection->send(boost::shared_ptr<Packet>(new ClientboundContainerSetContentPacket(menu->m_containerId, items)));
    mConnection->send(
        boost::shared_ptr<Packet>(new ClientboundContainerSetSlotPacket(-1, -1, mInventory->getCarried())));
}

void ServerPlayer::slotChanged(AbstractContainerMenu* menu, int32_t slot, not_null_ptr<ItemInstance> item)
{
    if (dynamic_cast<ResultSlot*>(menu->getSlot(slot)) != NULL || mIsChangingQuantityOnly)
    {
        return;
    }
    mConnection->send(
        boost::shared_ptr<Packet>(new ClientboundContainerSetSlotPacket(menu->m_containerId, slot, item)));
}

void ServerPlayer::setContainerData(AbstractContainerMenu* menu, int32_t dataIndex, int32_t value)
{
    mConnection->send(
        boost::shared_ptr<Packet>(new ClientboundContainerSetDataPacket(menu->m_containerId, dataIndex, value)));
}

void ServerPlayer::setAllContainerData(AbstractContainerMenu* menu, boost::shared_ptr<Container> container)
{
    for (int index = 0; index < container->getContainerDataCount(); ++index)
    {
        mConnection->send(boost::shared_ptr<Packet>(
            new ClientboundContainerSetDataPacket(menu->m_containerId, index, container->getContainerData(index))));
    }
}

void ServerPlayer::flagEntitiesToBeRemoved(uint32_t* removedFlags, bool* flagsInitialised)
{
    if (!mEntitiesToRemove.empty())
    {
        if (!*flagsInitialised)
        {
            *flagsInitialised = true;
            std::memset(removedFlags, 0, sizeof(Entity::sSmallIdsToRemove));
        }
        for (std::vector<int32_t>::iterator it = mEntitiesToRemove.begin(); it != mEntitiesToRemove.end(); ++it)
        {
            int id = *it;
            if (id < 2048)
                removedFlags[id / 32] |= 0x80000000 >> (id % 32);
        }
    }
}

int ServerPlayer::getFlagIndexForChunk(const ChunkPos& chunkPos, int dimension)
{
    return getFlagIndexForChunk(chunkPos.x, chunkPos.z, dimension);
}

void ServerPlayer::disconnect()
{
    mDisconnected = true;
    ejectPassengers();
    if (mSleeping != 0)
    {
        stopSleepInBed(true, false, false);
    }
}

ServerLevel* ServerPlayer::getLevel()
{
    return static_cast<ServerLevel*>(mLevel);
}

void ServerPlayer::doTickA()
{
    PIXBeginNamedEvent(0.0f, "Player::tick");
    Player::tick();
    PIXEndNamedEvent();

    PIXBeginNamedEvent(0.0f, "Update inventory");
    for (unsigned int slot = 0; slot < mInventory->getContainerSize(); ++slot)
    {
        not_null_ptr<ItemInstance> item = mInventory->getItem(slot);
        if (!item->isEmpty() && item->getItem()->isComplex())
        {
            boost::shared_ptr<Packet> packet =
                dynamic_cast<ComplexItem*>(item->getItem())
                    ->getUpdatePacket(item, mLevel, boost::static_pointer_cast<Player>(shared_from_this()));
            if (packet != NULL)
                mConnection->send(packet);
        }
    }
    PIXEndNamedEvent();
}

void ServerPlayer::broadcast(boost::shared_ptr<BlockEntity> blockEntity, bool delay)
{
    if (blockEntity == NULL)
        return;

    boost::shared_ptr<Packet> packet = blockEntity->getUpdatePacket();
    if (packet == NULL)
        return;

    packet->shouldDelay = delay;
    if (!delay)
        mConnection->send(packet);
    else
        mConnection->queueSend(packet);
}

void ServerPlayer::doChunkSendingTick(bool dontDelay, bool historyOnly)
{
    bool resendChangedChunks = false;
    if (!mLoadedChunks.empty())
    {
        MasterGameMode* masterGameMode = mServer->mMasterGameMode;
        ChunkPos closestChunk = mLoadedChunks.front();
        double closestDistance = std::numeric_limits<double>::max();
        bool foundChunk = false;
        for (std::list<ChunkPos>::iterator chunkIt = mLoadedChunks.begin(); chunkIt != mLoadedChunks.end();)
        {
            ChunkPos chunkPos = *chunkIt;
            if (Minecraft::InMiniGame(eMiniGameId_Any, true) && masterGameMode->IsRoundPlaying()
                && !MasterGameMode::IsChunkWithinActiveBounds(masterGameMode, chunkPos.x, chunkPos.z))
            {
                chunkIt = mLoadedChunks.erase(chunkIt);
                continue;
            }

            if (mLevel->isChunkFinalised(chunkPos.x, chunkPos.z)
                && (!Minecraft::InMiniGame(eMiniGameId_Any, true)
                    || !masterGameMode->IsChunkWithinLobbyRegionOfRoundLevel(chunkPos.x, chunkPos.z))
                && (!foundChunk || chunkPos.distanceToSqr(mX, mZ) < closestDistance)
                && (!historyOnly || masterGameMode->GetChunkHistory()->ShouldLoadOnClient(chunkPos.x, chunkPos.z)))
            {
                closestChunk = chunkPos;
                closestDistance = chunkPos.distanceToSqr(mX, mZ);
                foundChunk = true;
            }
            ++chunkIt;
        }

        bool sendChanges = false;
        if (foundChunk && Minecraft::InMiniGame(eMiniGameId_Any, true))
        {
            sendChanges = masterGameMode->GetChunkHistory()->ShouldLoadOnClient(closestChunk.x, closestChunk.z);
            if (sendChanges)
                dontDelay = true;
            else
                resendChangedChunks = true;
        }

        bool canSend = foundChunk;
        if (canSend && !mConnection->isLocal())
        {
            const bool clientReady = MinecraftServer::chunkPacketManagement_CanSendTo(mConnection->getNetworkPlayer());
            if (!dontDelay)
            {
                canSend = clientReady && mConnection->countDelayedPackets() <= MAX_DELAYED_CHUNK_PACKETS
                          && CGameNetworkManager::sInstance.GetHostPlayer()->GetSendQueueSizeMessages(NULL, true)
                                 <= MAX_DELAYED_CHUNK_PACKETS
                          && !mConnection->mDisconnected;
            }
            if (canSend)
            {
                mLastChunkSendingTick = mTickCount;
                MinecraftServer::chunkPacketManagement_DidSendTo(mConnection->getNetworkPlayer());
            }
        }
        else if (canSend && mConnection->mDisconnected)
        {
            canSend = false;
        }

        if (canSend)
        {
            ServerLevel* level = mServer->getLevel(mDimension);
            const int flagIndex = getFlagIndexForChunk(closestChunk, mLevel->mDimension->getType()->getId());
            const bool clientHasChunk = CGameNetworkManager::sInstance.SystemFlagGet(mConnection->getNetworkPlayer(),
                                                                                     flagIndex);
            LevelChunk* chunk = level->getChunk(closestChunk.x, closestChunk.z);
            if (chunk->getHasGapsToCheck() && level->mDimension->isHasSkyLight())
                chunk->recheckGaps(false);

            bool sendBlockEntities = !clientHasChunk;
            if (!mConnection->isLocal())
            {
                mLoadedChunks.remove(closestChunk);
                if (!clientHasChunk)
                {
                    BlockRegionUpdatePacket* regionPacket = NULL;
                    if (!sendChanges)
                    {
                        regionPacket = new BlockRegionUpdatePacket(closestChunk.x << 4, 0, closestChunk.z << 4,
                                                                   CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE, level, false,
                                                                   NULL);
                    }
                    else
                    {
                        std::vector<short>* changes = masterGameMode->GetChunkHistory()->GetChangesForChunk(
                            closestChunk.x, closestChunk.z);
                        if (changes != NULL && !changes->empty())
                        {
                            regionPacket = new BlockRegionUpdatePacket(closestChunk.x << 4, 0, closestChunk.z << 4,
                                                                       CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE, level,
                                                                       true, changes);
                        }
                    }

                    boost::shared_ptr<Packet> packet(regionPacket);
                    if (packet != NULL)
                    {
                        if (dontDelay)
                            packet->shouldDelay = false;
                        if (!packet->shouldDelay)
                            mConnection->send(packet);
                        else
                            mConnection->queueSend(packet);
                    }
                    CGameNetworkManager::sInstance.SystemFlagSet(mConnection->getNetworkPlayer(), flagIndex);
                }
            }
            else
            {
                MultiPlayerLevel* clientLevel = Minecraft::GetInstance()->getLevel(
                    level->mDimension->getType()->getId());
                if (clientLevel == NULL)
                {
                    sendBlockEntities = false;
                }
                else
                {
                    LevelChunk* clientChunk = clientLevel->getChunk(closestChunk.x, closestChunk.z);
                    mLoadedChunks.remove(closestChunk);
                    clientChunk->reSyncLighting();
                    clientChunk->recalcHeightmapOnly();
                    CGameNetworkManager::sInstance.SystemFlagSet(mConnection->getNetworkPlayer(), flagIndex);
                }
            }

            if (sendBlockEntities)
            {
                const int minX = closestChunk.x * CHUNK_SIZE;
                const int minZ = closestChunk.z * CHUNK_SIZE;
                std::vector<boost::shared_ptr<BlockEntity> >* blockEntities = level->getBlockEntitiesInRegion(
                    minX, 0, minZ, minX + CHUNK_SIZE, CHUNK_HEIGHT, minZ + CHUNK_SIZE);
                for (unsigned int index = 0; index < blockEntities->size(); ++index)
                    broadcast(blockEntities->at(index), !mConnection->isLocal() && !dontDelay);
                delete blockEntities;
            }
        }
    }

    if (!historyOnly && resendChangedChunks)
        doChunkSendingTick(true, true);
}

void ServerPlayer::doTickB()
{
    if (getHealth() != mLastSentHealth || mLastSentFood != mFoodData.getFoodLevel()
        || (mFoodData.getSaturationLevel() == 0.0f) != mLastSentSaturationWasZero)
    {
        if ((getKillCredit() != NULL && !eTYPE_DERIVED_FROM(EntityType::PLAYER, getKillCredit()->GetType()))
            || getKillCredit() == NULL)
            mCauseOfDeathId = -1;
        TelemetryDamageSource damageSource = {static_cast<ETelemetryChallenges>(mPvpState), mCauseOfDeathId,
                                              mPlayerWeaponId};
        mConnection->send(boost::shared_ptr<ClientboundSetHealthPacket>(new ClientboundSetHealthPacket(
            getHealth(), mFoodData.getFoodLevel(), mFoodData.getSaturationLevel(), damageSource)));
        mLastSentHealth = getHealth();
        mLastSentFood = mFoodData.getFoodLevel();
        mLastSentSaturationWasZero = mFoodData.getSaturationLevel() == 0.0f;
    }

    if (getHealth() + getAbsorptionAmount() != mLastRecordedHealth)
        mLastRecordedHealth = getHealth() + getAbsorptionAmount();
    if (mFoodData.getFoodLevel() != mLastRecordedFood)
        mLastRecordedFood = mFoodData.getFoodLevel();
    if (getAirSupply() != mLastRecordedAir)
        mLastRecordedAir = getAirSupply();
    if (getArmorValue() != mLastRecordedArmor)
        mLastRecordedArmor = getArmorValue();
    if (mTotalExperience != mLastRecordedTotalExperience)
        mLastRecordedTotalExperience = mTotalExperience;
    if (mExperienceLevel != mLastRecordedExperienceLevel)
        mLastRecordedExperienceLevel = mExperienceLevel;

    if (mTotalExperience != mLastSentTotalExperience)
    {
        mLastSentTotalExperience = mTotalExperience;
        mConnection->send(boost::shared_ptr<ClientboundSetExperiencePacket>(
            new ClientboundSetExperiencePacket(mExperienceProgress, mTotalExperience, mExperienceLevel)));
    }
}

void ServerPlayer::doTick(bool tickFrame, bool sendChunks, bool ignorePortals)
{
    mIgnorePortals = ignorePortals;

    if (tickFrame)
    {
        PIXBeginNamedEvent(0.0f, "Update frame tick");
        updateFrameTick();
        PIXEndNamedEvent();
    }

    PIXBeginNamedEvent(0.0f, "doTickA");
    doTickA();
    PIXEndNamedEvent();

    if (tickFrame)
    {
        PIXBeginNamedEvent(0.0f, "doChunkSendingTick");
        doChunkSendingTick(sendChunks, false);
        PIXEndNamedEvent();
    }

    PIXBeginNamedEvent(0.0f, "doTickB");
    doTickB();
    PIXEndNamedEvent();

    mIgnorePortals = false;
}

void ServerPlayer::initMenu()
{
    mContainerMenu->addSlotListener(this);
}

boost::shared_ptr<ServerPlayer> ServerPlayer::SpectatePlayerChange(bool previous)
{
    std::vector<boost::shared_ptr<ServerPlayer> > candidates;
    int currentIndex = -1;
    PlayerList* playerList = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
    std::vector<boost::shared_ptr<ServerPlayer> >* allPlayers = playerList->getPlayers();
    for (unsigned int index = 0; index < allPlayers->size(); ++index)
    {
        if (index >= mLevel->mPlayers.size() || mLevel->mPlayers[index] == NULL
            || mLevel->mPlayers[index]->isSpectator())
            continue;

        candidates.push_back(allPlayers->at(index));
        const bool isCurrent = allPlayers->at(index).get() == GetCamera().get();
        if (isCurrent)
            currentIndex = static_cast<int>(candidates.size()) - 1;
    }

    boost::shared_ptr<ServerPlayer> target;
    if (!candidates.empty())
    {
        int nextIndex;
        if (currentIndex == -1)
            nextIndex = previous ? -1 : 0;
        else
            nextIndex = previous ? currentIndex - 1 : currentIndex + 1;
        nextIndex = (candidates.size() + nextIndex) % candidates.size();

        target = candidates[nextIndex];
        std::wstring name = target->getName();
        int messageParams[2] = {nextIndex + 1, static_cast<int>(candidates.size())};
        sendMessage(ClientboundChatPacket::eChatPacketMessage_NowSpectating, messageParams, 2, &name, 1);
    }

    SetCamera(target);
    return target;
}

void ServerPlayer::updateClientsCameras()
{
    GameModePacket::SetCameraData data;
    data.camera = mPlayerIndex;
    data.target = GetCamera() ? GetCamera()->getId() : -1;
    PlayerList* players = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
    players->broadcastAll(GameModePacket::Create(GameModePacket::eMessage_SetCamera, data));
}

void ServerPlayer::notifyPlayerRespawn(boost::shared_ptr<ServerPlayer> oldPlayer,
                                       boost::shared_ptr<ServerPlayer> newPlayer)
{
    if (mCamera && mCamera->getId() == oldPlayer->getId())
    {
        if (!newPlayer->isSpectator())
        {
            SetCamera(newPlayer);
        }
        else
        {
            SpectatePlayerChange(false);
        }
        updateClientsCameras();
    }
}

void ServerPlayer::flushEntitiesToRemove()
{
    while (!mEntitiesToRemove.empty())
    {
        int count = std::min(static_cast<int>(mEntitiesToRemove.size()), MAX_REMOVED_ENTITIES_PER_PACKET);
        arrayWithLength<int> entityIds(count, true);
        int index = 0;
        for (std::vector<int32_t>::iterator it = mEntitiesToRemove.begin();
             it != mEntitiesToRemove.end() && index < count; ++index)
        {
            entityIds[index] = *it;
            it = mEntitiesToRemove.erase(it);
        }
        mConnection->send(boost::shared_ptr<Packet>(new ClientboundRemoveEntitiesPacket(entityIds)));
    }
}

void ServerPlayer::markAttributesDirty()
{
    BaseAttributeMap* attributes = getAttributes();
    std::vector<AttributeInstance*> instances;
    attributes->getAttributes(instances);
    for (std::vector<AttributeInstance*>::iterator it = instances.begin(); it != instances.end(); ++it)
    {
        ModifiableAttributeInstance* instance = dynamic_cast<ModifiableAttributeInstance*>(*it);
        if (instance != NULL)
            instance->setDirty();
    }
}

void ServerPlayer::refreshContainer(AbstractContainerMenu* menu)
{
    AbstractContainerMenu::ItemList* items = menu->getItems();
    refreshContainer(menu, items);
    delete items;
}

void ServerPlayer::resetSentInfo()
{
    mLastSentHealth = -100000000.0f;
}

int32_t ServerPlayer::getFlagIndexForChunk(int32_t x, int32_t z, int32_t dimension)
{
    return ((dimension + 1) & 3) << 28 | (x & 0x3FFF) << 14 | (z & 0x3FFF);
}

void ServerPlayer::resetChunkFlagsOutsideArea(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ, int32_t dimension)
{
    const int width = maxZ - minZ;
    const int height = maxX - minX;
    unsigned char* snapshot = new unsigned char[width * height];

    for (int x = minX; x < maxX; ++x)
    {
        for (int z = minZ; z < maxZ; ++z)
        {
            const int flagIndex = getFlagIndexForChunk(x, z, dimension);
            snapshot[(x - minX) * width + (z - minZ)] = static_cast<unsigned char>(
                CGameNetworkManager::sInstance.SystemFlagGet(mConnection->getNetworkPlayer(), flagIndex));
        }
    }

    CGameNetworkManager::sInstance.SystemFlagClearAll(mConnection->getNetworkPlayer());

    for (int x = minX; x < maxX; ++x)
    {
        for (int z = minZ; z < maxZ; ++z)
        {
            const int flagIndex = getFlagIndexForChunk(x, z, dimension);
            if (snapshot[(x - minX) * width + (z - minZ)] != 0)
                CGameNetworkManager::sInstance.SystemFlagSet(mConnection->getNetworkPlayer(), flagIndex);
        }
    }

    delete[] snapshot;
}

void ServerPlayer::readAdditionalSaveData(CompoundTag* tag)
{
    Player::readAdditionalSaveData(tag);
    mTu80OceanBiomes = static_cast<unsigned int>(tag->getInt(L"Tu80OceanBiomes")) & 0x3ffu;

    if (tag->contains(std::wstring(L"playerGameType"), 99))
    {
        if (getServer()->getForceGameType())
            mGameMode->SetGameType(getServer()->getDefaultGameType());
        else
            mGameMode->SetGameType(GameType::byId(tag->getInt(std::wstring(L"playerGameType"))));
    }

    GameRulesInstance* gameRules = mGameMode->mGameRules;
    if (tag->contains(std::wstring(L"GameRules")) && gameRules != NULL)
    {
        arrayWithLength<unsigned char> data = tag->getByteArray(std::wstring(L"GameRules"));
        ByteArrayInputStream bytes(data);
        DataInputStream input(&bytes);
        gameRules->read(&input);
        input.close();
        bytes.close();
        bytes.reset();
    }
}

void ServerPlayer::addWalkers(DataFixerUpper* dataFixer)
{
    class ServerPlayerDataWalker : public DataWalker
    {
    public:
        CompoundTag* walk(DataFixer* fixer, CompoundTag* tag, int version)
        {
            if (tag->contains(L"RootVehicle", Tag::TAG_COMPOUND))
            {
                CompoundTag* root = tag->getCompound(L"RootVehicle");
                if (root->contains(L"Entity", Tag::TAG_COMPOUND))
                {
                    root->put(L"Entity",
                              fixer->update(DataFixTypes::eDataFixTypes_Entity, root->getCompound(L"Entity"), version));
                }
            }
            return tag;
        }
    };

    dataFixer->addWalker(DataFixTypes::eDataFixTypes_Player, new ServerPlayerDataWalker());
}

void ServerPlayer::addAdditonalSaveData(CompoundTag* tag)
{
    Player::addAdditonalSaveData(tag);
    tag->putInt(L"Tu80OceanBiomes", static_cast<int>(mTu80OceanBiomes));

    GameRulesInstance* gameRules = mGameMode->mGameRules;
    if (gameRules != NULL)
    {
        ByteArrayOutputStream bytes;
        DataOutputStream output(&bytes);
        gameRules->write(&output);
        tag->putByteArray(std::wstring(L"GameRules"), bytes.mBuffer);
        bytes.mBuffer.data = NULL;
        output.close();
    }

    boost::shared_ptr<Entity> rootVehicle = getRootVehicle();
    boost::shared_ptr<Entity> vehicle = getVehicle();
    if (vehicle != NULL && rootVehicle.get() != this)
    {
        std::set<boost::shared_ptr<Entity> >* passengers = rootVehicle->getIndirectPassengers(
            EntityType::SERVER_PLAYER);
        const size_t passengerCount = passengers->size();
        delete passengers;

        if (passengerCount == 1)
        {
            CompoundTag* rootTag = new CompoundTag();
            CompoundTag* entityTag = new CompoundTag();
            rootVehicle->saveAsPassenger(entityTag);
            rootTag->putString(std::wstring(L"Attach"), vehicle->getUUID());
            rootTag->put(std::wstring(L"Entity"), entityTag);
            tag->put(std::wstring(L"RootVehicle"), rootTag);
        }
    }
}

void ServerPlayer::giveExperienceLevels(int32_t levels)
{
    Player::giveExperienceLevels(levels);
    mLastSentTotalExperience = -1;
}

void ServerPlayer::onEnchantmentPerformed(int32_t cost)
{
    Player::onEnchantmentPerformed(cost);
    mLastSentTotalExperience = -1;
}

void ServerPlayer::onEnterCombat()
{
    LivingEntity::onEnterCombat();
    mConnection->send(boost::shared_ptr<ClientboundPlayerCombatPacket>(
        new ClientboundPlayerCombatPacket(getCombatTracker(), ClientboundPlayerCombatPacket::ENTER_COMBAT)));
}

void ServerPlayer::onLeaveCombat()
{
    LivingEntity::onLeaveCombat();
    mConnection->send(boost::shared_ptr<ClientboundPlayerCombatPacket>(
        new ClientboundPlayerCombatPacket(getCombatTracker(), ClientboundPlayerCombatPacket::END_COMBAT)));
}

void ServerPlayer::ResetSpawnInvulnerableTime()
{
    mSpawnInvulnerableTime = 60;
}

void ServerPlayer::tick()
{
    mGameMode->tick();
    --mSpawnInvulnerableTime;
    if (mInvulnerableTime > 0)
    {
        --mInvulnerableTime;
    }
    mContainerMenu->broadcastChanges();

    Biome* biome = mLevel->getBiome(BlockPos(shared_from_this()));
    if (biome->id > 127)
    {
        biome = Biome::getBiome(biome->id - 128);
    }
    if (biome != mLastBiome)
    {
        awardStat(GenericStats::enteredBiome(biome->id), GenericStats::param_enteredBiome(biome->id));
        mLastBiome = biome;
        // This generator has ten ocean variants, including deep warm ocean.
        const unsigned int previousOceanBiomes = mTu80OceanBiomes;
        const int oceans[] = {0, 24, 40, 41, 42, 43, 44, 45, 46, 47};
        for (unsigned int i = 0; i < sizeof(oceans) / sizeof(oceans[0]); ++i)
            if (biome->id == oceans[i])
                mTu80OceanBiomes |= 1u << i;
        if (previousOceanBiomes != 0x3ffu && mTu80OceanBiomes == 0x3ffu)
            awardStat(GenericStats::achievement(eAward_SailThe7Seas), GenericStats::param_noArgs());
    }

    if (mTickCount % 20 == 0 && mTu80DiscoveredStructures != 3u)
    {
        ServerChunkCache* chunks = dynamic_cast<ServerChunkCache*>(mLevel->getChunkSource());
        const BlockPos position(shared_from_this());
        if (chunks && !(mTu80DiscoveredStructures & 1u)
            && chunks->isPosInFeature(mLevel, LargeFeature::SHIPWRECK, position))
        {
            awardStat(GenericStats::achievement(eAward_Ahoy), GenericStats::param_noArgs());
            mTu80DiscoveredStructures |= 1u;
        }
        if (chunks && !(mTu80DiscoveredStructures & 2u)
            && chunks->isPosInFeature(mLevel, LargeFeature::RUIN, position))
        {
            awardStat(GenericStats::achievement(eAward_Atlantis), GenericStats::param_noArgs());
            mTu80DiscoveredStructures |= 2u;
        }
    }

    if (!mLevel->mIsClientSide && !mContainerMenu->stillValid(boost::dynamic_pointer_cast<Player>(shared_from_this())))
    {
        closeContainer();
        mContainerMenu = mInventoryMenu;
    }

    flushEntitiesToRemove();

    if (mCamera)
    {
        boost::shared_ptr<Player> target = boost::dynamic_pointer_cast<Player>(mCamera);
        if (target && target->isSpectator())
        {
            SpectatePlayerChange(false);
            updateClientsCameras();
        }
    }

    if (IsSpectatingOtherPlayer() && mTickCount % 20 == 0)
    {
        boost::shared_ptr<Entity> camera = GetCamera();
        teleportTo(camera->mX, camera->mY + camera->mBbHeight, camera->mZ);
    }

    if (!hasEffect(MobEffects::LEVITATION))
    {
        if (mLevitationSourceY != NULL)
        {
            delete mLevitationSourceY;
            mLevitationSourceY = NULL;
        }
    }
    else if (mLevitationSourceY != NULL)
    {
        int height = (int)(mY - *mLevitationSourceY);
        if (height >= 50)
            awardStat(GenericStats::greatViewFromUpHere(), GenericStats::param_greatViewFromUpHere(height));
    }
}

void ServerPlayer::die(const DamageSource* source)
{
    if (mInventory->getSelected()->isEmpty())
        mPlayerWeaponId = 0;
    else
        mPlayerWeaponId = Item::getId(mInventory->getSelected()->getItem());

    const bool showDeathMessages = mLevel->getGameRules()->getBoolean(GameRules::RULE_SHOWDEATHMESSAGES);
    mConnection->send(boost::shared_ptr<ClientboundPlayerCombatPacket>(new ClientboundPlayerCombatPacket(
        getCombatTracker(), ClientboundPlayerCombatPacket::ENTITY_DIED, showDeathMessages)));

    if (showDeathMessages)
    {
        Team* team = getTeam();
        if (team == NULL || team->getDeathMessageVisibility() == Team::ALWAYS)
        {
            PlayerList* players = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
            players->broadcastAll(getCombatTracker()->getDeathMessagePacket());
        }
        else if (team->getDeathMessageVisibility() == Team::HIDE_FOR_OTHER_TEAMS)
        {
            PlayerList* players = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
            players->broadcastToTeam(boost::static_pointer_cast<Player>(shared_from_this()),
                                     getCombatTracker()->getDeathMessagePacket());
        }
        else if (team->getDeathMessageVisibility() == Team::HIDE_FOR_OWN_TEAM)
        {
            PlayerList* players = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
            players->broadcastToAllExceptTeam(boost::static_pointer_cast<Player>(shared_from_this()),
                                              getCombatTracker()->getDeathMessagePacket());
        }
    }

    if (!mLevel->getGameRules()->getBoolean(GameRules::RULE_KEEPINVENTORY) && !isSpectator())
    {
        destroyVanishingCursedItems();
        mInventory->dropAll();
    }

    boost::shared_ptr<LivingEntity> killCredit = getKillCredit();
    if (killCredit != NULL)
    {
        killCredit->awardKillScore(shared_from_this(), mDeathScore, source);
    }

    mGameMode->OnDeath(boost::static_pointer_cast<ServerPlayer>(shared_from_this()));
    clearFire();
    setSharedFlag(FLAG_ONFIRE, false);
    getCombatTracker()->recheckStatus();
}

bool ServerPlayer::hurt(const DamageSource* source, float damage)
{
    if (isInvulnerableTo(source)
        || ((!mServer->isPvpAllowed() || Minecraft::InMiniGame(eMiniGameId_Battle, false)) && mSpawnInvulnerableTime > 0
            && source != DamageSource::OUT_OF_WORLD && source != DamageSource::CREATIVE_PLAYER))
        return false;

    if (source != NULL && dynamic_cast<const EntityDamageSource*>(source) != NULL)
    {
        boost::shared_ptr<Entity> attacker = source->getDirectEntity();

        if (eTYPE_DERIVED_FROM(EntityType::PLAYER, attacker->GetType())
            && !boost::static_pointer_cast<Player>(attacker)->canHarmPlayer(
                boost::dynamic_pointer_cast<Player>(shared_from_this())))
            return false;

        if (attacker != NULL && eTYPE_DERIVED_FROM(EntityType::ARROW, attacker->GetType())
            && !Minecraft::GetInstance()->GetMiniGame()->HasFeatureArrowCanKnockback())
        {
            boost::shared_ptr<Arrow> arrow = boost::static_pointer_cast<Arrow>(attacker);
            if (arrow->mOwner != NULL && eTYPE_DERIVED_FROM(EntityType::PLAYER, arrow->mOwner->GetType())
                && !canHarmPlayer(boost::dynamic_pointer_cast<Player>(arrow->mOwner)))
                return false;
        }
    }

    bool hurt = Player::hurt(source, damage);
    if (!hurt)
        return hurt;

    if (source == DamageSource::FALL)
    {
        mPvpState = 0x19;
    }
    else if (source == DamageSource::IN_FIRE || source == DamageSource::ON_FIRE)
    {
        mPvpState = 0x1b;
    }
    else if (source == DamageSource::LAVA)
    {
        mPvpState = 0x1a;
    }
    else if (source == DamageSource::DROWN)
    {
        mPvpState = 0x1c;
    }
    else if (source == DamageSource::IN_WALL)
    {
        mPvpState = 0x1d;
    }
    else if (source == DamageSource::OUT_OF_WORLD || source == DamageSource::CREATIVE_PLAYER)
    {
        mPvpState = 0x1e;
    }
    else if (source == DamageSource::CACTUS)
    {
        mPvpState = 0x1f;
    }
    else
    {
        mPvpState = 0;
        boost::shared_ptr<Entity> attacker = source->getEntity();
        if (attacker != NULL)
        {
            unsigned long long type = attacker->GetType();
            if (type == EntityType::PHANTOM)
                mPvpState = 0x46;
            else if (type == EntityType::PUFFERFISH)
                mPvpState = 0x54;
            else if (type == EntityType::DOLPHIN)
                mPvpState = 0x53;
            else if (type == EntityType::VINDICATION_ILLAGER)
                mPvpState = 0x52;
            else if (type == EntityType::EVOCATION_ILLAGER || type == EntityType::EVOCATION_FANGS)
                mPvpState = 0x51;
            else if (type == EntityType::LLAMA_SPIT || type == EntityType::LLAMA)
                mPvpState = 0x50;
            else if (type == EntityType::SHULKER || type == EntityType::SHULKER_BULLET)
                mPvpState = 0x4b;
            else if (type == EntityType::POLAR_BEAR)
                mPvpState = 0x4a;
            else if (type == EntityType::FIREBALL)
                mPvpState = 0x29;
            else if (type == EntityType::TIPPABLE_ARROW || type == EntityType::ARROW)
            {
                if (boost::dynamic_pointer_cast<Arrow>(attacker)->mOwner != NULL)
                {
                    boost::shared_ptr<Entity> owner = boost::dynamic_pointer_cast<Arrow>(attacker)->mOwner;
                    if (owner != NULL)
                    {
                        unsigned long long ownerType = owner->GetType();
                        if (ownerType == EntityType::SERVER_PLAYER || ownerType == EntityType::PLAYER)
                            mPvpState = 0x21;
                        else if (ownerType == EntityType::SKELETON)
                            mPvpState = 0x26;
                    }
                }
            }
            else if (type == EntityType::PRIMED_TNT)
                mPvpState = 0x22;
            else if (type == EntityType::LIGHTNING_BOLT)
                mPvpState = 0x45;
            else if (type == EntityType::RABBIT)
                mPvpState = 0x49;
            else if (type == EntityType::ENDERMITE)
                mPvpState = 0x48;
            else if (type == EntityType::GUARDIAN)
                mPvpState = 0x47;
            else if (type == EntityType::ENDER_DRAGON)
                mPvpState = 0x44;
            else if (type == EntityType::SILVERFISH)
                mPvpState = 0x43;
            else if (type == EntityType::WITHER_BOSS)
                mPvpState = 0x42;
            else if (type == EntityType::MAGMA_CUBE)
                mPvpState = 0x41;
            else if (type == EntityType::BLAZE)
                mPvpState = 0x40;
            else if (type == EntityType::ENDER_MAN)
                mPvpState = 0x3f;
            else if (type == EntityType::WITCH)
                mPvpState = 0x3e;
            else if (type == EntityType::SLIME)
                mPvpState = 0x28;
            else if (type == EntityType::GHAST)
                mPvpState = 0x29;
            else if (type == EntityType::PIG_ZOMBIE)
                mPvpState = 0x2a;
            else if (type == EntityType::DROWNED || type == EntityType::HUSK || type == EntityType::ZOMBIE)
                mPvpState = 0x25;
            else if (type == EntityType::CAVE_SPIDER)
                mPvpState = 0x3d;
            else if (type == EntityType::SPIDER)
                mPvpState = 0x27;
            else if (type == EntityType::STRAY || type == EntityType::WITHER_SKELETON || type == EntityType::SKELETON)
                mPvpState = 0x26;
            else if (type == EntityType::CREEPER)
                mPvpState = 0x23;
            else if (type == EntityType::SNOW_MAN)
                mPvpState = 0x3c;
            else if (type == EntityType::VILLAGER_GOLEM)
                mPvpState = 0x3b;
            else if (type == EntityType::WOLF)
                mPvpState = 0x24;
            else if (type == EntityType::SERVER_PLAYER || type == EntityType::PLAYER)
            {
                mCauseOfDeathId = 0;
                mPvpState = 0x20;
                boost::shared_ptr<Player> attackerPlayer = boost::static_pointer_cast<Player>(attacker);
                if (attackerPlayer != NULL && !attackerPlayer->getMainHandItem()->isEmpty())
                    mCauseOfDeathId = Item::getId(attackerPlayer->getMainHandItem()->getItem());
            }
        }
    }

    source->sendDamageIndicatorUpdate(boost::static_pointer_cast<ServerPlayer>(shared_from_this()));
    return hurt;
}

bool ServerPlayer::isPvpAllowed()
{
    return mServer->isPvpAllowed();
}

uint32_t ServerPlayer::canHarmPlayer(boost::shared_ptr<Player> wstring)
{
    if (!isPvpAllowed())
        return 0;
    if (!isAllowedToAttackPlayers())
        return 0;
    return Player::canHarmPlayer(wstring);
}

bool ServerPlayer::canHarmPlayer(std::wstring wstring)
{
    bool canHarm = Player::canHarmPlayer(wstring);
    boost::shared_ptr<ServerPlayer> target = mServer->getPlayers()->getPlayerByName(wstring);
    if (target != NULL)
        canHarm = canHarm && canHarmPlayer(boost::shared_ptr<Player>(target));
    return canHarm;
}

void ServerPlayer::addEffect(MobEffectInstance* effectInstance, const boost::shared_ptr<Entity>& source)
{
    if (effectInstance != NULL && effectInstance->getEffect() == MobEffects::LEVITATION)
    {
        if (source && eTYPE_DERIVED_FROM(EntityType::SHULKER_BULLET, source->GetType()))
        {
            if (mLevitationSourceY == NULL)
                mLevitationSourceY = new double(mY);
        }
        else
        {
            delete mLevitationSourceY;
            mLevitationSourceY = NULL;
        }
    }
    boost::shared_ptr<Entity> emptySource;
    LivingEntity::addEffect(effectInstance, emptySource);
}

boost::shared_ptr<Entity> ServerPlayer::changeDimension(int dimension)
{
    if (mChangingDimension != 0)
        return boost::shared_ptr<Entity>();

    mChangingDimension = 1;

    if (mSleeping != 0)
        stopSleepInBed(true, false, false);

    if (mDimension == 1 && dimension == 1)
    {
        awardStat(GenericStats::winGame(), GenericStats::param_winGame());
        INetworkPlayer* networkPlayer = mConnection->getNetworkPlayer();
        if (!mRespawnState)
        {
            mLevel->removeEntity(shared_from_this());
            mRespawnState = true;
            mWonGame = true;
            mConnection->send(boost::shared_ptr<ClientboundGameEventPacket>(new ClientboundGameEventPacket(
                ClientboundGameEventPacket::WIN_GAME, static_cast<float>(networkPlayer->GetUserIndex()))));
        }

        if (networkPlayer != NULL)
        {
            for (std::vector<boost::shared_ptr<ServerPlayer> >::iterator it =
                     gMinecraftServer->getPlayers()->getPlayers()->begin();
                 it != gMinecraftServer->getPlayers()->getPlayers()->end(); ++it)
            {
                boost::shared_ptr<ServerPlayer> player = *it;
                INetworkPlayer* otherNetworkPlayer = player->mConnection->getNetworkPlayer();
                if (networkPlayer != otherNetworkPlayer && otherNetworkPlayer != NULL
                    && networkPlayer->IsSameSystem(otherNetworkPlayer) && !player->mRespawnState)
                {
                    player->mRespawnState = true;
                    player->mConnection->send(
                        boost::shared_ptr<ClientboundGameEventPacket>(new ClientboundGameEventPacket(
                            ClientboundGameEventPacket::WIN_GAME, static_cast<float>(networkPlayer->GetUserIndex()))));
                }
            }
        }
        return shared_from_this();
    }

    if (mDimension == 0 && dimension == 1)
    {
        awardStat(GenericStats::theEnd(), GenericStats::param_theEnd());
        BlockPos* spawn = mServer->getLevel(1)->getDimensionSpecificSpawn();
        if (spawn != NULL)
        {
            mConnection->teleport(static_cast<double>(spawn->x), static_cast<double>(spawn->y),
                                  static_cast<double>(spawn->z), 0.0f, 0.0f, true, true);
        }
    }

    PlayerList* players = gMinecraftServer == NULL ? NULL : gMinecraftServer->mPlayers;
    players->toggleDimension(boost::dynamic_pointer_cast<ServerPlayer>(shared_from_this()), dimension);

    mConnection->send(boost::shared_ptr<ClientboundLevelEventPacket>(
        new ClientboundLevelEventPacket(LevelEvent::SOUND_PORTAL_TRAVEL, BlockPos::ZERO, 0, false)));

    mLastSentHealth = -1.0f;
    mLastSentFood = -1;
    mLastSentTotalExperience = -1;

    return shared_from_this();
}

bool ServerPlayer::broadcastToPlayer(boost::shared_ptr<ServerPlayer> player)
{
    if (player->isSpectator())
    {
        return GetCamera() == shared_from_this();
    }
    if (isSpectator())
    {
        return false;
    }
    return Entity::broadcastToPlayer(player);
}

void ServerPlayer::take(boost::shared_ptr<Entity> entity, int32_t count)
{
    LivingEntity::take(entity, count);
    mContainerMenu->broadcastChanges();
}

uint32_t ServerPlayer::startSleepInBed(const BlockPos& position, bool force)
{
    const uint32_t result = Player::startSleepInBed(position, force);
    if (result != BED_SLEEPING_OK)
        return result;

    boost::shared_ptr<Packet> packet = boost::shared_ptr<ClientboundPlayerSleepPacket>(
        new ClientboundPlayerSleepPacket(boost::dynamic_pointer_cast<Player>(shared_from_this()), position));
    getLevel()->getTracker()->broadcast(shared_from_this(), packet);
    mConnection->teleport(mX, mY, mZ, mYRot, mXRot, true, true);
    mConnection->send(packet);
    return result;
}

void ServerPlayer::stopSleepInBed(bool resetTimer, bool updateLevel, bool setSpawn)
{
    if (isSleeping())
    {
        getLevel()->getTracker()->broadcastAndSend(
            shared_from_this(),
            boost::shared_ptr<ClientboundAnimatePacket>(new ClientboundAnimatePacket(shared_from_this(), 2)));
    }

    Player::stopSleepInBed(resetTimer, updateLevel, setSpawn);

    if (mConnection != NULL)
        mConnection->teleport(mX, mY, mZ, mYRot, mXRot, true, true);
}

bool ServerPlayer::startRiding(boost::shared_ptr<Entity> vehicle)
{
    boost::shared_ptr<Entity> vehicleBefore = getVehicle();

    if (!Player::startRiding(vehicle))
        return false;

    boost::shared_ptr<Entity> vehicleAfter = getVehicle();
    if (vehicleAfter.get() != vehicleBefore.get() && mConnection != NULL)
        mConnection->teleport(mX, mY, mZ, mYRot, mXRot, true, true);

    return true;
}

void ServerPlayer::stopRiding()
{
    boost::shared_ptr<Entity> vehicleBefore = getVehicle();
    Player::stopRiding();
    boost::shared_ptr<Entity> vehicleAfter = getVehicle();
    if (vehicleBefore.get() != vehicleAfter.get() && mConnection)
        mConnection->teleport(mX, mY, mZ, mYRot, mXRot, true, true);
}

void ServerPlayer::stopRidingBeforeTeleport()
{
    boost::shared_ptr<Entity> vehicle = getVehicle();
    Player::stopRiding();
    fjImmediateBroadcastPassengerPacket(vehicle);
}

uint8_t ServerPlayer::isChangingDimension()
{
    return mChangingDimension;
}

bool ServerPlayer::isInvulnerableTo(const DamageSource* source)
{
    if (Minecraft::InMiniGame(eMiniGameId_Glide, true)
        && ((source->isFire() && !Entity::isInLava()) || !TakeGlideCollisionDamage()))
    {
        return true;
    }
    bool result = true;
    if (!Entity::isInvulnerableTo(source) && !isChangingDimension())
    {
        result = false;
    }
    return result;
}

void ServerPlayer::checkFallDamage(double, bool, Block*, const BlockPos&)
{
}

void ServerPlayer::doCheckFallDamage(double yMovement, bool onGround)
{
    int x = Mth::floor(mX);
    int y = Mth::floor(mY - 0.2f);
    int z = Mth::floor(mZ);
    BlockPos pos(x, y, z);
    if (!onGround)
    {
        if (mFallStartPosition == NULL)
            mFallStartPosition = new BlockPos(pos);
    }
    else if (mFallStartPosition != NULL)
    {
        static_cast<ServerPlayerGameMode*>(GetGameMode())->RecordLanding(pos);
        delete mFallStartPosition;
        mFallStartPosition = NULL;
    }
    Block* block = mLevel->getBlock(pos);
    if (block->mMaterial == Material::AIR)
    {
        BlockPos belowPos = pos.below();
        Block* belowBlock = mLevel->getBlock(belowPos);
        if (belowBlock != NULL
            && (dynamic_cast<FenceBlock*>(belowBlock) != NULL || dynamic_cast<WallBlock*>(belowBlock) != NULL
                || dynamic_cast<FenceGateBlock*>(belowBlock) != NULL))
        {
            pos = belowPos;
            block = belowBlock;
        }
    }
    LivingEntity::checkFallDamage(yMovement, onGround, block, pos);
}

void ServerPlayer::nextContainerCounter()
{
    mContainerCounter = mContainerCounter % 100 + 1;
}

void ServerPlayer::openTextEdit(boost::shared_ptr<SignBlockEntity> sign)
{
    boost::shared_ptr<SignBlockEntity> signEntity = boost::dynamic_pointer_cast<SignBlockEntity>(sign);
    if (signEntity == NULL)
        return;

    signEntity->setCreatedPlayer(getUUID());
    signEntity->setCreatedByHostPlayer(false);

    for (int i = 0; i < 4; ++i)
    {
        boost::shared_ptr<MultiplayerLocalPlayer> localPlayer = Minecraft::GetInstance()->mLocalPlayers[i];
        if (localPlayer != NULL && getUUID() == localPlayer->getUUID())
        {
            signEntity->setCreatedByHostPlayer(true);
            break;
        }
    }

    mConnection->send(boost::shared_ptr<Packet>(new ClientboundOpenSignEditorPacket(sign->getBlockPos())));
}

int ServerPlayer::startBlockInteraction(InteractionObject& interaction)
{
    if (mContainerMenu == mInventoryMenu)
    {
        nextContainerCounter();

        mConnection->send(boost::shared_ptr<Packet>(new ClientboundContainerOpenPacket(
            mContainerCounter, interaction.getTypeId(false), interaction.GetNetworkNameData())));

        mContainerMenu = interaction.createMenu(mInventory, boost::dynamic_pointer_cast<Player>(shared_from_this()));
        mContainerMenu->m_containerId = mContainerCounter;
        mContainerMenu->addSlotListener(this);
    }
    return 1;
}

int ServerPlayer::openFireworks(int32_t x, int32_t y, int32_t z)
{
    if (mContainerMenu == mInventoryMenu)
        return 1;

    if (mContainerMenu == NULL)
        return 1;

    if (dynamic_cast<CraftingMenu*>(mContainerMenu) == NULL)
        return 1;

    closeContainer();
    nextContainerCounter();

    InteractionObject::NameData nameData(std::wstring(L""), std::wstring(L""), true, false);
    mConnection->send(boost::shared_ptr<Packet>(new ClientboundContainerOpenPacket(
        mContainerCounter, ClientboundContainerOpenPacket::FIREWORKS, nameData, 9, 0)));

    mContainerMenu = new FireworksMenu(mInventory, mLevel, BlockPos(x, y, z));
    mContainerMenu->m_containerId = mContainerCounter;
    mContainerMenu->addSlotListener(this);
    return 1;
}

int ServerPlayer::openContainer(boost::shared_ptr<Container> container)
{
    if (mContainerMenu != mInventoryMenu)
        return 1;

    boost::shared_ptr<LockableContainer> lockable = boost::dynamic_pointer_cast<LockableContainer>(container);
    if (lockable != NULL && lockable->isLocked() && !canUnlock(lockable->getLockKeyCode()) && !isSpectator())
    {
        boost::shared_ptr<ClientboundChatPacket> chatPacket(new ClientboundChatPacket());
        chatPacket->message = ClientboundChatPacket::eChatPacketMessage_ContainerLocked;
        chatPacket->strings.push_back(container->getDisplayName());
        mConnection->send(chatPacket);
        mConnection->send(boost::shared_ptr<Packet>(new ClientboundSoundPacket(
            SoundEvents::CHEST_LOCKED, SoundSource::eSoundSource_Blocks, mX, mY, mZ, 1.0f, 1.0f)));
        return 0;
    }

    nextContainerCounter();

    boost::shared_ptr<InteractionObject> interaction = boost::dynamic_pointer_cast<InteractionObject>(container);
    if (interaction != NULL)
    {
        mConnection->send(boost::shared_ptr<Packet>(
            new ClientboundContainerOpenPacket(mContainerCounter, interaction->getTypeId(false),
                                               interaction->GetNetworkNameData(), container->getContainerSize())));

        mContainerMenu = interaction->createMenu(mInventory, boost::static_pointer_cast<Player>(shared_from_this()));
    }
    else
    {
        boost::shared_ptr<PlayerEnderChestContainer> enderChest =
            boost::dynamic_pointer_cast<PlayerEnderChestContainer>(container);
        if (enderChest != NULL)
        {
            mConnection->send(boost::shared_ptr<Packet>(
                new ClientboundContainerOpenPacket(mContainerCounter, ClientboundContainerOpenPacket::ENDER_CHEST,
                                                   enderChest->GetNetworkNameData(), container->getContainerSize())));
        }
        else
        {
            InteractionObject::NameData nameData(container->getDisplayName(), std::wstring(L""), true, false);
            mConnection->send(boost::shared_ptr<Packet>(
                new ClientboundContainerOpenPacket(mContainerCounter, ClientboundContainerOpenPacket::CONTAINER,
                                                   nameData, container->getContainerSize())));
        }

        mContainerMenu = new ChestMenu(mInventory, container, boost::static_pointer_cast<Player>(shared_from_this()));
    }

    mContainerMenu->m_containerId = mContainerCounter;
    mContainerMenu->addSlotListener(this);
    return 1;
}

int ServerPlayer::openTrading(boost::shared_ptr<Merchant> merchant, const std::wstring& name, int entityId)
{
    if (mContainerMenu != mInventoryMenu)
        return 1;

    nextContainerCounter();

    mContainerMenu = new MerchantMenu(mInventory, merchant, mLevel);
    mContainerMenu->m_containerId = mContainerCounter;
    mContainerMenu->addSlotListener(this);

    boost::shared_ptr<Container> tradeContainer = static_cast<MerchantMenu*>(mContainerMenu)->getTradeContainer();

    InteractionObject::NameData nameData(name.empty() ? L"" : name, L"", true, false);

    boost::shared_ptr<Entity> entity = mLevel->getEntity(entityId);
    if (entity != NULL)
    {
        nameData = InteractionObject::NameData(name.empty() ? L"" : name, entity->getOwnerUUID(),
                                               entity->wasNamedOnHost(), entity->wasNamedByRestrictedPlayer());
    }

    mConnection->send(boost::shared_ptr<Packet>(
        new ClientboundContainerOpenPacket(mContainerCounter, ClientboundContainerOpenPacket::TRADER_NPC, nameData,
                                           tradeContainer->getContainerSize(), entityId)));

    sendTradeOffers();
    return 1;
}

void ServerPlayer::sendTradeOffers()
{
    auto* menu = dynamic_cast<MerchantMenu*>(mContainerMenu);
    if (!menu)
        return;

    MerchantRecipeList* offers = menu->getMerchant()->getOffers(boost::dynamic_pointer_cast<Player>(shared_from_this()));
    if (offers != NULL)
    {
        ByteArrayOutputStream bytes;
        DataOutputStream output(&bytes);
        output.writeInt(menu->m_containerId);
        offers->writeToStream(&output);
        mConnection->send(boost::shared_ptr<Packet>(
            new ClientboundCustomPayloadPacket(std::wstring(L"MC|TrList"), bytes.toByteArray())));
    }

}

int ServerPlayer::openHorseInventory(boost::shared_ptr<AbstractHorse> horse, boost::shared_ptr<Container> container)
{
    if (mContainerMenu != mInventoryMenu)
        closeContainer();

    nextContainerCounter();

    InteractionObject::NameData nameData(horse->getCustomName(), horse->getOwnerUUID(), horse->wasNamedOnHost(),
                                         horse->wasNamedByRestrictedPlayer());
    mConnection->send(boost::shared_ptr<Packet>(
        new ClientboundContainerOpenPacket(mContainerCounter, ClientboundContainerOpenPacket::HORSE, nameData,
                                           container->getContainerSize(), horse->getId())));

    mContainerMenu = new HorseInventoryMenu(mInventory, container, horse,
                                            boost::dynamic_pointer_cast<Player>(shared_from_this()));
    mContainerMenu->m_containerId = mContainerCounter;
    mContainerMenu->addSlotListener(this);
    return 1;
}

void ServerPlayer::openItemInstanceGui(not_null_ptr<ItemInstance> item, InteractionHand::EInteractionHand hand)
{
    if (item->getItem() != Items::writtenBook)
        return;

    ByteArrayOutputStream bytes;
    DataOutputStream output(&bytes);
    output.write(static_cast<unsigned int>(hand));
    mConnection->send(
        boost::shared_ptr<Packet>(new ClientboundCustomPayloadPacket(std::wstring(L"MC|BOpen"), bytes.toByteArray())));
}

void ServerPlayer::doCloseContainer()
{
    mContainerMenu->removed(boost::static_pointer_cast<Player>(shared_from_this()));

    if (mContainerMenu != mInventoryMenu)
        delete mContainerMenu;

    mContainerMenu = mInventoryMenu;
    refreshContainer(mContainerMenu);
}

void ServerPlayer::closeContainer()
{
    mConnection->send(boost::shared_ptr<Packet>(new ClientboundContainerClosePacket(mContainerMenu->m_containerId)));
    doCloseContainer();
}

void ServerPlayer::broadcastCarriedItem()
{
    if (!mIsChangingQuantityOnly)
        mConnection->send(
            boost::shared_ptr<Packet>(new ClientboundContainerSetSlotPacket(-1, -1, mInventory->getCarried())));
}

void ServerPlayer::setPlayerInput(float xxa, float zza, bool jumping, bool sneaking)
{
    if (isPassenger())
    {
        if (xxa >= -1.0f && xxa <= 1.0f)
            mXxa = xxa;
        if (zza >= -1.0f && zza <= 1.0f)
            mZza = zza;
        mJumping = jumping;
        setSneaking(sneaking);
    }
}

void ServerPlayer::awardStat(Stat* stat, arrayWithLength<unsigned char> parameters)
{
    if (stat != NULL && parameters.data != NULL && parameters.length >= sizeof(int))
    {
        int amount;
        std::memcpy(&amount, parameters.data, sizeof(amount));
        if (amount > 0)
            LeaderboardSavedData::onAward(this, stat, static_cast<unsigned int>(amount));
    }
    if (stat != NULL && stat->isAwardedFromServer() && CanRecordStats(stat, parameters))
    {
        int value = *(int*)parameters.data;
        delete[] parameters.data;
        mConnection->send(boost::shared_ptr<Packet>(new ClientboundAwardStatPacket(stat->id, value)));
        return;
    }

    delete[] parameters.data;
}

void ServerPlayer::resetStat(Stat* stat)
{
}

uint8_t ServerPlayer::hasDisconnected()
{
    return mDisconnected;
}

void ServerPlayer::displayClientMessage(int32_t message, bool force)
{
    std::wstring name = getName();
    ClientboundChatPacket::EChatPacketMessage packetMessage = ClientboundChatPacket::eChatPacketMessage_Text;
    enum
    {
        eRecipients_None,
        eRecipients_Self,
        eRecipients_Others,
        eRecipients_All
    } recipients = eRecipients_None;

    switch (static_cast<uint32_t>(message))
    {
    case IDS_TILE_BED_PLAYERSLEEP:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedPlayerSleep;
        for (unsigned int i = 0; i < mServer->getPlayers()->getPlayers()->size(); ++i)
        {
            boost::shared_ptr<ServerPlayer> player = (*mServer->getPlayers()->getPlayers())[i];
            if (shared_from_this() == player)
                player->mConnection->send(boost::shared_ptr<ClientboundChatPacket>(
                    new ClientboundChatPacket(name, ClientboundChatPacket::eChatPacketMessage_BedMeSleep, -1)));
            else
                player->mConnection->send(
                    boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(name, packetMessage, -1)));
        }
        break;
    case IDS_TILE_BED_OCCUPIED:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedOccupied;
        mConnection->send(boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(L"", packetMessage, -1)));
        break;
    case IDS_TILE_BED_NO_SLEEP:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedNoSleep;
        mConnection->send(boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(L"", packetMessage, -1)));
        break;
    case IDS_TILE_BED_NOT_VALID:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedNotValid;
        mConnection->send(boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(L"", packetMessage, -1)));
        break;
    case IDS_TILE_BED_NOTSAFE:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedNotSafe;
        mConnection->send(boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(L"", packetMessage, -1)));
        break;
    case IDS_TILE_BED_TOO_FAR_AWAY:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedTooFarAway;
        mConnection->send(boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(L"", packetMessage, -1)));
        break;
    case IDS_TILE_BED_MESLEEP:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_BedMeSleep;
        mConnection->send(boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(L"", packetMessage, -1)));
        break;
    case IDS_PLAYER_ENTERED_END:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_PlayerEnteredEnd;
        recipients = eRecipients_All;
        break;
    case IDS_PLAYER_LEFT_END:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_PlayerLeftEnd;
        recipients = eRecipients_All;
        break;
    case 0x9F8BFFAE:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxAnimals;
        recipients = eRecipients_All;
        break;
    case 0x82460728:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxChickens;
        recipients = eRecipients_All;
        break;
    case 0x1E0D17D0:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxSquid;
        recipients = eRecipients_All;
        break;
    case 0xFAB1DEB6:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBats;
        recipients = eRecipients_All;
        break;
    case 0x70EDC806:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxWolves;
        recipients = eRecipients_All;
        break;
    case 0x3E35B6E8:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxMooshrooms;
        recipients = eRecipients_All;
        break;
    case 0xA82B5A66:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxEnemies;
        recipients = eRecipients_All;
        break;
    case 0x16972FBF:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxVillagers;
        recipients = eRecipients_All;
        break;
    case 0xB399BD15:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBredAnimals;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_CHICKENS_BRED:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBredChickens;
        recipients = eRecipients_All;
        break;
    case 0x00FE5875:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBredMooshrooms;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_WOLVES_BRED:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBredWolves;
        recipients = eRecipients_All;
        break;
    case IDS_CANT_SHEAR_MOOSHROOM:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_CantShearMooshroom;
        recipients = eRecipients_All;
        break;
    case 0x1EE681BA:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxHangingEntities;
        recipients = eRecipients_All;
        break;
    case 0x8ECD701E:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxArmorStands;
        recipients = eRecipients_All;
        break;
    case IDS_CANT_SPAWN_IN_PEACEFUL:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_CantSpawnInPeaceful;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_BOATS:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBoats;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_MINECARTS:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxMinecarts;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_BANNERS:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBanners;
        recipients = eRecipients_All;
        break;
    case 0xB714E0E2:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxDolphins;
        recipients = eRecipients_All;
        break;
    case 0x3DCD926B:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxPhantoms;
        recipients = eRecipients_All;
        break;
    case 0x60A3835D:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxFishAndSquid;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_ENDER_CRYSTALS:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxEnderCrystals;
        recipients = eRecipients_All;
        break;
    case IDS_MAX_BEACONS:
        packetMessage = ClientboundChatPacket::eChatPacketMessage_MaxBeacons;
        recipients = eRecipients_All;
        break;
    }

    switch (recipients)
    {
    case eRecipients_Self:
    {
        std::vector<boost::shared_ptr<ServerPlayer> >* players = mServer->getPlayers()->getPlayers();
        const unsigned int count = players->size();
        for (unsigned int i = 0; i < count; ++i)
        {
            boost::shared_ptr<ServerPlayer> player = players->at(i);
            if (shared_from_this() == player)
                player->mConnection->send(
                    boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(name, packetMessage, -1)));
        }
        break;
    }
    case eRecipients_Others:
    {
        std::vector<boost::shared_ptr<ServerPlayer> >* players = mServer->getPlayers()->getPlayers();
        const unsigned int count = players->size();
        for (unsigned int i = 0; i < count; ++i)
        {
            boost::shared_ptr<ServerPlayer> player = players->at(i);
            if (shared_from_this() != player)
                player->mConnection->send(
                    boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(name, packetMessage, -1)));
        }
        break;
    }
    case eRecipients_All:
    {
        std::vector<boost::shared_ptr<ServerPlayer> >* players = mServer->getPlayers()->getPlayers();
        const unsigned int count = players->size();
        for (unsigned int i = 0; i < count; ++i)
        {
            boost::shared_ptr<ServerPlayer> player = players->at(i);
            player->mConnection->send(
                boost::shared_ptr<ClientboundChatPacket>(new ClientboundChatPacket(name, packetMessage, -1)));
        }
        break;
    }
    }
}

void ServerPlayer::completeUsingItem()
{
    if (mUseItem->isEmpty())
        return;

    if (!isUsingItem())
        return;

    mConnection->send(boost::shared_ptr<ClientboundEntityEventPacket>(
        new ClientboundEntityEventPacket(shared_from_this(), 9, 0)));

    LivingEntity::completeUsingItem();
}

void ServerPlayer::restoreFrom(boost::shared_ptr<Player> oldPlayer, bool keepEverything)
{
    if (keepEverything)
    {
        mInventory->replaceWith(oldPlayer->mInventory);
        setHealth(oldPlayer->getHealth());
        mFoodData = oldPlayer->mFoodData;
        mExperienceLevel = oldPlayer->mExperienceLevel;
        mTotalExperience = oldPlayer->mTotalExperience;
        mExperienceProgress = oldPlayer->mExperienceProgress;
        setScore(static_cast<int32_t>(oldPlayer->getScore()));
    }
    else if (mLevel->getGameRules()->getBoolean(GameRules::RULE_KEEPINVENTORY) || oldPlayer->isSpectator())
    {
        mInventory->replaceWith(oldPlayer->mInventory);
        mExperienceLevel = oldPlayer->mExperienceLevel;
        mTotalExperience = oldPlayer->mTotalExperience;
        mExperienceProgress = oldPlayer->mExperienceProgress;
        setScore(static_cast<int32_t>(oldPlayer->getScore()));
    }

    mEnchantmentSeed = oldPlayer->mEnchantmentSeed;


    mEnderChestInventory = oldPlayer->mEnderChestInventory;

    getEntityData()->set(DATA_PLAYER_MODE_CUSTOMISATION,
                         oldPlayer->getEntityData()->get(DATA_PLAYER_MODE_CUSTOMISATION), true);

    mLastSentHealth = -1.0f;
    mLastSentFood = -1;
    mLastSentTotalExperience = -1;

    const boost::shared_ptr<ServerPlayer> oldServerPlayer = boost::dynamic_pointer_cast<ServerPlayer>(oldPlayer);
    mTu80OceanBiomes = oldServerPlayer->mTu80OceanBiomes;
    mTu80DiscoveredStructures = oldServerPlayer->mTu80DiscoveredStructures;
    mEntitiesToRemove = oldServerPlayer->mEntitiesToRemove;

    setShoulderEntityLeft(oldPlayer->getShoulderEntityLeft()->copy());
    setShoulderEntityRight(oldPlayer->getShoulderEntityRight()->copy());

    mGameMode->RestoreFromOld(oldServerPlayer->mGameMode);
}

void ServerPlayer::onEffectAdded(MobEffectInstance* effect)
{
    LivingEntity::onEffectAdded(effect);
    mConnection->send(
        boost::shared_ptr<ClientboundUpdateMobEffectPacket>(new ClientboundUpdateMobEffectPacket(getId(), effect)));
}

void ServerPlayer::onEffectUpdated(MobEffectInstance* effect, bool attributesChanged)
{
    LivingEntity::onEffectUpdated(effect, attributesChanged);
    mConnection->send(
        boost::shared_ptr<ClientboundUpdateMobEffectPacket>(new ClientboundUpdateMobEffectPacket(getId(), effect)));
}

void ServerPlayer::onEffectRemoved(MobEffectInstance* effect)
{
    LivingEntity::onEffectRemoved(effect);
    mConnection->send(boost::shared_ptr<ClientboundRemoveMobEffectPacket>(
        new ClientboundRemoveMobEffectPacket(getId(), effect->getEffect())));
}

void ServerPlayer::teleportTo(double x, double y, double z)
{
    mConnection->teleport(x, y, z, mYRot, mXRot, true, false);
}

void ServerPlayer::teleportTo(double x, double y, double z, float yRot, float xRot)
{
    mConnection->teleport(x, y, z, xRot, yRot, true, true);
}

void ServerPlayer::crit(boost::shared_ptr<Entity> entity)
{
    getLevel()->getTracker()->broadcastAndSend(
        shared_from_this(), boost::shared_ptr<ClientboundAnimatePacket>(new ClientboundAnimatePacket(entity, 4)));
}

void ServerPlayer::magicCrit(boost::shared_ptr<Entity> entity)
{
    getLevel()->getTracker()->broadcastAndSend(
        shared_from_this(), boost::shared_ptr<ClientboundAnimatePacket>(new ClientboundAnimatePacket(entity, 5)));
}

void ServerPlayer::onUpdateAbilities()
{
    if (mConnection != NULL)
    {
        mConnection->send(boost::shared_ptr<ClientboundPlayerAbilitiesPacket>(
            new ClientboundPlayerAbilitiesPacket(&mAbilities, getId())));
        updateInvisibilityStatus();
    }
}

void ServerPlayer::setGameMode(const GameType* gameType)
{
    mGameMode->SetGameType(gameType);
    setPlayerGamePrivilege(Player::ePlayerGamePrivilege_CreativeMode, mGameMode->isCreative());
    if (gameType == GameType::SPECTATOR)
    {
        removeEntitiesOnShoulder();
        stopRiding();
    }
    updateEffectVisibility();
}

bool ServerPlayer::isSpectator()
{
    if (mGameMode != NULL)
        return mGameMode->GetGameType() == GameType::SPECTATOR;
    return false;
}

bool ServerPlayer::isCreative()
{
    if (mGameMode != NULL)
        return mGameMode->GetGameType() == GameType::CREATIVE;
    return false;
}

void ServerPlayer::resetLastActionTime()
{
    mLastActionTime = MinecraftServer::getCurrentTimeMillis();
}

void ServerPlayer::sendRemoveEntity(boost::shared_ptr<Entity> entity)
{
    if (eTYPE_DERIVED_FROM(EntityType::PLAYER, entity->GetType()))
        mConnection->send(boost::shared_ptr<ClientboundRemoveEntitiesPacket>(
            new ClientboundRemoveEntitiesPacket(entity->getId())));
    else
        mEntitiesToRemove.push_back(entity->getId());
}

void ServerPlayer::cancelRemoveEntity(boost::shared_ptr<Entity> entity)
{
    std::vector<int32_t>::iterator it = mEntitiesToRemove.begin();
    std::vector<int32_t>::iterator end = mEntitiesToRemove.end();
    const int32_t id = entity->getId();
    for (; it != end; ++it)
    {
        if (*it == id)
            break;
    }
    if (it != mEntitiesToRemove.end())
        mEntitiesToRemove.erase(it);
}

void ServerPlayer::updateInvisibilityStatus()
{
    LivingEntity::updateInvisibilityStatus();
    getLevel()->getTracker()->updateVisibility(boost::dynamic_pointer_cast<ServerPlayer>(shared_from_this()));
}

void ServerPlayer::processDimensionDelay()
{
    if (mPortalCooldown < 1 || mChangingDimension != 0)
        return;
    --mPortalCooldown;
}

void ServerPlayer::attack(boost::shared_ptr<Entity> entity)
{
    if (mGameMode->GetGameType() == GameType::SPECTATOR)
        return;
    Player::attack(entity);
}

bool ServerPlayer::swing(InteractionHand::EInteractionHand hand)
{
    bool swung = LivingEntity::swing(hand);
    resetAttackStrengthTicker();
    return swung;
}

void ServerPlayer::hasChangedDimension()
{
    mChangingDimension = 0;
}

void ServerPlayer::startFallFlying()
{
    setSharedFlag(FLAG_FALL_FLYING, true);
}

void ServerPlayer::stopFallFlying()
{
    setSharedFlag(FLAG_FALL_FLYING, true);
    setSharedFlag(FLAG_FALL_FLYING, false);
}

uint32_t ServerPlayer::getPlayerViewDistanceModifier()
{
    int modifier = 0;
    if (!mConnection->isLocal())
    {
        INetworkPlayer* networkPlayer = mConnection->getNetworkPlayer();
        if (networkPlayer != NULL)
        {
            uint32_t rtt = networkPlayer->GetCurrentRtt();
            modifier = rtt >> 6;
            if (modifier > 4)
                modifier = 4;
        }
    }
    return modifier;
}

void ServerPlayer::handleCollectItem(not_null_ptr<ItemInstance> item)
{
    ServerPlayerGameMode* gameMode = mGameMode;
    if (gameMode->mGameRules != NULL)
        gameMode->mGameRules->onCollectItem(item);
}

void ServerPlayer::SetCamera(boost::shared_ptr<Entity> entity)
{
    Player::SetCamera(entity);
    if (isSpectator() && IsSpectatingOtherPlayer() && mCamera)
    {
        teleportTo(mCamera->mX, mCamera->mY, mCamera->mZ, mCamera->mXRot, mCamera->mYRot);
    }
}

const GameType* ServerPlayer::GetGameType()
{
    return ((GameMode*)mGameMode)->GetGameType();
}

GameMode* ServerPlayer::GetGameMode()
{
    return mGameMode;
}

void ServerPlayer::OnEquipArmor(not_null_ptr<ItemInstance> item)
{
    const SoundEvent* sound = SoundEvents::ARMOR_EQUIP_GENERIC;
    Item* heldItem = item->getItem();
    ArmorItem* armorItem = dynamic_cast<ArmorItem*>(heldItem);
    if (armorItem != NULL)
    {
        sound = armorItem->getMaterial()->getEquipSound();
    }
    else if (heldItem == Items::elytra)
    {
        sound = SoundEvents::ARMOR_EQUIP_LEATHER;
    }

    mConnection->send(boost::shared_ptr<ClientboundSoundPacket>(
        new ClientboundSoundPacket(sound, SoundSource::eSoundSource_Neutral, mX, mY, mZ, 1.0f, 1.0f)));
}

bool ServerPlayer::checkTotemDeathProtection(const DamageSource* source)
{
    if (!LivingEntity::checkTotemDeathProtection(source))
        return false;

    Stat* totemUsedStat = gStats->get_itemsUsed(Item::VALIDgetId(Items::totemOfUndying));
    awardStat(totemUsedStat,
              GenericStats::param_itemsUsed(boost::static_pointer_cast<Player>(shared_from_this()),
                                            not_null_ptr<ItemInstance>(new ItemInstance(Items::totemOfUndying))));
    return true;
}

unsigned long long ServerPlayer::GetType()
{
    return EntityType::SERVER_PLAYER;
}
