#ifndef GAME_NETWORK_SERVERBOUNDVOICEPACKET_HPP
#define GAME_NETWORK_SERVERBOUNDVOICEPACKET_HPP

#include <vector>

#include <boost/enable_shared_from_this.hpp>
#include "game/network/Packet.hpp"

class ServerboundVoicePacket : public Packet, public boost::enable_shared_from_this<ServerboundVoicePacket>
{
public:
    ServerboundVoicePacket();
    ServerboundVoicePacket(bool hasVoice, bool talking, const std::vector<unsigned char>& audio);

    virtual int getPacketId();
    virtual void read(DataInputStream* input);
    virtual void write(DataOutputStream* output);
    virtual void handle(PacketListener* listener);
    virtual int getEstimatedSize();

    static boost::shared_ptr<Packet> create() { return boost::shared_ptr<Packet>(new ServerboundVoicePacket()); }

    bool HasVoice() const { return m_hasVoice; }
    bool IsTalking() const { return m_talking; }
    const std::vector<unsigned char>& Audio() const { return m_audio; }

private:
    bool m_hasVoice;
    bool m_talking;
    std::vector<unsigned char> m_audio;
};

#endif
