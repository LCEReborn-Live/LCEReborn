#ifndef PLATFORM_PC_AUDIO_PCSOUNDENGINE_HPP
#define PLATFORM_PC_AUDIO_PCSOUNDENGINE_HPP

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "game/client/audio/PlatformSoundEngine.hpp"
#include "platform/pc/audio/PcMixer.hpp"
#include "platform/pc/audio/PcSoundBank.hpp"

// The game's sound engine on PC: plays the events of the game's sound bank
// and the music streams, placing 3D sounds the way the Wii U edition does.
class PcSoundEngine : public PlatformSoundEngine
{
public:
    PcSoundEngine();
    virtual ~PcSoundEngine();

    virtual void init(char* soundBankPath, SoundBankLoadedCallback soundBankLoaded);
    virtual void tick(void* userData, float listenerX, float listenerY, float listenerZ, float listenerForwardX,
                      float listenerForwardY, float listenerForwardZ, ConfigureSoundCallback configureSound,
                      UpdateSoundCallback updateSound, RouteToDRCCallback routeToDRC, bool* systemMusicPlaying);
    virtual void startSound(void* soundData, int soundDataSize, char* eventName);
    virtual bool openStream(char* fileName);
    virtual void startStream(void* userData, ConfigureSoundCallback configureSound);
    virtual void setStreamVol(float volume);
    virtual void stopStream();
    virtual void updateStreamPos(void* userData, UpdateSoundCallback updateSound);
    virtual bool isStreamComplete();
    virtual void shutdown();

private:
    enum InstanceState
    {
        INSTANCE_NEW,
        INSTANCE_PLAYING,
        INSTANCE_DONE
    };

    struct Pending
    {
        std::string eventName;
        std::vector<unsigned char> data;
    };

    struct Instance
    {
        InstanceState state;
        std::vector<unsigned char> selection;
        const PcSoundBank::Sound* sound;
        const PcSoundBank::Play* play;
        int voice;
        unsigned long long order;
        bool evicted;
        bool is3D;
        float maxDistance;
        float x;
        float y;
        float z;
        float eventVolume;
        float eventPitch;
    };

    struct Listener
    {
        float position[3];
        float face[3];
        float up[3];
        float right[3];
    };

    void StartEvent(const Pending& pending);
    const PcSoundBank::Choice* Choose(const PcSoundBank::Play& play);
    bool MakeRoom(const PcSoundBank::Sound& sound, const PcSoundBank::Play& play);
    void Levels(bool is3D, float x, float y, float z, float maxDistance, float volume, float* left,
                float* right) const;
    float Random();

    PcMixer m_mixer;
    PcSoundBank m_bank;
    PcSoundBank m_tu80Bank;
    bool m_tu80BankLoaded = false;
    bool m_ready;
    Listener m_listener;

    std::mutex m_pendingMutex;
    std::vector<Pending> m_pending;
    std::vector<Instance> m_instances;
    std::map<const PcSoundBank::Play*, int> m_selectionState;
    std::set<std::string> m_missingEvents;
    unsigned long long m_nextOrder;
    unsigned int m_random;

    std::mutex m_streamMutex;
    std::vector<unsigned char> m_streamBytes;
    int m_streamVoice;
    bool m_streamIs3D;
    float m_streamMaxDistance;
    float m_streamX;
    float m_streamY;
    float m_streamZ;
    float m_streamVolume;
};

#endif
