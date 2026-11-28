#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "platform/pc/audio/BinkAudioDecoder.hpp"

// Native bank samples may be the original Bink format or lossless PCM WAVE.
// This keeps imported Java sounds in the same mixer, limits and 3D sound path.
class PcAudioDecoder
{
public:
    bool Open(const unsigned char* data, size_t size)
    {
        mPcm = nullptr; mPosition = 0;
        if (size < 12 || std::memcmp(data,"RIFF",4) != 0) return mBink.Open(data,size);
        if (std::memcmp(data+8,"WAVE",4) != 0) return false;
        const unsigned char* pcm = nullptr; size_t length = 0;
        int channels = 0, rate = 0;
        for (size_t offset=12; offset+8<=size;)
        {
            size_t bytes = read32(data+offset+4);
            if (bytes > size-offset-8) return false;
            if (std::memcmp(data+offset,"fmt ",4)==0)
            {
                if (bytes<16 || read16(data+offset+8)!=1 || read16(data+offset+22)!=16) return false;
                channels=read16(data+offset+10);rate=read32(data+offset+12);
                if (channels<1 || channels>2 || rate<8000 || rate>192000
                    || read16(data+offset+20)!=channels*2) return false;
            }
            else if (std::memcmp(data+offset,"data",4)==0) { pcm=data+offset+8;length=bytes; }
            offset+=8+bytes+(bytes&1);
        }
        if (!pcm || !channels || length % (channels*2) != 0 || !length) return false;
        mPcm=pcm;mChannels=channels;mRate=rate;mFrames=static_cast<unsigned>(length/(channels*2));
        return true;
    }
    int Channels() const { return mPcm ? mChannels : mBink.Channels(); }
    int SampleRate() const { return mPcm ? mRate : mBink.SampleRate(); }
    unsigned TotalFrames() const { return mPcm ? mFrames : mBink.TotalFrames(); }
    int MaxBlockFrames() const { return mPcm ? 1024 : mBink.MaxBlockFrames(); }
    void Rewind() { if (mPcm) mPosition=0; else mBink.Rewind(); }
    int DecodeBlock(float* output)
    {
        if (!mPcm) return mBink.DecodeBlock(output);
        const unsigned frames=std::min(1024u,mFrames-mPosition);
        const auto* data=mPcm+size_t(mPosition)*mChannels*2;
        for (unsigned i=0;i<frames*mChannels;++i)
            output[i]=static_cast<int16_t>(read16(data+i*2))/32768.0f;
        mPosition+=frames;return frames;
    }
private:
    static unsigned read16(const unsigned char* p) { return unsigned(p[0]) | unsigned(p[1])<<8; }
    static unsigned read32(const unsigned char* p) { return read16(p) | read16(p+2)<<16; }
    BinkAudioDecoder mBink;
    const unsigned char* mPcm = nullptr;
    unsigned mPosition = 0, mFrames = 0;
    int mChannels = 0, mRate = 0;
};
