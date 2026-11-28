#include "game/client/render/CampfireSmokeParticle.hpp"

#include <algorithm>

#include "game/client/render/ParticleEngine.hpp"
#include "game/core/EntityType.hpp"

namespace
{
constexpr int FrameSize = 16;
constexpr int FrameCount = 12;
constexpr int TextureHeight = FrameSize * FrameCount;
}

CampfireSmokeParticle::CampfireSmokeParticle(Level* level, double x, double y, double z, bool signal)
    : Particle(level, x, y, z)
{
    lifetime = (signal ? 140 : 64) + random.nextInt(25);
    size = 5.0f + random.nextFloat();
    xd = (random.nextDouble() - .5) * .015;
    yd = signal ? .12 : .075;
    zd = (random.nextDouble() - .5) * .015;
    setAlpha(.9f);
}

unsigned long long CampfireSmokeParticle::GetType() { return ParticleKind::PARTICLE | 0x30; }
int CampfireSmokeParticle::getParticleTexture() { return ParticleEngine::CampfireSmokeTexture; }

void CampfireSmokeParticle::tick()
{
    xo = x;
    yo = y;
    zo = z;
    if (++age >= lifetime)
    {
        remove();
        return;
    }
    move(xd, yd, zd);
    if (lifetime - age < 24)
        setAlpha(.9f * (lifetime - age) / 24.f);
}

void CampfireSmokeParticle::render(BufferBuilder* buffer, const boost::shared_ptr<Entity>& camera, float partialTick,
                                   float xa, float ya, float za, float xa2, float za2)
{
    const int frame = std::min(FrameCount - 1, int((age + partialTick) * FrameCount / lifetime));
    uvs = TextureUVCoordinateSet(0, float(frame * FrameSize) / TextureHeight,
                                 1, float((frame + 1) * FrameSize) / TextureHeight, FrameSize, TextureHeight);
    Particle::render(buffer, camera, partialTick, xa, ya, za, xa2, za2);
}

Particle* CampfireSmokeParticle::Provider::createParticle(ePARTICLE_TYPE type, Level* level, double x, double y,
                                                          double z, double, double, double, arrayWithLength<int>)
{
    return new CampfireSmokeParticle(level, x, y, z, type == ePARTICLE_CAMPFIRE_SIGNAL_SMOKE);
}
