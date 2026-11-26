#include "game/server/ServerChunkCache.hpp"
#include "game/server/ServerLevel.hpp"
#include "game/server/MinecraftServer.hpp"
#include "game/network/ProgressListener.hpp"
#include "game/world/Dimension.hpp"
#include "game/world/chunk/ChunkGenerator.hpp"
#include "game/world/chunk/ChunkStorage.hpp"
#include "game/world/chunk/EmptyLevelChunk.hpp"
#include "game/world/chunk/LevelChunk.hpp"
#include "game/world/chunk/OldChunkStorage.hpp"
#include "game/core/Compression.hpp"
#include "game/core/PIXNamedEvents.hpp"
#include "platform/C4JEvent.hpp"
#include "platform/Platform.hpp"
#include "game/core/Mth.hpp"
#include "game/core/System.hpp"
#include "game/core/VideoSettings.hpp"
#include "game/world/chunk/ClientChunkCache.hpp"
#include "game/world/generation/Player.hpp"
#include <algorithm>
#include <cstring>

extern long InterlockedCompareExchangeRelease(volatile long* destination, long exchange, long comparand);

#if LCE_PLATFORM_PC
static LevelChunk* InterlockedCompareExchangeChunkRelease(LevelChunk* volatile* destination, LevelChunk* exchange,
                                                          LevelChunk* comparand)
{
    __atomic_compare_exchange_n(destination, &comparand, exchange, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    return comparand;
}
#endif

static const int MAX_DROPS_PER_TICK = 100;
static const int SAVE_PROGRESS_INTERVAL = 10;

struct ServerChunkCacheSaveThreadData
{
    ServerChunkCache* cache;
    LevelChunk* chunk;
    bool saveEntities;
    bool useSharedThreadStorage;
    C4JEvent* notificationEvent;
    C4JEvent* wakeEvent;
};

ServerChunkCache::ServerChunkCache(ServerLevel* level, ChunkStorage* storage, ChunkGenerator* source)
{
    m_xzSize = source->mLevelSize;
    mLevelSize = source->mLevelSize;
    m_createOnRequest = false;
    m_xzOffset = m_xzSize / 2;
    m_emptyChunk = new EmptyLevelChunk(level, 0, 0);
    m_storage = storage;
    m_dimension = level->mDimension;
    m_source = source;
    m_level = level;
    m_cache = new LevelChunk*[m_xzSize * m_xzSize];
    std::memset(m_cache, 0, m_xzSize * m_xzSize * sizeof(LevelChunk*));
    m_unloadedCache = new LevelChunk*[m_xzSize * m_xzSize];
    std::memset(m_unloadedCache, 0, m_xzSize * m_xzSize * sizeof(LevelChunk*));
    m_boundFunction = NULL;
    m_boundData = NULL;
    InitializeCriticalSectionAndSpinCount(&m_csLoadCreate, 4000);
}

ServerChunkCache::~ServerChunkCache()
{
    m_storage->WaitForAll();
    delete m_storage;
    delete m_emptyChunk;
    delete m_cache;
    delete m_source;

    for (int i = 0; i < m_xzSize * m_xzSize; ++i)
    {
        delete m_unloadedCache[i];
    }
    delete m_unloadedCache;

    for (std::vector<LevelChunk*>::iterator it = m_loadedChunkList.begin(), end = m_loadedChunkList.end(); it != end;
         ++it)
    {
        delete *it;
    }

    DeleteCriticalSection(&m_csLoadCreate);
}

int ServerChunkCache::computeIdx(int x, int z)
{
    return (x + m_xzOffset) * m_xzSize + (z + m_xzOffset);
}

bool ServerChunkCache::inBounds(int x, int z)
{
    if (m_boundFunction != NULL)
    {
        return m_boundFunction(m_boundData, x, z);
    }
    return m_dimension->containsChunk(x, z);
}

void ServerChunkCache::dontDrop(int x, int z)
{
    LevelChunk* chunk = getChunk(x, z);
    m_toDrop.erase(std::remove(m_toDrop.begin(), m_toDrop.end(), chunk), m_toDrop.end());
}

void ServerChunkCache::drop(int x, int z)
{
    if (inBounds(x, z))
    {
        LevelChunk* chunk = m_cache[computeIdx(x, z)];
        if (chunk != NULL)
        {
            m_toDrop.push_back(chunk);
        }
    }
}

void ServerChunkCache::invalidateChunksOutsideArea(int minX, int minZ, int maxX, int maxZ)
{
    for (int i = 0; i < m_xzSize * m_xzSize; ++i)
    {
        LevelChunk* chunk = m_unloadedCache[i];
        if (chunk != NULL && (chunk->x < minX || chunk->x > maxX || chunk->z < minZ || chunk->z > maxZ))
        {
            chunk->SetInvalid(true);
        }
    }

    std::vector<LevelChunk*>::iterator itEnd = m_loadedChunkList.end();
    for (std::vector<LevelChunk*>::iterator it = m_loadedChunkList.begin(); it != itEnd; ++it)
    {
        LevelChunk* chunk = *it;
        if (chunk->x < minX || chunk->x > maxX || chunk->z < minZ || chunk->z > maxZ)
        {
            chunk->SetInvalid(true);
        }
    }
}

LevelChunk* ServerChunkCache::getChunkIfLoaded(int x, int z)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }
    return m_cache[computeIdx(x, z)];
}

