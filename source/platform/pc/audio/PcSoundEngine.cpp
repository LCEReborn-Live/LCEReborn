#include "platform/pc/audio/PcSoundEngine.hpp"

#include <math.h>
#include <string.h>
#include <time.h>

#include <utility>

#include "platform/FileApi.hpp"
#include "platform/pc/Log.hpp"

namespace
{
const Logger sLog("Audio");
const char SOUND_BANK_PATH[] = "WiiU/Sound/Minecraft.msscmp";
const char TU80_SOUND_BANK_PATH[] = "WiiU/Sound/TU80.msscmp";
const float PI = 3.14159265f;
const float EPSILON = 0.0001f;
const float ALIGNED_DOT = 0.9999f;
const float DOWNMIX = 0.70710678f;
const float NO_FALLOFF_DISTANCE = 10000.0f;
const float MINIMUM_DISTANCE = 1.0f;
const float PAN_EXPONENT = 3.0f;
const int SPEAKER_COUNT = 8;

enum Speaker
{
    SPEAKER_FRONT_LEFT,
    SPEAKER_FRONT_RIGHT,
    SPEAKER_CENTER,
    SPEAKER_BACK_LEFT,
    SPEAKER_BACK_RIGHT,
    DIRECTED_SPEAKER_COUNT
};

const float SPEAKER_DIRECTIONS[DIRECTED_SPEAKER_COUNT][3] = {
    {-DOWNMIX, 0.0f, DOWNMIX}, {DOWNMIX, 0.0f, DOWNMIX}, {0.0f, 0.0f, 1.0f}, {-DOWNMIX, 0.0f, -DOWNMIX},
    {DOWNMIX, 0.0f, -DOWNMIX},
};

bool ReadWholeFile(const char* path, std::vector<unsigned char>* bytes)
{
    int file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    unsigned int size = GetFileSize(file, NULL);
    bytes->resize(size);
    unsigned int total = 0;
    while (total < size)
    {
        unsigned int read = 0;
        if (ReadFile(file, &(*bytes)[total], size - total, &read, NULL) == 0 || read == 0)
            break;
        total += read;
    }
    CloseHandle(file);
    return total == size && size > 0;
}

float Dot(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Normalise(float* vector, float fallbackX, float fallbackY, float fallbackZ)
{
    float length = sqrtf(Dot(vector, vector));
    if (length < EPSILON)
    {
        vector[0] = fallbackX;
        vector[1] = fallbackY;
        vector[2] = fallbackZ;
        return;
    }
    vector[0] /= length;
    vector[1] /= length;
    vector[2] /= length;
}
}

PcSoundEngine::PcSoundEngine()
    : m_ready(false),
      m_nextOrder(0),
      m_random(static_cast<unsigned int>(time(NULL)) | 1u),
      m_streamVoice(PcMixer::INVALID_VOICE),
      m_streamIs3D(false),
      m_streamMaxDistance(0.0f),
      m_streamX(0.0f),
      m_streamY(0.0f),
      m_streamZ(0.0f),
      m_streamVolume(1.0f)
{
    memset(&m_listener, 0, sizeof(m_listener));
    m_listener.face[2] = 1.0f;
    m_listener.up[1] = 1.0f;
    m_listener.right[0] = 1.0f;
}

PcSoundEngine::~PcSoundEngine()
{
    shutdown();
}

void PcSoundEngine::init(char*, SoundBankLoadedCallback)
{
    std::vector<unsigned char> bytes;
    if (!ReadWholeFile(SOUND_BANK_PATH, &bytes))
    {
        LOG_WARN(sLog, "The sound bank %s could not be read; the game will be silent", SOUND_BANK_PATH);
        return;
    }

    std::string error;
    if (!m_bank.Load(bytes, &error))
    {
        LOG_WARN(sLog, "The sound bank %s could not be loaded (%s); the game will be silent", SOUND_BANK_PATH,
                 error.c_str());
        return;
    }
    for (size_t i = 0; i < m_bank.Skipped().size(); ++i)
        LOG_WARN(sLog, "Sound bank: %s", m_bank.Skipped()[i].c_str());

    const bool hasTu80Bank = ReadWholeFile(TU80_SOUND_BANK_PATH, &bytes);
    m_tu80BankLoaded = hasTu80Bank && m_tu80Bank.Load(bytes, &error);
    if (m_tu80BankLoaded)
    {
        for (const auto& skipped : m_tu80Bank.Skipped())
            LOG_WARN(sLog, "TU80 sound bank: %s", skipped.c_str());
        LOG_INFO(sLog, "TU80 sound bank loaded: %zu events, %zu sounds",
                 m_tu80Bank.Events().size(), m_tu80Bank.Sounds().size());
    }
    else if (hasTu80Bank)
        LOG_WARN(sLog, "The TU80 sound bank could not be loaded (%s)", error.c_str());

    if (!m_mixer.Open())
        return;

    LOG_INFO(sLog, "Sound bank loaded: %zu events, %zu sounds", m_bank.Events().size(), m_bank.Sounds().size());
    m_ready = true;
}

float PcSoundEngine::Random()
{
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return (m_random >> 8) * (1.0f / 16777216.0f);
}

void PcSoundEngine::startSound(void* soundData, int soundDataSize, char* eventName)
{
    if (eventName == NULL)
        return;
    Pending pending;
    pending.eventName = eventName;
    if (soundData != NULL && soundDataSize > 0)
    {
        const unsigned char* bytes = static_cast<const unsigned char*>(soundData);
        pending.data.assign(bytes, bytes + soundDataSize);
    }
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pending.push_back(pending);
}

const PcSoundBank::Choice* PcSoundEngine::Choose(const PcSoundBank::Play& play)
{
    const int count = static_cast<int>(play.choices.size());
    if (count == 0)
        return NULL;

    int& state = m_selectionState[&play];
    unsigned long long pickMask = 0;
    int forcedIndex = -1;
    if (play.selectionMode == PcSoundBank::SELECT_RANDOM_NO_REPEAT)
        pickMask = static_cast<unsigned int>(state);
    else if (play.selectionMode == PcSoundBank::SELECT_IN_ORDER)
        forcedIndex = state;
    if (forcedIndex >= count)
        forcedIndex = 0;

    int totalWeight = 0;
    int availableWeight = 0;
    int played = 0;
    for (int i = 0; i < count; ++i)
    {
        totalWeight += play.choices[i].weight;
        if (i < 64 && (pickMask & (1ull << i)) != 0)
            ++played;
        else
            availableWeight += play.choices[i].weight;
    }
    int range = availableWeight;
    if (played == count)
    {
        pickMask = 0;
        range = totalWeight;
    }

    int chosen = 0;
    if (forcedIndex != -1)
    {
        chosen = forcedIndex;
    }
    else if (range > 0)
    {
        int remaining = static_cast<int>(Random() * range);
        for (int i = 0; i < count; ++i)
        {
            if (i < 64 && (pickMask & (1ull << i)) != 0)
                continue;
            if (remaining < play.choices[i].weight)
            {
                chosen = i;
                break;
            }
            remaining -= play.choices[i].weight;
        }
    }

    if (play.selectionMode == PcSoundBank::SELECT_RANDOM_NO_REPEAT)
        state = chosen < 31 ? (1 << chosen) : 0;
    else if (play.selectionMode == PcSoundBank::SELECT_IN_ORDER)
        state = chosen + 1;
    return &play.choices[chosen];
}

bool PcSoundEngine::MakeRoom(const PcSoundBank::Sound& sound, const PcSoundBank::Play& play)
{
    if (sound.maxInstances <= 0)
        return true;

    int active = 0;
    Instance* oldest = NULL;
    Instance* lowerPriority = NULL;
    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        Instance& instance = m_instances[i];
        if (instance.state == INSTANCE_DONE || instance.evicted || instance.sound != &sound)
            continue;
        ++active;
        if (oldest == NULL || instance.order < oldest->order)
            oldest = &instance;
        if (instance.play->priority < play.priority && (lowerPriority == NULL || instance.order < lowerPriority->order))
            lowerPriority = &instance;
    }
    if (active < sound.maxInstances)
        return true;

