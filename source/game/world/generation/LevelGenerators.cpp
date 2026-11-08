#include "game/world/generation/LevelGenerators.hpp"

#include <algorithm>

#include "game/core/DLCPack.hpp"
#include "game/world/LevelGenerationOptions.hpp"

LevelGenerators::LevelGenerators()
{
}

void LevelGenerators::removeLevelGenerator(LevelGenerationOptions* levelGenerator)
{
    std::vector<LevelGenerationOptions*>::iterator it;
    while ((it = std::find(mLevelGenerators.begin(), mLevelGenerators.end(), levelGenerator)) != mLevelGenerators.end())
    {
        mLevelGenerators.erase(it);
    }
}

std::vector<LevelGenerationOptions*>* LevelGenerators::getLevelGenerators()
{
    return &mLevelGenerators;
}

bool LevelGenerators::SortLevelGenerators(const LevelGenerationOptions* first, const LevelGenerationOptions* second)
{
    unsigned int firstPackId = first->getPack() != NULL ? first->getPack()->GetPackId() : 0;
    unsigned int secondPackId = second->getPack() != NULL ? second->getPack()->GetPackId() : 0;

    if (firstPackId == 4)
    {
        unsigned int themeId = first->getThemeId();
        if (themeId == 0 || themeId == static_cast<unsigned int>(-1))
            return true;
    }

    if (secondPackId == 4)
    {
        unsigned int themeId = second->getThemeId();
        if (themeId == 0 || themeId == static_cast<unsigned int>(-1))
            return false;
    }

    return firstPackId < secondPackId;
}
