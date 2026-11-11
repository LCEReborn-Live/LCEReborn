#ifndef GAME_NETWORK_CLIENTBOUNDVOICEPACKET_HPP
#define GAME_NETWORK_CLIENTBOUNDVOICEPACKET_HPP

#include <vector>

#include <boost/enable_shared_from_this.hpp>
#include "game/network/Packet.hpp"

class ClientboundVoicePacket : public Packet, public boost::enable_shared_from_this<ClientboundVoicePacket>
{
public:
    ClientboundVoicePacket();
    ClientboundVoicePacket(unsigned char playerId, bool hasVoice, bool talking,
                           const std::vector<unsigned char>& audio);

    virtual int getPacketId();
    virtual void read(DataInputStream* input);
    virtual void write(DataOutputStream* output);
    virtual void handle(PacketListener* listener);
    virtual int getEstimatedSize();

    static boost::shared_ptr<Packet> create() { return boost::shared_ptr<Packet>(new ClientboundVoicePacket()); }

    unsigned char PlayerId() const { return m_playerId; }
    bool HasVoice() const { return m_hasVoice; }
    bool IsTalking() const { return m_talking; }
    const std::vector<unsigned char>& Audio() const { return m_audio; }

private:
    unsigned char m_playerId;
    bool m_hasVoice;
    bool m_talking;
    std::vector<unsigned char> m_audio;
};

#endif