LevelChunk* ServerChunkCache::getChunkIfLoadedOrInvalid(int x, int z)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }
    return m_cache[computeIdx(x, z)];
}

LevelChunk* ServerChunkCache::getChunkIfHasChunk(int x, int z)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }
    LevelChunk* chunk = m_cache[computeIdx(x, z)];
    if (chunk == NULL || chunk->mInvalid)
    {
        return NULL;
    }
    return chunk;
}

LevelChunk* ServerChunkCache::load(int x, int z, LevelChunk* chunk)
{
    ChunkStorage* storage = m_storage;
    if (storage == NULL)
    {
        return NULL;
    }
    int idx = computeIdx(x, z);
    if (chunk == NULL)
    {
        chunk = m_unloadedCache[idx];
        m_unloadedCache[idx] = NULL;
    }
    if (chunk == NULL || chunk->mInvalid)
    {
        chunk = storage->load(m_level, x, z, chunk);
    }
    if (chunk != NULL)
    {
        chunk->setLastSaveTime(m_level->getGameTime());
        if (m_source != NULL)
        {
            m_source->recreateLogicStructuresForChunk(chunk, x, z);
        }
    }
    return chunk;
}

void ServerChunkCache::updatePostProcessFlag(short flag, int x, int z, int dx, int dz, LevelChunk* chunk)
{
    if (hasChunk(x + dx, z + dz))
    {
        LevelChunk* neighbour = getChunk(x + dx, z + dz);
        if (neighbour == m_emptyChunk || (neighbour->mChunkFlags & LevelChunk::TERRAIN_POPULATED_SELF) != 0)
        {
            chunk->mChunkFlags |= flag;
        }
    }
}

void ServerChunkCache::updatePostProcessFlags(int x, int z)
{
    LevelChunk* chunk = getChunk(x, z);
    if (chunk != m_emptyChunk)
    {
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_NORTH, x, z, 0, -1, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_NORTH_WEST, x, z, -1, -1, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_WEST, x, z, -1, 0, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_SOUTH_WEST, x, z, -1, 1, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_SOUTH, x, z, 0, 1, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_SOUTH_EAST, x, z, 1, 1, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_EAST, x, z, 1, 0, chunk);
        updatePostProcessFlag(LevelChunk::TERRAIN_POPULATED_NORTH_EAST, x, z, 1, -1, chunk);

        if ((chunk->mChunkFlags & LevelChunk::TERRAIN_POPULATED_SELF) != 0)
        {
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_WEST, x + 1, z);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_NORTH_WEST, x + 1, z + 1);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_NORTH, x, z + 1);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_NORTH_EAST, x - 1, z + 1);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_EAST, x - 1, z);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_SOUTH_EAST, x - 1, z - 1);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_SOUTH, x, z - 1);
            flagPostProcessComplete(LevelChunk::TERRAIN_POPULATED_SOUTH_WEST, x + 1, z - 1);
        }
    }
    flagPostProcessComplete(0, x, z);
}

