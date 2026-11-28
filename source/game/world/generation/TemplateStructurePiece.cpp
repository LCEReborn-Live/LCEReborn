#include "game/world/generation/TemplateStructurePiece.hpp"

#include <boost/unordered_map.hpp>

#include "game/core/BoundingBox.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/Direction.hpp"
#include "game/core/Mirror.hpp"
#include "game/core/Rotation.hpp"
#include "game/world/Level.hpp"
#include "game/world/block/Block.hpp"
#include "game/world/block/Blocks.hpp"
#include "game/world/generation/StructureManager.hpp"
#include "game/world/generation/StructurePlaceSettings.hpp"
#include "game/world/generation/StructureTemplate.hpp"

static StructurePlaceSettings* DEFAULT_PLACE_SETTINGS = new StructurePlaceSettings();

TemplateStructurePiece::TemplateStructurePiece() : StructurePiece(), mTemplatePosition()
{
    mPlaceSettings = DEFAULT_PLACE_SETTINGS->setIgnoreEntities(true)->setIgnoreBlock(Blocks::air);
    mTemplate = NULL;
}

TemplateStructurePiece::TemplateStructurePiece(int genDepth) : StructurePiece(genDepth), mTemplatePosition()
{
    mPlaceSettings = DEFAULT_PLACE_SETTINGS->setIgnoreEntities(true)->setIgnoreBlock(Blocks::air);
    mTemplate = NULL;
}

TemplateStructurePiece::~TemplateStructurePiece()
{
    if (mPlaceSettings != DEFAULT_PLACE_SETTINGS)
        delete mPlaceSettings;
}

void TemplateStructurePiece::setBoundingBoxFromTemplate()
{
    setBoundingBox(mTemplate->getTransformedBounds(mTemplatePosition, mPlaceSettings->getMirror(),
                                                   mPlaceSettings->getRotation()));
}

void TemplateStructurePiece::setup(const StructureTemplate* templateObject, const BlockPos& templatePosition,
                                   StructurePlaceSettings* placeSettings)
{
    mTemplate = const_cast<StructureTemplate*>(templateObject);
    setOrientation(Direction::NORTH);
    mTemplatePosition = templatePosition;
    mPlaceSettings = placeSettings;
    setBoundingBoxFromTemplate();
}

void TemplateStructurePiece::addAdditionalSaveData(DataOutputStream* output)
{
    output->writeInt(mTemplatePosition.x);
    output->writeInt(mTemplatePosition.y);
    output->writeInt(mTemplatePosition.z);
}

void TemplateStructurePiece::readAdditionalSaveData(DataInputStream* input, StructureManager*)
{
    int x = input->readInt();
    int y = input->readInt();
    int z = input->readInt();
    mTemplatePosition = BlockPos(x, y, z);
}

bool TemplateStructurePiece::postProcess(Level* level, Random& random, BoundingBox* box)
{
    mPlaceSettings->setBoundingBox(box);
    mTemplate->placeInWorld(level, mTemplatePosition, mPlaceSettings, 18);
    StructureTemplate::MarkerMap markers = mTemplate->getMarkers(mTemplatePosition, mPlaceSettings);

    for (StructureTemplate::MarkerMap::const_iterator it = markers.begin(); it != markers.end(); ++it)
    {
        std::wstring marker = it->second;
        handleDataMarker(marker, it->first, level, random, box);
    }

    return true;
}

void TemplateStructurePiece::move(int x, int y, int z)
{
    StructurePiece::move(x, y, z);
    mTemplatePosition = mTemplatePosition.offset(x, y, z);
}

Rotation* TemplateStructurePiece::getRotation()
{
    return mPlaceSettings->getRotation();
}

BlockPos TemplateStructurePiece::getTemplatePosition()
{
    return mTemplatePosition;
}

StructureTemplate* TemplateStructurePiece::getStructureTemplate()
{
    return mTemplate;
}

StructurePlaceSettings* TemplateStructurePiece::getPlaceSettings()
{
    return mPlaceSettings;
}
