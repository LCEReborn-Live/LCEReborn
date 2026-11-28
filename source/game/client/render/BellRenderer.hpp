#ifndef GAME_CLIENT_RENDER_BELLRENDERER_HPP
#define GAME_CLIENT_RENDER_BELLRENDERER_HPP

#include "game/client/render/BlockEntityRenderer.hpp"
#include "game/client/render/Model.hpp"
#include "game/client/render/ModelPart.hpp"

class BellRenderer : public BlockEntityRenderer
{
public:
    BellRenderer();
    void render(boost::shared_ptr<BlockEntity>, double, double, double, float, int, bool, float, bool) override;

private:
    Model mModel;
    ModelPart mBell;
};

#endif