LevelChunk* ServerChunkCache::updateCacheAndPostProcess(int x, int z, LevelChunk* chunk, LevelChunk* previous,
                                                        bool allowEmpty)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }

    int idx = computeIdx(x, z);
#if LCE_PLATFORM_WIIU
    if (InterlockedCompareExchangeRelease(reinterpret_cast<volatile long*>(&m_cache[idx]),
                                          reinterpret_cast<long>(chunk), reinterpret_cast<long>(previous))
        == reinterpret_cast<long>(previous))
#elif LCE_PLATFORM_PC
    if (InterlockedCompareExchangeChunkRelease(&m_cache[idx], chunk, previous) == previous)
#endif
    {
        EnterCriticalSection(&m_csLoadCreate);

        PIXBeginNamedEvent(0.0f, "Lighting chunk");
        m_source->lightChunk(chunk);
        PIXEndNamedEvent();

        PIXBeginNamedEvent(0.0f, "Post-processing");
        updatePostProcessFlags(x, z);
        if (chunk != previous)
        {
            m_loadedChunkList.push_back(chunk);
        }

        if (allowEmpty)
        {
            if ((chunk->mChunkFlags & LevelChunk::TERRAIN_POPULATED_SELF) == 0 && hasChunk(x + 1, z + 1)
                && hasChunk(x, z + 1) && hasChunk(x + 1, z))
            {
                gMinecraftServer->addPostProcessRequest(this, m_source, x, z);
            }
            if (hasChunk(x - 1, z) && (getChunk(x - 1, z)->mChunkFlags & LevelChunk::TERRAIN_POPULATED_SELF) == 0
                && hasChunk(x - 1, z + 1) && hasChunk(x, z + 1) && hasChunk(x - 1, z))
            {
                gMinecraftServer->addPostProcessRequest(this, m_source, x - 1, z);
            }
            if (hasChunk(x, z - 1) && (getChunk(x, z - 1)->mChunkFlags & LevelChunk::TERRAIN_POPULATED_SELF) == 0
                && hasChunk(x + 1, z - 1) && hasChunk(x, z - 1) && hasChunk(x + 1, z))
            {
                gMinecraftServer->addPostProcessRequest(this, m_source, x, z - 1);
            }
            if (hasChunk(x - 1, z - 1)
                && (getChunk(x - 1, z - 1)->mChunkFlags & LevelChunk::TERRAIN_POPULATED_SELF) == 0
                && hasChunk(x - 1, z - 1) && hasChunk(x, z - 1) && hasChunk(x - 1, z))
            {
                gMinecraftServer->addPostProcessRequest(this, m_source, x - 1, z - 1);
            }
        }
        else
        {
            chunk->checkPostProcess(this, m_source);
        }
        PIXEndNamedEvent();

        PIXBeginNamedEvent(0.0f, "Checking chests");
        if (hasChunk(x - 1, z) && hasChunk(x - 2, z) && hasChunk(x - 1, z + 1) && hasChunk(x - 1, z - 1))
        {
            chunk->checkChests(this, x - 1, z);
        }
        if (hasChunk(x, z + 1) && hasChunk(x, z + 2) && hasChunk(x - 1, z + 1) && hasChunk(x + 1, z + 1))
        {
            chunk->checkChests(this, x, z + 1);
        }
        if (hasChunk(x + 1, z) && hasChunk(x + 2, z) && hasChunk(x + 1, z + 1) && hasChunk(x + 1, z - 1))
        {
            chunk->checkChests(this, x + 1, z);
        }
        if (hasChunk(x, z - 1) && hasChunk(x, z - 2) && hasChunk(x - 1, z - 1) && hasChunk(x + 1, z - 1))
        {
            chunk->checkChests(this, x, z - 1);
        }
        if (hasChunk(x - 1, z) && hasChunk(x + 1, z) && hasChunk(x, z - 1) && hasChunk(x, z + 1))
        {
            chunk->checkChests(this, x, z);
        }
        PIXEndNamedEvent();

        LeaveCriticalSection(&m_csLoadCreate);
        return chunk;
    }

    chunk->unload(true, true);
    delete chunk;
    return m_cache[idx];
}

