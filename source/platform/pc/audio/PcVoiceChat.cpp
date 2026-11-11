#include "platform/pc/audio/PcVoiceChat.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <opus.h>
#include <SDL3/SDL.h>

#include "platform/pc/Log.hpp"

namespace
{
const Logger sLog("Voice");
const int SAMPLE_RATE = 48000;
const int FRAME_SAMPLES = 960; // 20 ms at 48 kHz.
const int MAX_OPUS_PACKET = 1024;
const int PLAYBACK_CHANNELS = 2;
const float VAD_THRESHOLD = 430.0f;
const unsigned int VAD_HANGOVER_FRAMES = 8;
const unsigned int RING_MASK = 8191;
const unsigned int START_BUFFER_SAMPLES = FRAME_SAMPLES * 2;
}

PcVoiceChat::RemoteVoice::RemoteVoice()
    : decoder(NULL), read(0), write(0), queued(0), ready(false)
{
}

PcVoiceChat::PcVoiceChat()
    : m_captureStream(NULL),
      m_playbackStream(NULL),
      m_encoder(NULL),
      m_audioInitialized(false),
      m_enabled(false),
      m_talking(false),
      m_silenceFrames(0)
{
}

PcVoiceChat::~PcVoiceChat()
{
    Close();
}

bool PcVoiceChat::Open()
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        LOG_WARN(sLog, "Voice chat audio could not be started: %s", SDL_GetError());
        return false;
    }
    m_audioInitialized = true;

    SDL_AudioSpec captureSpec = {};
    captureSpec.format = SDL_AUDIO_S16;
    captureSpec.channels = 1;
    captureSpec.freq = SAMPLE_RATE;
    m_captureStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &captureSpec, NULL, NULL);
    if (m_captureStream == NULL)
    {
        LOG_WARN(sLog, "Voice chat microphone could not be opened: %s", SDL_GetError());
        Close();
        return false;
    }

    SDL_AudioSpec playbackSpec = {};
    playbackSpec.format = SDL_AUDIO_F32;
    playbackSpec.channels = PLAYBACK_CHANNELS;
    playbackSpec.freq = SAMPLE_RATE;
    m_playbackStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &playbackSpec,
                                                  PlaybackCallback, this);
    if (m_playbackStream == NULL)
    {
        LOG_WARN(sLog, "Voice chat playback could not be opened: %s", SDL_GetError());
        Close();
        return false;
    }

    int error = OPUS_OK;
    m_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &error);
    if (m_encoder == NULL || error != OPUS_OK)
    {
        LOG_WARN(sLog, "Voice chat encoder could not be created: %d", error);
        Close();
        return false;
    }
    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(m_encoder, OPUS_SET_DTX(1));
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

    m_capture.clear();
    m_capture.reserve(FRAME_SAMPLES * 4);
    m_playback.resize(4096 * PLAYBACK_CHANNELS);
    SDL_ResumeAudioStreamDevice(m_captureStream);
    SDL_ResumeAudioStreamDevice(m_playbackStream);
    LOG_INFO(sLog, "Voice chat ready: open microphone, voice activity detection enabled");
    return true;
}

void PcVoiceChat::Close()
{
    if (m_captureStream != NULL)
    {
        SDL_PauseAudioStreamDevice(m_captureStream);
        SDL_DestroyAudioStream(m_captureStream);
        m_captureStream = NULL;
    }
    if (m_playbackStream != NULL)
    {
        SDL_PauseAudioStreamDevice(m_playbackStream);
        SDL_DestroyAudioStream(m_playbackStream);
        m_playbackStream = NULL;
    }
    if (m_encoder != NULL)
    {
        opus_encoder_destroy(m_encoder);
        m_encoder = NULL;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_remote.size(); ++i)
        {
            if (m_remote[i].decoder != NULL)
            {
                opus_decoder_destroy(m_remote[i].decoder);
                m_remote[i].decoder = NULL;
            }
            m_remote[i].read = 0;
            m_remote[i].write = 0;
            m_remote[i].queued = 0;
            m_remote[i].ready = false;
        }
    }
    m_capture.clear();
    m_playback.clear();
    if (m_audioInitialized)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_audioInitialized = false;
    }
}

void PcVoiceChat::Tick(bool enabled, const SendCallback& send)
{
    if (!enabled)
    {
        if (m_enabled)
        {
            m_enabled = false;
            m_talking = false;
            m_silenceFrames = 0;
            send(false, false, std::vector<unsigned char>());
            Close();
        }
        return;
    }

    if (!m_enabled)
    {
        if (!Open())
            return;
        m_enabled = true;
        m_talking = false;
        m_silenceFrames = 0;
        send(true, false, std::vector<unsigned char>());
    }

    ProcessCapture(send);
}