    Instance* victim = play.limitPolicy == PcSoundBank::LIMIT_LOWER_PRIORITY ? lowerPriority : oldest;
    if (victim == NULL)
        return false;
    victim->evicted = true;
    victim->state = INSTANCE_PLAYING;
    m_mixer.Stop(victim->voice);
    return true;
}

void PcSoundEngine::StartEvent(const Pending& pending)
{
    const char* name = pending.eventName.c_str();
    const PcSoundBank::Event* event = m_tu80BankLoaded ? m_tu80Bank.FindEvent(name) : NULL;
    const PcSoundBank& bank = event ? m_tu80Bank : m_bank;
    if (!event)
        event = m_bank.FindEvent(name);
    if (!event)
    {
        if (pending.eventName == "Minecraft/block/barrel/open")
            event = m_bank.FindEvent("Minecraft/block/chest/open");
        else if (pending.eventName == "Minecraft/block/barrel/close")
            event = m_bank.FindEvent("Minecraft/block/chest/close");
        else if (pending.eventName == "Minecraft/block/blastfurnace/fire_crackle")
            event = m_bank.FindEvent("Minecraft/block/furnace/fire_crackle");
    }
    if (event == NULL)
    {
        if (m_missingEvents.insert(pending.eventName).second)
            LOG_WARN(sLog, "Sound event %s is not in the sound bank", pending.eventName.c_str());
        return;
    }

    for (size_t p = 0; p < event->plays.size(); ++p)
    {
        const PcSoundBank::Play& play = event->plays[p];
        const PcSoundBank::Choice* choice = Choose(play);
        if (choice == NULL)
            continue;
        const PcSoundBank::Sound* sound = bank.FindSound(choice->sound.c_str());
        if (sound == NULL || !MakeRoom(*sound, play))
            continue;

        Instance instance;
        instance.state = INSTANCE_NEW;
        instance.selection = pending.data;
        instance.sound = sound;
        instance.play = &play;
        instance.voice = PcMixer::INVALID_VOICE;
        instance.order = m_nextOrder++;
        instance.evicted = false;
        instance.is3D = false;
        instance.maxDistance = 0.0f;
        instance.x = 0.0f;
        instance.y = 0.0f;
        instance.z = 0.0f;
        instance.eventVolume = play.volumeMinimum + (play.volumeMaximum - play.volumeMinimum) * Random();
        instance.eventPitch = play.pitchMinimum + (play.pitchMaximum - play.pitchMinimum) * Random();
        m_instances.push_back(instance);
    }
}

