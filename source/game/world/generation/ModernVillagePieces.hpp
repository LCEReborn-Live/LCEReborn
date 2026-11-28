#ifndef GAME_WORLD_GENERATION_MODERNVILLAGEPIECES_HPP
#define GAME_WORLD_GENERATION_MODERNVILLAGEPIECES_HPP

#include <vector>

class Level;
class Random;
class StructurePiece;

namespace ModernVillagePieces
{
void registerPieces();
void generate(Level* level, Random& random, int chunkX, int chunkZ, int size, int worldWidth,
              std::vector<StructurePiece*>& pieces);
}

#endif