LevelChunk* ServerChunkCache::getChunkIfGenerated(int x, int z, bool allowEmpty)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }

    LevelChunk* previous = m_cache[computeIdx(x, z)];
    LevelChunk* chunk = getChunkIfLoadedOrInvalid(x, z);
    if (chunk == NULL || chunk == m_emptyChunk || chunk->mInvalid)
    {
        EnterCriticalSection(&m_csLoadCreate);
        PIXBeginNamedEvent(0.0f, "Loading chunk");
        chunk = load(x, z, chunk);
        PIXEndNamedEvent();
        if (chunk != NULL)
        {
            chunk->load(false);
            LeaveCriticalSection(&m_csLoadCreate);
            chunk = updateCacheAndPostProcess(x, z, chunk, previous, allowEmpty);
        }
        else
        {
            LeaveCriticalSection(&m_csLoadCreate);
            chunk = NULL;
        }
    }
    return chunk;
}

LevelChunk* ServerChunkCache::getOrCreateChunk(int x, int z, bool allowEmpty)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }

    LevelChunk* previous = m_cache[computeIdx(x, z)];
    LevelChunk* chunk = getChunkIfGenerated(x, z, allowEmpty);
    if (chunk == NULL)
    {
        EnterCriticalSection(&m_csLoadCreate);
        PIXBeginNamedEvent(0.0f, "Creating chunk");
        chunk = m_source->createChunk(x, z);
        PIXEndNamedEvent();
        chunk->load(false);
        LeaveCriticalSection(&m_csLoadCreate);
        chunk = updateCacheAndPostProcess(x, z, chunk, previous, allowEmpty);
    }
    return chunk;
}

int ServerChunkCache::getLoadedChunksCount()
{
    return m_loadedChunkList.size();
}

bool ServerChunkCache::hasChunk(int x, int z)
{
    if (inBounds(x, z))
    {
        LevelChunk* chunk = m_cache[computeIdx(x, z)];
        if (chunk != NULL && !chunk->mInvalid)
        {
            return true;
        }
        return false;
    }
    return true;
}

bool ServerChunkCache::isChunkGeneratedAt(int x, int z)
{
    if (!inBounds(x, z))
    {
        return false;
    }
    LevelChunk* chunk = m_cache[computeIdx(x, z)];
    return chunk != NULL && !chunk->isEmpty();
}

std::vector<LevelChunk*>& ServerChunkCache::getLoadedChunkList()
{
    return m_loadedChunkList;
}

LevelChunk* ServerChunkCache::create(int x, int z, bool allowEmpty)
{
    if (!inBounds(x, z))
    {
        return m_emptyChunk;
    }

    LevelChunk* chunk = m_cache[computeIdx(x, z)];
    LevelChunk* lastChunk = chunk;
    if (chunk == NULL || chunk->x != x || chunk->z != z || chunk->mInvalid)
    {
        EnterCriticalSection(&m_csLoadCreate);
        PIXBeginNamedEvent(0.0f, "Loading chunk");
        chunk = load(x, z, chunk);
        PIXEndNamedEvent();
        if (chunk == NULL)
        {
            if (m_source == NULL)
            {
                chunk = m_emptyChunk;
            }
            else
            {
                PIXBeginNamedEvent(0.0f, "Getting chunk from source");
                chunk = m_source->createChunk(x, z);
                PIXEndNamedEvent();
            }
        }
        if (chunk != NULL)
        {
            chunk->load(false);
        }
        LeaveCriticalSection(&m_csLoadCreate);
        chunk = updateCacheAndPostProcess(x, z, chunk, lastChunk, allowEmpty);
    }
    return chunk;
}