void PcSoundEngine::Levels(bool is3D, float x, float y, float z, float maxDistance, float volume, float* left,
                           float* right) const
{
    if (!is3D)
    {
        *left = volume;
        *right = volume;
        return;
    }

    float direction[3] = {x - m_listener.position[0], y - m_listener.position[1], z - m_listener.position[2]};
    float distance = sqrtf(Dot(direction, direction));
    if (distance > maxDistance)
    {
        *left = 0.0f;
        *right = 0.0f;
        return;
    }

    float gain = 1.0f;
    if (distance > MINIMUM_DISTANCE + EPSILON && maxDistance != NO_FALLOFF_DISTANCE)
    {
        gain = 1.0f - distance / maxDistance;
        gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    }

    if (distance <= EPSILON)
    {
        float level = gain * sqrtf(1.0f / SPEAKER_COUNT) * (1.0f + 2.0f * DOWNMIX) * volume;
        *left = level;
        *right = level;
        return;
    }

    float local[3] = {Dot(direction, m_listener.right) / distance, Dot(direction, m_listener.up) / distance,
                      Dot(direction, m_listener.face) / distance};
    float terms[DIRECTED_SPEAKER_COUNT];
    float total = 0.0f;
    for (int speaker = 0; speaker < DIRECTED_SPEAKER_COUNT; ++speaker)
    {
        float dot = Dot(SPEAKER_DIRECTIONS[speaker], local);
        if (dot > ALIGNED_DOT)
            terms[speaker] = PI;
        else if (dot < -ALIGNED_DOT)
            terms[speaker] = 0.0f;
        else
            terms[speaker] = PI - acosf(dot);
        total += terms[speaker];
    }

    float levels[DIRECTED_SPEAKER_COUNT];
    float normaliser = sqrtf(PAN_EXPONENT);
    for (int speaker = 0; speaker < DIRECTED_SPEAKER_COUNT; ++speaker)
    {
        float amplitude = total > 0.0f ? sqrtf(powf(terms[speaker], PAN_EXPONENT) / total) / normaliser : 0.0f;
        levels[speaker] = (amplitude > 1.0f ? 1.0f : amplitude) * gain;
    }

    *left = (levels[SPEAKER_FRONT_LEFT] + DOWNMIX * levels[SPEAKER_CENTER] + DOWNMIX * levels[SPEAKER_BACK_LEFT])
            * volume;
    *right = (levels[SPEAKER_FRONT_RIGHT] + DOWNMIX * levels[SPEAKER_CENTER] + DOWNMIX * levels[SPEAKER_BACK_RIGHT])
             * volume;
}

