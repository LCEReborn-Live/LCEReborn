#ifndef PLATFORM_PC_AUDIO_PCMIXER_HPP
#define PLATFORM_PC_AUDIO_PCMIXER_HPP

#include <stddef.h>

#include <mutex>
#include <vector>

#include "platform/pc/audio/PcAudioDecoder.hpp"

struct SDL_AudioStream;

// Plays Bink Audio voices, mixed to stereo through one SDL audio stream.
class PcMixer
{
public:
    static const int INVALID_VOICE = -1;

    PcMixer();
    ~PcMixer();

    bool Open();
    void Close();

    // data must outlive the voice unless owned is given, in which case the
    // voice takes the bytes. A loop count of 0 loops until stopped. A muted
    // voice keeps time without being heard.
    int Play(const unsigned char* data, size_t size, std::vector<unsigned char>* owned, int loopCount, bool muted,
             float rate, float left, float right);
    void SetLevels(int voice, float left, float right);
    void SetRate(int voice, float rate);
    void SetMuted(int voice, bool muted);
    void SetPaused(int voice, bool paused);
    void Stop(int voice);
    bool IsFinished(int voice);
    void Release(int voice);

private:
    struct Voice
    {
        bool used;
        bool finished;
        bool muted;
        bool paused;
        unsigned int generation;
        PcAudioDecoder decoder;
        std::vector<unsigned char> owned;
        std::vector<float> frames;
        long long framesBase;
        int framesCount;
        double position;
        double step;
        double rate;
        int loopsLeft;
        float level[2];
        float target[2];
    };

    static void Callback(void* user, SDL_AudioStream* stream, int additional, int total);
    void Mix(float* out, int frames);
    void MixVoice(Voice& voice, float* out, int frames);
    bool FetchFrames(Voice& voice, long long frame);
    bool Restart(Voice& voice);
    Voice* Find(int voice);

    std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::vector<float> m_mixBuffer;
    SDL_AudioStream* m_stream;
    int m_outputRate;
    bool m_subsystem;
};

#endif
