#ifndef GAME_CLIENT_RENDER_CAMPFIRERENDERER_HPP
#define GAME_CLIENT_RENDER_CAMPFIRERENDERER_HPP

#include "game/client/render/BlockEntityRenderer.hpp"

class CampfireRenderer : public BlockEntityRenderer
{
public:
    void render(boost::shared_ptr<BlockEntity>, double, double, double, float, int, bool, float, bool) override;
};

#endif