void PcSoundEngine::tick(void* userData, float listenerX, float listenerY, float listenerZ, float listenerForwardX,
                         float listenerForwardY, float listenerForwardZ, ConfigureSoundCallback configureSound,
                         UpdateSoundCallback updateSound, RouteToDRCCallback routeToDRC, bool* systemMusicPlaying)
{
    *systemMusicPlaying = false;

    std::vector<Pending> pending;
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        pending.swap(m_pending);
    }
    if (!m_ready)
        return;

    m_listener.position[0] = listenerX;
    m_listener.position[1] = listenerY;
    m_listener.position[2] = listenerZ;
    m_listener.face[0] = listenerForwardX;
    m_listener.face[1] = listenerForwardY;
    m_listener.face[2] = listenerForwardZ;
    Normalise(m_listener.face, 0.0f, 0.0f, 1.0f);
    m_listener.right[0] = m_listener.face[2] * m_listener.up[1] - m_listener.face[1] * m_listener.up[2];
    m_listener.right[1] = m_listener.face[0] * m_listener.up[2] - m_listener.face[2] * m_listener.up[0];
    m_listener.right[2] = m_listener.face[1] * m_listener.up[0] - m_listener.face[0] * m_listener.up[1];
    Normalise(m_listener.right, 1.0f, 0.0f, 0.0f);

    for (size_t i = 0; i < pending.size(); ++i)
        StartEvent(pending[i]);

    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        Instance& instance = m_instances[i];
        void* selection = instance.selection.empty() ? NULL : &instance.selection[0];
        bool muted = selection != NULL && routeToDRC(userData, selection);
        bool is3D = false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float volume = 0.0f;

        if (instance.state == INSTANCE_NEW)
        {
            float maxDistance = 0.0f;
            float rate = 0.0f;
            configureSound(userData, selection, &is3D, &maxDistance, &rate, &x, &y, &z, &volume);
            instance.is3D = is3D;
            instance.maxDistance = maxDistance;
            instance.x = x;
            instance.y = y;
            instance.z = z;
            float configuredVolume = volume;
            updateSound(userData, selection, &is3D, 0, &x, &y, &z, &volume);

            float left;
            float right;
            Levels(instance.is3D, instance.x, instance.y, instance.z, instance.maxDistance,
                   configuredVolume * instance.eventVolume, &left, &right);
            float playbackRate = (rate != 0.0f ? rate : 1.0f) * instance.eventPitch;
            instance.voice = m_mixer.Play(instance.sound->data, instance.sound->size, NULL, instance.play->loopCount,
                                          muted, playbackRate, left, right);
            instance.state = INSTANCE_PLAYING;
            continue;
        }

        if (instance.state == INSTANCE_PLAYING && !m_mixer.IsFinished(instance.voice))
        {
            updateSound(userData, selection, &is3D, 1, &x, &y, &z, &volume);
            if (is3D)
            {
                instance.x = x;
                instance.y = y;
                instance.z = z;
            }
            float left;
            float right;
            Levels(instance.is3D, instance.x, instance.y, instance.z, instance.maxDistance,
                   volume * instance.eventVolume, &left, &right);
            m_mixer.SetLevels(instance.voice, left, right);
            m_mixer.SetMuted(instance.voice, muted);
            continue;
        }

        updateSound(userData, selection, &is3D, 2, &x, &y, &z, &volume);
        if (instance.voice != PcMixer::INVALID_VOICE)
            m_mixer.Release(instance.voice);
        instance.voice = PcMixer::INVALID_VOICE;
        instance.state = INSTANCE_DONE;
    }

    size_t kept = 0;
    for (size_t i = 0; i < m_instances.size(); ++i)
    {
        if (m_instances[i].state == INSTANCE_DONE)
            continue;
        if (kept != i)
            m_instances[kept] = std::move(m_instances[i]);
        ++kept;
    }
    m_instances.resize(kept);
}

