#pragma once

#include <map>

#include "game/core/Boat.hpp"
#include "game/entity/mob/Mob.hpp"
#include "game/network/ClientboundSetEntityLinkPacket.hpp"
#include "game/world/Level.hpp"

// Entity spawn packets can reach the client after their leash packets. Keep
// the latest relation per source until both endpoints have been registered.
// This state belongs to one connection and is cleared on dimension changes.
class PendingEntityLinks
{
public:
    void receive(Level& level, const boost::shared_ptr<ClientboundSetEntityLinkPacket>& packet)
    {
        mPending.erase(packet->getSourceId());
        if (!apply(level, packet))
            mPending[packet->getSourceId()] = packet;
    }

    void entityAdded(Level& level, int entityId)
    {
        for (auto it = mPending.begin(); it != mPending.end();)
        {
            const auto& packet = it->second;
            if ((packet->getSourceId() == entityId || packet->getDestId() == entityId) && apply(level, packet))
                it = mPending.erase(it);
            else
                ++it;
        }
    }

    void entityRemoved(int entityId)
    {
        for (auto it = mPending.begin(); it != mPending.end();)
        {
            if (it->first == entityId || it->second->getDestId() == entityId)
                it = mPending.erase(it);
            else
                ++it;
        }
    }

    void clear() { mPending.clear(); }

private:
    static bool apply(Level& level, const boost::shared_ptr<ClientboundSetEntityLinkPacket>& packet)
    {
        const auto source = level.getEntity(packet->getSourceId());
        const auto holder = level.getEntity(packet->getDestId());
        if (!source || (!holder && packet->getDestId() >= 0))
            return false;
        if (const auto mob = boost::dynamic_pointer_cast<Mob>(source))
        {
            if (holder)
                mob->setLeashedTo(holder, false);
            else
                mob->dropLeash(false, false);
        }
        else if (const auto boat = boost::dynamic_pointer_cast<Boat>(source))
        {
            if (holder)
                boat->setLeashedTo(holder, false);
            else
                boat->dropLeash(false, false);
        }
        return true;
    }

    std::map<int, boost::shared_ptr<ClientboundSetEntityLinkPacket>> mPending;
};