LevelChunk* ServerChunkCache::create(int x, int z)
{
    return create(x, z, false);
}

LevelChunk* ServerChunkCache::getChunk(int x, int z)
{
    if (inBounds(x, z))
    {
        LevelChunk* chunk = m_cache[computeIdx(x, z)];
        if (chunk != NULL && !chunk->mInvalid)
        {
            return chunk;
        }
        if (m_level->isFindingSpawn() || m_createOnRequest)
        {
            return create(x, z);
        }
    }
    return m_emptyChunk;
}

LevelChunk* ServerChunkCache::getChunkLoadedOrUnloaded(int x, int z)
{
    if (inBounds(x, z))
    {
        int idx = computeIdx(x, z);
        LevelChunk* chunk = m_cache[idx];
        if (chunk == NULL)
        {
            chunk = m_unloadedCache[idx];
        }
        if (chunk != NULL && !chunk->mInvalid)
        {
            return chunk;
        }
        if (m_level->isFindingSpawn() || m_createOnRequest)
        {
            return create(x, z);
        }
    }
    return m_emptyChunk;
}

void ServerChunkCache::saveEntities(LevelChunk* chunk)
{
    if (m_storage != NULL)
    {
        m_storage->saveEntities(m_level, chunk);
    }
}

void ServerChunkCache::save(LevelChunk* chunk)
{
    if (m_storage != NULL)
    {
        chunk->setLastSaveTime(m_level->getGameTime());
        m_storage->save(m_level, chunk);
    }
}

void ServerChunkCache::flagPostProcessComplete(short flag, int x, int z)
{
    if (hasChunk(x, z))
    {
        LevelChunk* chunk = m_level->getChunk(x, z);
        if (chunk != m_emptyChunk)
        {
            chunk->flagPostProcessComplete(flag);
        }
    }
}

bool ServerChunkCache::saveAllEntities()
{
    PIXBeginNamedEvent(0.0f, "Save all entities");

    PIXBeginNamedEvent(0.0f, "saving to NBT");
    EnterCriticalSection(&m_csLoadCreate);
    for (std::vector<LevelChunk*>::iterator it = m_loadedChunkList.begin(); it != m_loadedChunkList.end(); ++it)
    {
        m_storage->saveEntities(m_level, *it);
    }
    LeaveCriticalSection(&m_csLoadCreate);
    PIXEndNamedEvent();

    PIXBeginNamedEvent(0.0f, "Flushing");
    m_storage->flush();
    PIXEndNamedEvent();

    PIXEndNamedEvent();
    return true;
}

