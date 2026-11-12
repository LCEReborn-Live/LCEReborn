#ifndef PLATFORM_PC_AUDIO_PCVOICECHAT_HPP
#define PLATFORM_PC_AUDIO_PCVOICECHAT_HPP

#include <array>
#include <functional>
#include <mutex>
#include <vector>

struct SDL_AudioStream;
struct OpusEncoder;
struct OpusDecoder;
struct DenoiseState;

class PcVoiceChat
{
public:
    typedef std::function<void(bool, bool, const std::vector<unsigned char>&)> SendCallback;

    PcVoiceChat();
    ~PcVoiceChat();

    void Tick(bool enabled, const SendCallback& send);
    void Receive(unsigned char playerId, bool hasVoice, bool talking, const std::vector<unsigned char>& packet);

private:
    struct RemoteVoice
    {
        RemoteVoice();
        OpusDecoder* decoder;
        std::array<short, 8192> samples;
        unsigned int read;
        unsigned int write;
        unsigned int queued;
        bool ready;
    };

    bool Open();
    void Close();
    void ProcessCapture(const SendCallback& send);
    void EncodeFrame(const short* samples, const SendCallback& send);
    void MixPlayback(SDL_AudioStream* stream, int additionalBytes);
    static void PlaybackCallback(void* userdata, SDL_AudioStream* stream, int additionalBytes, int totalBytes);

    std::mutex m_mutex;
    std::array<RemoteVoice, 256> m_remote;
    std::vector<short> m_capture;
    std::vector<unsigned char> m_captureBytes;
    std::vector<float> m_playback;
    SDL_AudioStream* m_captureStream;
    SDL_AudioStream* m_playbackStream;
    OpusEncoder* m_encoder;
    DenoiseState* m_denoiser;
    bool m_audioInitialized;
    bool m_enabled;
    bool m_talking;
    unsigned int m_silenceFrames;
};

#endif