void PcVoiceChat::ProcessCapture(const SendCallback& send)
{
    if (m_captureStream == NULL)
        return;

    int available = SDL_GetAudioStreamAvailable(m_captureStream);
    if (available <= 0)
        return;
    const int maxCaptureBytes = FRAME_SAMPLES * 2 * 4;
    available = std::min(available, maxCaptureBytes);
    std::vector<short> incoming(static_cast<size_t>(available + 1) / sizeof(short));
    const int received = SDL_GetAudioStreamData(m_captureStream, &incoming[0], available);
    if (received <= 0)
        return;
    incoming.resize(static_cast<size_t>(received) / sizeof(short));
    m_capture.insert(m_capture.end(), incoming.begin(), incoming.end());

    size_t offset = 0;
    while (m_capture.size() - offset >= FRAME_SAMPLES)
    {
        EncodeFrame(&m_capture[offset], send);
        offset += FRAME_SAMPLES;
    }
    if (offset > 0)
        m_capture.erase(m_capture.begin(), m_capture.begin() + offset);
    if (m_capture.size() > FRAME_SAMPLES * 4)
        m_capture.erase(m_capture.begin(), m_capture.end() - FRAME_SAMPLES * 2);
}

void PcVoiceChat::EncodeFrame(const short* samples, const SendCallback& send)
{
    double energy = 0.0;
    for (int i = 0; i < FRAME_SAMPLES; ++i)
        energy += static_cast<double>(samples[i]) * samples[i];
    const float rms = static_cast<float>(std::sqrt(energy / FRAME_SAMPLES));

    if (rms >= VAD_THRESHOLD)
    {
        m_silenceFrames = 0;
        m_talking = true;
    }
    else if (m_talking && ++m_silenceFrames >= VAD_HANGOVER_FRAMES)
    {
        m_talking = false;
        m_silenceFrames = 0;
        send(true, false, std::vector<unsigned char>());
        return;
    }

    if (!m_talking || m_encoder == NULL)
        return;

    std::vector<unsigned char> encoded(MAX_OPUS_PACKET);
    const int bytes = opus_encode(m_encoder, samples, FRAME_SAMPLES, &encoded[0], encoded.size());
    if (bytes <= 0)
        return;
    encoded.resize(static_cast<size_t>(bytes));
    send(true, true, encoded);
}

void PcVoiceChat::Receive(unsigned char playerId, bool hasVoice, bool talking,
                          const std::vector<unsigned char>& packet)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    RemoteVoice& voice = m_remote[playerId];
    if (!hasVoice)
    {
        if (voice.decoder != NULL)
        {
            opus_decoder_destroy(voice.decoder);
            voice.decoder = NULL;
        }
        voice.read = voice.write = voice.queued = 0;
        voice.ready = false;
        return;
    }
    if (!talking || packet.empty() || packet.size() > MAX_OPUS_PACKET)
        return;
    if (voice.decoder == NULL)
    {
        int error = OPUS_OK;
        voice.decoder = opus_decoder_create(SAMPLE_RATE, 1, &error);
        if (voice.decoder == NULL || error != OPUS_OK)
            return;
    }

    std::array<short, FRAME_SAMPLES> decoded;
    const int samples = opus_decode(voice.decoder, &packet[0], static_cast<opus_int32>(packet.size()),
                                   &decoded[0], FRAME_SAMPLES, 0);
    if (samples <= 0)
        return;
    for (int i = 0; i < samples; ++i)
    {
        if (voice.queued == voice.samples.size())
        {
            voice.read = (voice.read + 1) & RING_MASK;
            --voice.queued;
        }
        voice.samples[voice.write] = decoded[i];
        voice.write = (voice.write + 1) & RING_MASK;
        ++voice.queued;
    }
}

void PcVoiceChat::PlaybackCallback(void* userdata, SDL_AudioStream* stream, int additionalBytes, int)
{
    static_cast<PcVoiceChat*>(userdata)->MixPlayback(stream, additionalBytes);
}

void PcVoiceChat::MixPlayback(SDL_AudioStream* stream, int additionalBytes)
{
    const int frames = additionalBytes / (sizeof(float) * PLAYBACK_CHANNELS);
    if (frames <= 0)
        return;
    if (m_playback.size() < static_cast<size_t>(frames * PLAYBACK_CHANNELS))
        m_playback.resize(static_cast<size_t>(frames * PLAYBACK_CHANNELS));
    std::fill(m_playback.begin(), m_playback.begin() + frames * PLAYBACK_CHANNELS, 0.0f);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int frame = 0; frame < frames; ++frame)
        {
            float mixed = 0.0f;
            for (size_t player = 0; player < m_remote.size(); ++player)
            {
                RemoteVoice& voice = m_remote[player];
                if (!voice.ready)
                {
                    if (voice.queued < START_BUFFER_SAMPLES)
                        continue;
                    voice.ready = true;
                }
                if (voice.queued == 0)
                {
                    voice.ready = false;
                    continue;
                }
                mixed += static_cast<float>(voice.samples[voice.read]) / 32768.0f * 0.55f;
                voice.read = (voice.read + 1) & RING_MASK;
                --voice.queued;
            }
            mixed = std::max(-1.0f, std::min(1.0f, mixed));
            m_playback[frame * 2] = mixed;
            m_playback[frame * 2 + 1] = mixed;
        }
    }
    SDL_PutAudioStreamData(stream, &m_playback[0], frames * sizeof(float) * PLAYBACK_CHANNELS);
}