bool ServerChunkCache::save(bool force, ProgressListener* progressListener)
{
    EnterCriticalSection(&m_csLoadCreate);

    int saveTotal = 0;
    if (progressListener != NULL)
    {
        for (std::vector<LevelChunk*>::iterator it = m_loadedChunkList.begin(), end = m_loadedChunkList.end();
             it != end; ++it)
        {
            if ((*it)->shouldSave(force))
            {
                ++saveTotal;
            }
        }
    }

    int saveCount = 0;
    if (!force)
    {
        for (unsigned int i = 0; i < m_loadedChunkList.size(); ++i)
        {
            LevelChunk* chunk = m_loadedChunkList[i];
            if (chunk->shouldSave(false))
            {
                save(chunk);
                chunk->setUnsaved(false);
                LeaveCriticalSection(&m_csLoadCreate);
                return false;
            }
        }
    }
    else
    {
        {
            std::vector<LevelChunk*> sortedChunkList;
            for (unsigned int i = 0; i < m_loadedChunkList.size(); ++i)
            {
                if (m_loadedChunkList[i]->x < 0 && m_loadedChunkList[i]->z < 0)
                {
                    sortedChunkList.push_back(m_loadedChunkList[i]);
                }
            }
            for (unsigned int i = 0; i < m_loadedChunkList.size(); ++i)
            {
                if (m_loadedChunkList[i]->x >= 0 && m_loadedChunkList[i]->z < 0)
                {
                    sortedChunkList.push_back(m_loadedChunkList[i]);
                }
            }
            for (unsigned int i = 0; i < m_loadedChunkList.size(); ++i)
            {
                if (m_loadedChunkList[i]->x >= 0 && m_loadedChunkList[i]->z >= 0)
                {
                    sortedChunkList.push_back(m_loadedChunkList[i]);
                }
            }
            for (unsigned int i = 0; i < m_loadedChunkList.size(); ++i)
            {
                if (m_loadedChunkList[i]->x < 0 && m_loadedChunkList[i]->z >= 0)
                {
                    sortedChunkList.push_back(m_loadedChunkList[i]);
                }
            }

            for (unsigned int i = 0; i < sortedChunkList.size(); ++i)
            {
                LevelChunk* chunk = sortedChunkList[i];
                if (!chunk->mDontSave)
                {
                    saveEntities(chunk);
                }
                if (chunk->shouldSave(force))
                {
                    save(chunk);
                    chunk->setUnsaved(false);
                    if (progressListener != NULL)
                    {
                        ++saveCount;
                        if (saveCount % SAVE_PROGRESS_INTERVAL == 0)
                        {
                            progressListener->progressStagePercentage(saveCount * 100 / saveTotal);
                        }
                    }
                }
                m_storage->WaitIfTooManyQueuedChunks();
            }
            m_storage->WaitForAll();
        }

        if (m_storage != NULL)
        {
            m_storage->flush();
        }
    }

    LeaveCriticalSection(&m_csLoadCreate);
    return true;
}

bool ServerChunkCache::tick()
{
    if (m_level->noSave)
    {
        return true;
    }

    for (int i = 0; i < MAX_DROPS_PER_TICK; ++i)
    {
        if (m_toDrop.empty())
        {
            continue;
        }

        LevelChunk* chunk = m_toDrop.front();
        if (!chunk->isUnloaded() && !chunk->containsPlayer())
        {
            save(chunk);
            saveEntities(chunk);
            chunk->unload(true, true);

            std::vector<LevelChunk*>::iterator it = std::find(m_loadedChunkList.begin(), m_loadedChunkList.end(),
                                                              chunk);
            if (it != m_loadedChunkList.end())
            {
                m_loadedChunkList.erase(it);
            }

            int idx = (chunk->x + m_xzOffset) * m_xzSize + (chunk->z + m_xzOffset);
            m_unloadedCache[idx] = chunk;
            m_cache[idx] = NULL;
            if (freesUnloadedChunks())
            {
                UnloadedChunk unloaded = {chunk, chunk->x, chunk->z, System::processTimeInMilliSecs()};
                m_unloadedQueue.push_back(unloaded);
            }
        }
        m_toDrop.pop_front();
    }

    freeOldUnloadedChunks();

    if (m_storage != NULL)
    {
        m_storage->tick();
    }
    return true;
}

bool ServerChunkCache::freesUnloadedChunks()
{
    return m_xzSize > ClientChunkCache::RING_WIDTH;
}

bool ServerChunkCache::isNearPlayer(int x, int z)
{
    static const int KEEP_DISTANCE = VideoSettings::RENDER_DISTANCE_MAX + 2;
    for (unsigned int i = 0; i < m_level->mPlayers.size(); ++i)
    {
        const int playerChunkX = Mth::floor(m_level->mPlayers[i]->mX / 16.0);
        const int playerChunkZ = Mth::floor(m_level->mPlayers[i]->mZ / 16.0);
        if (Mth::abs(x - playerChunkX) <= KEEP_DISTANCE && Mth::abs(z - playerChunkZ) <= KEEP_DISTANCE)
        {
            return true;
        }
    }
    return false;
}