bool PcSoundEngine::openStream(char* fileName)
{
    std::vector<unsigned char> bytes;
    if (fileName == NULL || !ReadWholeFile(fileName, &bytes))
    {
        LOG_WARN(sLog, "Music %s could not be read", fileName != NULL ? fileName : "(none)");
        return false;
    }
    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_streamBytes.swap(bytes);
    return true;
}

void PcSoundEngine::startStream(void* userData, ConfigureSoundCallback configureSound)
{
    bool is3D = false;
    float maxDistance = 0.0f;
    float rate = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float volume = 0.0f;
    configureSound(userData, NULL, &is3D, &maxDistance, &rate, &x, &y, &z, &volume);

    std::lock_guard<std::mutex> lock(m_streamMutex);
    if (m_streamVoice != PcMixer::INVALID_VOICE)
        m_mixer.Release(m_streamVoice);
    m_streamIs3D = is3D;
    m_streamMaxDistance = maxDistance;
    m_streamX = x;
    m_streamY = y;
    m_streamZ = z;
    m_streamVolume = volume;
    float left;
    float right;
    Levels(is3D, x, y, z, maxDistance, volume, &left, &right);
    m_streamVoice = m_mixer.Play(NULL, 0, &m_streamBytes, 1, false, rate > 0.0f ? rate : 1.0f, left, right);
    m_streamBytes.clear();
}

void PcSoundEngine::setStreamVol(float volume)
{
    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_streamVolume = volume;
    float left;
    float right;
    Levels(m_streamIs3D, m_streamX, m_streamY, m_streamZ, m_streamMaxDistance, volume, &left, &right);
    m_mixer.SetLevels(m_streamVoice, left, right);
}

void PcSoundEngine::stopStream()
{
    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_mixer.Release(m_streamVoice);
    m_streamVoice = PcMixer::INVALID_VOICE;
}

void PcSoundEngine::updateStreamPos(void* userData, UpdateSoundCallback updateSound)
{
    bool is3D = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float volume = 0.0f;
    updateSound(userData, NULL, &is3D, 1, &x, &y, &z, &volume);
    if (!is3D)
        return;

    std::lock_guard<std::mutex> lock(m_streamMutex);
    m_streamX = x;
    m_streamY = y;
    m_streamZ = z;
    float left;
    float right;
    Levels(m_streamIs3D, x, y, z, m_streamMaxDistance, m_streamVolume, &left, &right);
    m_mixer.SetLevels(m_streamVoice, left, right);
}

bool PcSoundEngine::isStreamComplete()
{
    std::lock_guard<std::mutex> lock(m_streamMutex);
    if (m_streamVoice == PcMixer::INVALID_VOICE || !m_mixer.IsFinished(m_streamVoice))
        return false;
    m_mixer.Release(m_streamVoice);
    m_streamVoice = PcMixer::INVALID_VOICE;
    return true;
}

void PcSoundEngine::shutdown()
{
    m_ready = false;
    m_mixer.Close();
    m_instances.clear();
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pending.clear();
}
