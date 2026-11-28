#ifndef GAME_WORLD_GENERATION_VILLAGETEMPLATEPIECES_HPP
#define GAME_WORLD_GENERATION_VILLAGETEMPLATEPIECES_HPP

#include <vector>

class GenericOverworldLevelSource;
class Level;
class Random;
class StructurePiece;

namespace VillageTemplatePieces
{
void registerPieces();
bool generate(Level* level, GenericOverworldLevelSource* terrain, Random& random, int chunkX, int chunkZ,
              int size, int worldWidth, std::vector<StructurePiece*>& pieces);
}

#endif
