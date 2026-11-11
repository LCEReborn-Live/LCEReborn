#include "game/network/ClientboundVoicePacket.hpp"

#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/network/PacketListener.hpp"

namespace
{
const int MAX_VOICE_BYTES = 1024;
}

ClientboundVoicePacket::ClientboundVoicePacket()
    : m_playerId(0), m_hasVoice(false), m_talking(false)
{
}

ClientboundVoicePacket::ClientboundVoicePacket(unsigned char playerId, bool hasVoice, bool talking,
                                               const std::vector<unsigned char>& audio)
    : m_playerId(playerId), m_hasVoice(hasVoice), m_talking(talking), m_audio(audio)
{
}

int ClientboundVoicePacket::getPacketId() { return 252; }

void ClientboundVoicePacket::read(DataInputStream* input)
{
    m_playerId = input->readUnsignedByte();
    m_hasVoice = input->readBoolean();
    m_talking = input->readBoolean();
    const int length = input->readVarInt();
    m_audio.clear();
    if (length < 0)
        return;
    if (length > MAX_VOICE_BYTES)
    {
        input->skipBytes(length);
        m_hasVoice = false;
        m_talking = false;
        return;
    }
    m_audio.resize(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i)
        m_audio[static_cast<size_t>(i)] = input->readUnsignedByte();
}

void ClientboundVoicePacket::write(DataOutputStream* output)
{
    output->writeByte(m_playerId);
    output->writeBoolean(m_hasVoice);
    output->writeBoolean(m_talking);
    output->writeVarInt(static_cast<int>(m_audio.size()));
    for (size_t i = 0; i < m_audio.size(); ++i)
        output->writeByte(m_audio[i]);
}

void ClientboundVoicePacket::handle(PacketListener* listener)
{
    listener->handleVoice(shared_from_this());
}

int ClientboundVoicePacket::getEstimatedSize() { return static_cast<int>(m_audio.size()) + 9; }
