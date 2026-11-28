#ifndef GAME_CLIENT_RENDER_CAMPFIRESMOKEPARTICLE_HPP
#define GAME_CLIENT_RENDER_CAMPFIRESMOKEPARTICLE_HPP

#include "game/client/render/Particle.hpp"
#include "game/client/render/ParticleProvider.hpp"

class CampfireSmokeParticle : public Particle
{
public:
    CampfireSmokeParticle(Level* level, double x, double y, double z, bool signal);
    unsigned long long GetType() override;
    int getParticleTexture() override;
    bool isSemiTransparent() override { return true; }
    void tick() override;
    void render(BufferBuilder*, const boost::shared_ptr<Entity>&, float, float, float, float, float, float) override;

    class Provider : public ParticleProvider
    {
    public:
        Particle* createParticle(ePARTICLE_TYPE, Level*, double, double, double, double, double, double,
                                 arrayWithLength<int>) override;
    };
};

#endif