void ServerChunkCache::freeOldUnloadedChunks()
{
    static const long long UNLOADED_LIFETIME_MS = 60000;
    const long long now = System::processTimeInMilliSecs();
    const size_t pending = m_unloadedQueue.size();
    EnterCriticalSection(&m_csLoadCreate);
    for (size_t i = 0; i < pending && now - m_unloadedQueue.front().unloadedAt > UNLOADED_LIFETIME_MS; ++i)
    {
        UnloadedChunk unloaded = m_unloadedQueue.front();
        m_unloadedQueue.pop_front();
        // A chunk loaded again since it was queued may already be freed, so only the cache says it is still here.
        const int idx = computeIdx(unloaded.x, unloaded.z);
        if (m_unloadedCache[idx] != unloaded.chunk)
        {
            continue;
        }
        if (isNearPlayer(unloaded.x, unloaded.z))
        {
            unloaded.unloadedAt = now;
            m_unloadedQueue.push_back(unloaded);
            continue;
        }
        m_unloadedCache[idx] = NULL;
        delete unloaded.chunk;
    }
    LeaveCriticalSection(&m_csLoadCreate);
}

bool ServerChunkCache::shouldSave()
{
    return !m_level->noSave;
}

std::wstring ServerChunkCache::gatherStats()
{
    return L"ServerChunkCache: ";
}

std::vector<Biome::MobSpawnerData*>* ServerChunkCache::getMobsAt(MobCategory* category, const BlockPos& pos)
{
    return m_source->getMobsAt(category, pos);
}

BlockPos* ServerChunkCache::findNearestMapFeature(Level* level, const std::wstring& feature, const BlockPos& pos,
                                                  bool takeBlock)
{
    return m_source->findNearestMapFeature(level, feature, pos, takeBlock);
}

void ServerChunkCache::recreateLogicStructuresForChunk(LevelChunk*, int, int)
{
}

bool ServerChunkCache::isPosInFeature(Level* level, const std::wstring& feature, const BlockPos& pos)
{
    return m_source != NULL && m_source->isPosInFeature(level, feature, pos);
}

LevelChunk* ServerChunkCache::getChunkAt(const BlockPos& pos)
{
    return getChunk(pos.x >> 4, pos.z >> 4);
}

unsigned int ServerChunkCache::runSaveThreadProc(void* data)
{
    ServerChunkCacheSaveThreadData* threadData = static_cast<ServerChunkCacheSaveThreadData*>(data);

    if (threadData->useSharedThreadStorage)
    {
        Compression::UseDefaultThreadStorage();
        OldChunkStorage::UseDefaultThreadStorage();
    }
    else
    {
        Compression::CreateNewThreadStorage();
        OldChunkStorage::CreateNewThreadStorage();
    }
    threadData->wakeEvent->WaitForSignal(-1);

    while (threadData->chunk != NULL)
    {
        PIXBeginNamedEvent(0.0f, "Saving entities");
        if (threadData->saveEntities)
        {
            threadData->cache->saveEntities(threadData->chunk);
        }
        PIXEndNamedEvent();

        PIXBeginNamedEvent(0.0f, "Saving chunk");
        threadData->cache->save(threadData->chunk);
        threadData->chunk->setUnsaved(false);
        PIXEndNamedEvent();

        PIXBeginNamedEvent(0.0f, "Notifying and waiting for next chunk");
        threadData->notificationEvent->Set();
        threadData->wakeEvent->WaitForSignal(-1);
        PIXEndNamedEvent();
    }

    if (!threadData->useSharedThreadStorage)
    {
        Compression::ReleaseThreadStorage();
        OldChunkStorage::ReleaseThreadStorage();
    }
    return 0;
}

int ServerChunkCache::getLoadedChunks()
{
    return getLoadedChunkList().size();
}

void ServerChunkCache::setOverrideBoundFunction(bool (*function)(void*, int, int), void* data)
{
    m_boundFunction = function;
    m_boundData = data;
}

LevelChunk** ServerChunkCache::getCache()
{
    return m_cache;
}
