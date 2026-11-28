#include "game/world/block/IdMapper.hpp"

#include "game/world/block/Block.hpp"
#include "game/world/block/BlockState.hpp"

#include <string.h>

IdMapper<const BlockState*>::IdMapper()
{
    memset(mBlockStates, 0, sizeof(mBlockStates));
}

int IdMapper<const BlockState*>::getId(const BlockState* state)
{
    return state->mElementId;
}

const BlockState* IdMapper<const BlockState*>::byId(int id)
{
    if (id < 0 || id >= BLOCK_STATE_COUNT)
        return mBlockStates[0];
    const BlockState* state = mBlockStates[id];
    if (state == NULL)
    {
        Block* block = Block::byId(id >> 4);
        state = block ? block->getBlockState(id & 0xF) : mBlockStates[0];
    }

    return state;
}

void IdMapper<const BlockState*>::addMapping(BlockState* state, int id)
{
    state->mElementId = id;
    mBlockStates[id] = state;
}
