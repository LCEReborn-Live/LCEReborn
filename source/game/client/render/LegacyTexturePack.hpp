#ifndef GAME_CLIENT_RENDER_LEGACYTEXTUREPACK_HPP
#define GAME_CLIENT_RENDER_LEGACYTEXTUREPACK_HPP

#include "game/client/render/DefaultTexturePack.hpp"

// The original built-in LCE textures, retained as an optional texture pack
// after the bundled 1.14 data becomes the default pack.
class LegacyTexturePack : public DefaultTexturePack
{
public:
    explicit LegacyTexturePack(unsigned int id);

    virtual std::wstring getName();
    virtual std::wstring getDesc1();
};

#endif
