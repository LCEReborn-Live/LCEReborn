#include "game/ui/UIScene_FurnaceMenu.hpp"

#include <cstring>

#include "game/core/StringIds.hpp"
#include "game/core/ExtraStrings.hpp"
#include "game/core/InteractionObject.hpp"
#include "game/client/render/Renderer.hpp"
#include "game/client/render/Tesselator.hpp"

#include "game/core/BufferBuilder.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/GlStateManager.hpp"
#include "game/core/L10N.hpp"
#include "game/core/Minecraft.hpp"
#include "game/item/Container.hpp"
#include "game/item/Inventory.hpp"
#include "game/item/Item.hpp"
#include "game/item/ItemInstance.hpp"
#include "game/item/Items.hpp"
#include "game/network/ClientboundContainerOpenPacket.hpp"
#include "game/minigame/Tutorial.hpp"
#include "game/ui/FurnaceMenu.hpp"
#include "game/ui/CartographyMenu.hpp"
#include "game/ui/GrindstoneMenu.hpp"
#include "game/ui/StonecutterMenu.hpp"
#include "game/ui/LoomMenu.hpp"
#include "game/ui/UIString.hpp"
#include "game/ui/fuiFile.hpp"
#include "game/ui/fuiRect.hpp"
#include "game/ui/fuiRenderNode.hpp"
#include "game/world/generation/MultiPlayerGameMode.hpp"
#include "game/world/generation/MultiplayerLocalPlayer.hpp"
#include "game/ui/UILayer.hpp"
#include "game/world/block/Blocks.hpp"

extern CConsoleMinecraftApp gApp;

namespace
{
constexpr int HintSize = 12;
constexpr const char* LoomHintNames[] = {"slot_0", "slot_1", "slot_2"};
constexpr const char* LoomHints[][HintSize] = {
    {"............", "..########..", "....####....", "....#..#....", "....#..#....", "....#..#....",
     "....#..#....", "....#..#....", "....#..#....", "....####....", ".....#......", "............"},
    {"............", "............", ".......#....", ".....#......", "....###.....", "...#...#....",
     "..#.....#...", "..#......#..", "..########..", "...######...", "............", "............"},
    {"............", "............", "....######..", "...##...##..", "...#....#...", "...#.#..#...",
     "...#.##.#...", "...#.#..#...", "...#...##...", "..######....", "............", "............"},
};
static_assert(sizeof(LoomHints) / sizeof(*LoomHints) == LoomMenu::InputCount
    && sizeof(LoomHintNames) / sizeof(*LoomHintNames) == LoomMenu::InputCount, "Expected one hint per loom input");

void drawLoomHint(const fuiRect& rect, int slot)
{
    const float scaleX = (rect.xMax - rect.xMin) / HintSize;
    const float scaleY = (rect.yMax - rect.yMin) / HintSize;
    auto* builder = Tesselator::getInstance()->getBuilder();
    GlStateManager::disableTexture();
    builder->begin();
    builder->color(104, 104, 104);
    for (int row = 0; row < HintSize; ++row)
    {
        const auto* pixels = LoomHints[slot][row];
        const float top = rect.yMin + row * scaleY;
        const float bottom = top + scaleY;
        for (int column = 0; column < HintSize;)
        {
            if (pixels[column] != '#')
            {
                ++column;
                continue;
            }
            const float left = rect.xMin + column * scaleX;
            do { ++column; } while (column < HintSize && pixels[column] == '#');
            const float right = rect.xMin + column * scaleX;
            builder->vertex(right, top, 0);
            builder->vertex(left, top, 0);
            builder->vertex(left, bottom, 0);
            builder->vertex(right, bottom, 0);
        }
    }
    builder->end();
    GlStateManager::enableTexture();
}
}

struct FurnaceMenuData
{
    boost::shared_ptr<Inventory> inventory;
    boost::shared_ptr<Container> container;
    int player;
    bool splitscreen;
};

UIScene_FurnaceMenu::UIScene_FurnaceMenu(int type, void* data, UILayer* layer)
    : UIScene_AbstractContainerMenu(type, layer),
      m_container(),
      m_fuelSlot(),
      m_ingredientSlot(),
      m_resultSlot(),
      m_furnaceLabel(),
      m_ingredientLabel(),
      m_fuelLabel(),
      m_furnaceFire(),
      m_furnaceArrow()
{
    FurnaceMenuData* input = static_cast<FurnaceMenuData*>(data);
    m_container = input->container;
    auto* interaction = dynamic_cast<InteractionObject*>(m_container.get());
    const int kind = interaction ? interaction->getTypeId(false) : -1;
    mCartography = kind == ClientboundContainerOpenPacket::CARTOGRAPHY;
    mGrindstone = kind == ClientboundContainerOpenPacket::GRINDSTONE;
    mStonecutter = kind == ClientboundContainerOpenPacket::STONECUTTER;
    mLoom = kind == ClientboundContainerOpenPacket::LOOM;
    initialiseMovie();

    const std::wstring name = m_container->getName();
    int defaultName = IDS_TILE_FURNACE;
    switch (FurnaceMenu::getKind(m_container))
    {
    case FurnaceKind::Smoker: defaultName = IDS_LCE_TILE_SMOKER; break;
    case FurnaceKind::BlastFurnace: defaultName = IDS_LCE_TILE_BLAST_FURNACE; break;
    default: break;
    }
    if (mCartography)
        defaultName = IDS_LCE_TILE_CARTOGRAPHY_TABLE;
    if (mGrindstone)
        defaultName = IDS_LCE_TILE_GRINDSTONE;
    if (mStonecutter)
        defaultName = IDS_LCE_TILE_STONECUTTER;
    if (mLoom)
        defaultName = IDS_LCE_LOOM;
    m_furnaceLabel.init(UIString(name.empty() ? L10N::GetString(defaultName) : name));

    m_ingredientLabel.init(UIString(L10N::GetString(mLoom ? IDS_LCE_LOOM_INPUTS
                                   : mCartography ? IDS_LCE_CARTOGRAPHY_MAP : IDS_INGREDIENT)));
    const int secondLabel = m_useStonecutterMovie ? IDS_LCE_STONECUTTER_RECIPES : IDS_LCE_STONECUTTER_NEXT;
    m_fuelLabel.init(UIString(L10N::GetString(mLoom ? IDS_LCE_LOOM_PATTERNS : mCartography ? IDS_LCE_CARTOGRAPHY_MATERIAL
                                   : mStonecutter ? secondLabel : mGrindstone ? IDS_INGREDIENT : IDS_FUEL)));
    if (!m_useWorkstationMovie && !m_useStonecutterMovie && !mLoom)
    {
        m_furnaceFire.init(UIString(L""), 0, 0, 0, 0);
        m_furnaceArrow.init(UIString(L""), 0, 0, 0, 0);
    }

    if (Minecraft::GetInstance()->mGameModes[input->player] != 0)
    {
        Tutorial* tutorial = Minecraft::GetInstance()->mGameModes[input->player]->getTutorial();
        if (tutorial != 0)
        {
            m_previousTutorialState = tutorial->currentState;
            if (!mCartography && !mGrindstone && !mStonecutter && !mLoom)
                tutorial->changeTutorialState(eTutorial_State_FurnaceMenu, this);
        }
    }

    AbstractContainerMenu* menu;
    if (mCartography)
        menu = new CartographyMenu(input->inventory,
                                  Minecraft::GetInstance()->mLocalPlayers[input->player]->mLevel, BlockPos());
    else if (mGrindstone)
        menu = new GrindstoneMenu(input->inventory,
                                 Minecraft::GetInstance()->mLocalPlayers[input->player]->mLevel, BlockPos());
    else if (mStonecutter)
        menu = new StonecutterMenu(input->inventory,
                                   Minecraft::GetInstance()->mLocalPlayers[input->player]->mLevel, BlockPos());
    else if (mLoom)
        menu = new LoomMenu(input->inventory, Minecraft::GetInstance()->mLocalPlayers[input->player]->mLevel, BlockPos());
    else
        menu = new FurnaceMenu(input->inventory, input->container);
    Initialize(false, input->player, menu, true, mLoom ? LoomMenu::ResultSlot + 1 : 3, IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar,
               IUIScene_AbstractContainerMenu::ESceneSection_FurnaceMax, false);
    m_fuelSlot.addSlots(getSectionStartOffset(ESceneSection_FurnaceFuel), getSectionColumns(ESceneSection_FurnaceFuel));
    m_ingredientSlot.addSlots(0, getSectionColumns(ESceneSection_FurnaceIngredient));
    m_resultSlot.addSlots(getSectionStartOffset(ESceneSection_FurnaceResult), 1);
    gApp.SetRichPresenceContext(m_playerId, 6);
    delete input;
}

UIScene_FurnaceMenu::~UIScene_FurnaceMenu()
{
}

int UIScene_FurnaceMenu::getSectionColumns(IUIScene_AbstractContainerMenu::ESceneSection section)
{
    int columns = 0;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        columns = 9;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        columns = mLoom ? LoomMenu::PatternSlotCount : m_useStonecutterMovie ? StonecutterMenu::RecipeSlotCount : 1;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        columns = mLoom ? LoomMenu::InputCount : 1;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        columns = 1;
        break;
    default:
        break;
    }
    return columns;
}

int UIScene_FurnaceMenu::getSectionRows(IUIScene_AbstractContainerMenu::ESceneSection section)
{
    int rows = 0;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        rows = GetInventoryRows();
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        rows = 1;
        break;
    default:
        break;
    }
    return rows;
}

unsigned int UIScene_FurnaceMenu::GetSectionAndSlotInDirection(IUIScene_AbstractContainerMenu::ESceneSection section,
                                                               IUIScene_AbstractContainerMenu::ETapState state, int* x,
                                                               int* y)
{
    IUIScene_AbstractContainerMenu::ESceneSection target = section;
    int horizontalOffset = 0;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        if (state == IUIScene_AbstractContainerMenu::ETapState_Up)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar;
            horizontalOffset = -7;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Down)
        {
            if (mLoom)
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel;
            else if (m_smallInventory)
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar;
            else
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory;
            horizontalOffset = -7;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Left
                 || state == IUIScene_AbstractContainerMenu::ETapState_Right)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient;
        }
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        if (state == IUIScene_AbstractContainerMenu::ETapState_Up)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar;
            horizontalOffset = -3;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Down)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Left
                 || state == IUIScene_AbstractContainerMenu::ETapState_Right)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult;
        }
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        if (state == IUIScene_AbstractContainerMenu::ETapState_Down)
        {
            if (m_smallInventory)
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar;
            else
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory;
            horizontalOffset = -3;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Up)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Left
                 || state == IUIScene_AbstractContainerMenu::ETapState_Right)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult;
        }
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        if (state == IUIScene_AbstractContainerMenu::ETapState_Down)
        {
            target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar;
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Up)
        {
            if (*x < 6)
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel;
            else
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult;
        }
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
        if (state == IUIScene_AbstractContainerMenu::ETapState_Up)
        {
            if (m_smallInventory)
            {
                if (*x < 6)
                    target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel;
                else
                    target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult;
            }
            else
            {
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory;
            }
        }
        else if (state == IUIScene_AbstractContainerMenu::ETapState_Down)
        {
            if (*x < 6)
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient;
            else
                target = IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult;
        }
        break;
    default:
        break;
    }
    updateSlotPosition(section, target, state, x, y, horizontalOffset, 0);
    return static_cast<unsigned int>(target);
}

void UIScene_FurnaceMenu::GetPositionOfSection(IUIScene_AbstractContainerMenu::ESceneSection section,
                                               _UIVec2D* position)
{
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        position->x = static_cast<float>(m_resultSlot.getXPos());
        position->y = static_cast<float>(m_resultSlot.getYPos());
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        position->x = static_cast<float>(m_fuelSlot.getXPos());
        position->y = static_cast<float>(m_fuelSlot.getYPos());
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        position->x = static_cast<float>(m_ingredientSlot.getXPos());
        position->y = static_cast<float>(m_ingredientSlot.getYPos());
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        position->x = static_cast<float>(m_inventoryList.getXPos());
        position->y = static_cast<float>(m_inventoryList.getYPos());
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
        position->x = static_cast<float>(m_hotbarList.getXPos());
        position->y = static_cast<float>(m_hotbarList.getYPos());
        break;
    default:
        break;
    }
}

void UIScene_FurnaceMenu::GetItemScreenData(IUIScene_AbstractContainerMenu::ESceneSection section, int slot,
                                            _UIVec2D* position, _UIVec2D* size)
{
    _UIVec2D controlSize;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        controlSize.x = m_resultSlot.getWidth();
        controlSize.y = m_resultSlot.getHeight();
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        controlSize.x = m_fuelSlot.getWidth();
        controlSize.y = m_fuelSlot.getHeight();
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        controlSize.x = m_ingredientSlot.getWidth();
        controlSize.y = m_ingredientSlot.getHeight();
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        controlSize.x = m_inventoryList.getWidth();
        controlSize.y = m_inventoryList.getHeight();
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
        controlSize.x = m_hotbarList.getWidth();
        controlSize.y = m_hotbarList.getHeight();
        break;
    default:
        break;
    }
    const int rows = getSectionRows(section);
    const int columns = getSectionColumns(section);
    size->x = controlSize.x / static_cast<float>(columns);
    size->y = controlSize.y / static_cast<float>(rows);
    int column = slot % columns;
    int row = slot / columns;
    position->x = static_cast<float>(column) * size->x;
    position->y = static_cast<float>(row) * size->y;
}

int UIScene_FurnaceMenu::getSectionStartOffset(IUIScene_AbstractContainerMenu::ESceneSection section)
{
    int offset = 0;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
        offset = mLoom ? LoomMenu::ResultSlot + 28 : 30;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        offset = mLoom ? LoomMenu::ResultSlot + 1 : 3;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        offset = 0;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        offset = mLoom ? LoomMenu::PatternStart : m_useStonecutterMovie ? StonecutterMenu::RecipeSlotStart : 1;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        offset = mLoom ? LoomMenu::ResultSlot : 2;
        break;
    default:
        break;
    }
    return offset;
}

void UIScene_FurnaceMenu::setSectionSelectedSlot(IUIScene_AbstractContainerMenu::ESceneSection section, int slot,
                                                 int direction, bool wrap)
{
    const int highlightSlot = direction * getSectionColumns(section) + slot;
    UIControl_SlotList* list = 0;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        list = &m_resultSlot;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        list = &m_fuelSlot;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        list = &m_ingredientSlot;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        list = &m_inventoryList;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
        list = &m_hotbarList;
        break;
    default:
        break;
    }
    if (wrap)
        list->setHighlightSlot(highlightSlot);
}

unsigned int UIScene_FurnaceMenu::mapElementsAndNames()
{
    UIScene_AbstractContainerMenu::mapElementsAndNames();
    if (m_useStonecutterMovie || mLoom)
    {
        auto* panel = m_mainPanel.getFuiRenderNode();
        m_ingredientSlot.setupControl(this, panel, mLoom ? "Inputs" : "Material");
        m_fuelSlot.setupControl(this, panel, mLoom ? "Patterns" : "Recipes");
        m_resultSlot.setupControl(this, panel, "Output");
        m_furnaceLabel.setupControl(this, panel, "Title");
        m_ingredientLabel.setupControl(this, panel, mLoom ? "InputsLabel" : "MaterialLabel");
        m_fuelLabel.setupControl(this, panel, mLoom ? "PatternsLabel" : "RecipesLabel");
        m_controls.insert(m_controls.end(),
                          {&m_ingredientSlot, &m_fuelSlot, &m_resultSlot, &m_furnaceLabel, &m_ingredientLabel, &m_fuelLabel});
        return 1;
    }
    if (m_useWorkstationMovie)
    {
        auto* panel = m_mainPanel.getFuiRenderNode();
        m_ingredientSlot.setupControl(this, panel, "Ingredient");
        m_fuelSlot.setupControl(this, panel, "Ingredient2");
        m_resultSlot.setupControl(this, panel, "Result");
        m_furnaceLabel.setupControl(this, panel, "TitleText");
        for (UIControl* control : {static_cast<UIControl*>(&m_ingredientSlot), static_cast<UIControl*>(&m_fuelSlot),
                                   static_cast<UIControl*>(&m_resultSlot), static_cast<UIControl*>(&m_furnaceLabel)})
            m_controls.push_back(control);
        if (mGrindstone)
        {
            m_grindstoneCross.setupControl(this, panel, "GrindstoneCross");
            m_grindstoneCross.setVisible(false);
            return 1;
        }
        m_mapName.setupControl(this, panel, "MapNameText");
        m_mapStatus.setupControl(this, panel, "MapStatusText");
        m_controls.push_back(&m_mapName);
        m_controls.push_back(&m_mapStatus);
        const char* previews[] = {"Map_Map", "Map_Zoom", "Map_Copy", "Map_Locked", "Map_Empty"};
        for (size_t i = 0; i < m_mapPreviews.size(); ++i)
        {
            m_mapPreviews[i].setupControl(this, panel, previews[i]);
            m_mapPreviews[i].setVisible(i == 0);
        }
        UIControl nameEditor;
        if (nameEditor.setupControl(this, panel, "MapTextInput"))
            nameEditor.setVisible(false);
        m_mapName.init(UIString(L""));
        m_mapStatus.init(UIString(L""));
        return 1;
    }
    fuiRenderNode* currentNode = m_movie->getRootNode();
    {
        fuiRenderNode* parentNode = currentNode;
        currentNode = m_mainPanel.getFuiRenderNode();
        {
            m_ingredientSlot.setupControl(this, currentNode, std::string("Ingredient"));
            m_controls.push_back(&m_ingredientSlot);
        }
        {
            m_fuelSlot.setupControl(this, currentNode, std::string("Fuel"));
            m_controls.push_back(&m_fuelSlot);
        }
        {
            m_resultSlot.setupControl(this, currentNode, std::string("Result"));
            m_controls.push_back(&m_resultSlot);
        }
        {
            m_furnaceLabel.setupControl(this, currentNode, std::string("Furnace_text"));
            m_controls.push_back(&m_furnaceLabel);
        }
        {
            m_ingredientLabel.setupControl(this, currentNode, std::string("Ingredient_Label"));
            m_controls.push_back(&m_ingredientLabel);
        }
        {
            m_fuelLabel.setupControl(this, currentNode, std::string("Fuel_Label"));
            m_controls.push_back(&m_fuelLabel);
        }
        {
            m_furnaceFire.setupControl(this, currentNode, std::string("FurnaceFire"));
            if (mCartography || mGrindstone || mStonecutter)
                m_furnaceFire.setVisible(false);
            m_controls.push_back(&m_furnaceFire);
        }
        {
            m_furnaceArrow.setupControl(this, currentNode, std::string("FurnaceArrow"));
            m_controls.push_back(&m_furnaceArrow);
        }
        currentNode = parentNode;
    }
    return 1;
}

std::wstring UIScene_FurnaceMenu::getMoviePath()
{
    const bool split = gApp.GetLocalPlayerCount() > 1;
    if (mLoom)
        return split ? L"LoomMenuSplit" : L"LoomMenu";
    if (mStonecutter)
    {
        const std::wstring movie = split ? L"StonecutterMenuSplit" : L"StonecutterMenu";
        m_useStonecutterMovie = gApp.hasArchiveFile(movie + (gRenderer->IsWidescreen() ? L"720.fui" : L"480.fui"), false);
        if (m_useStonecutterMovie)
            return movie;
    }
    std::wstring workstation = mGrindstone ? L"GrindstoneMenu" : L"CartographyMenu";
    if (split)
        workstation += L"Split";
    m_useWorkstationMovie = (mCartography || mGrindstone) && gApp.hasArchiveFile(
        workstation + (gRenderer->IsWidescreen() ? L"720.fui" : L"480.fui"), false);
    if (m_useWorkstationMovie)
        return workstation;
    return split ? L"FurnaceMenuSplit" : L"FurnaceMenu";
}

void UIScene_FurnaceMenu::handleReload()
{
    Initialize(true, m_playerId, m_menu, true, mLoom ? LoomMenu::ResultSlot + 1 : 3, IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar,
               IUIScene_AbstractContainerMenu::ESceneSection_FurnaceMax, false);
    m_fuelSlot.addSlots(getSectionStartOffset(ESceneSection_FurnaceFuel), getSectionColumns(ESceneSection_FurnaceFuel));
    m_ingredientSlot.addSlots(0, getSectionColumns(ESceneSection_FurnaceIngredient));
    m_resultSlot.addSlots(getSectionStartOffset(ESceneSection_FurnaceResult), 1);
    updateWorkstationMovie();
}

void UIScene_FurnaceMenu::updateWorkstationMovie()
{
    if (!m_useWorkstationMovie)
        return;
    if (mGrindstone)
    {
        const bool hasInput = m_menu->getSlot(0)->hasItem() || m_menu->getSlot(1)->hasItem();
        m_grindstoneCross.setVisible(hasInput && !m_menu->getSlot(2)->hasItem());
    }
    else
        updateMapPreview();
}

void UIScene_FurnaceMenu::updateMapPreview()
{
    const auto map = m_menu->getSlot(0)->getItem();
    const auto material = m_menu->getSlot(1)->getItem();
    const auto result = m_menu->getSlot(2)->getItem();
    size_t preview = 0;
    if (result->getItem() == Items::map)
        preview = 4;
    else if (result->getItem() == Items::filledMap)
    {
        if (material->getItem() == Items::paper)
            preview = 1;
        else if (material->getItem() == Items::map)
            preview = 2;
        else if (material->getItem() == Item::byBlock(Blocks::glass_pane))
            preview = 3;
    }
    for (size_t i = 0; i < m_mapPreviews.size(); ++i)
        m_mapPreviews[i].setVisible(i == preview);
    const int labels[] = {0, IDS_LCE_CARTOGRAPHY_ZOOM, IDS_LCE_CARTOGRAPHY_COPY,
                          IDS_LCE_CARTOGRAPHY_LOCK, IDS_LCE_CARTOGRAPHY_EMPTY};
    m_mapStatus.setLabel(UIString(preview ? L10N::GetString(labels[preview]) : L""), true, false);
    if (map->getItem() == Items::filledMap)
        m_mapName.setLabel(UIString(map->getHoverName()), true, false);
    else
        m_mapName.setLabel(UIString(L""), true, false);
}

void UIScene_FurnaceMenu::tick()
{
    if (mLoom)
    {
        const int selected = static_cast<LoomMenu*>(m_menu)->getSelectedPattern();
        for (int i = 0; i < LoomMenu::PatternsPerPage; ++i)
            m_fuelSlot.setSlotHighlighted(i, i == selected);
    }
    if (m_useStonecutterMovie)
    {
        const int selected = static_cast<StonecutterMenu*>(m_menu)->getSelectedRecipe();
        for (int i = 0; i < StonecutterMenu::RecipeSlotCount; ++i)
            m_fuelSlot.setSlotHighlighted(i, i == selected);
    }
    if (mCartography || mGrindstone || mStonecutter || mLoom)
    {
        updateWorkstationMovie();
        UIScene_AbstractContainerMenu::tick();
        return;
    }
    if (m_furnaceFire.getMax() != m_container->getContainerData(1))
    {
        m_furnaceFire.setMax(m_container->getContainerData(1));
        m_furnaceFire.ReInit();
    }
    if (m_furnaceArrow.getMax() != m_container->getContainerData(3))
    {
        m_furnaceArrow.setMax(m_container->getContainerData(3));
        m_furnaceArrow.ReInit();
    }
    m_furnaceFire.setProgress(m_container->getContainerData(0));
    m_furnaceArrow.setProgress(m_container->getContainerData(2));
    UIScene_AbstractContainerMenu::tick();
}

void UIScene_FurnaceMenu::customDraw(const char* name, fuiRect* rect)
{
    if (mLoom)
    {
        const auto* minecraft = Minecraft::GetInstance();
        if (minecraft->mLocalPlayers[m_playerId] && minecraft->mGameModes[m_playerId])
        {
            for (int slot = 0; slot < LoomMenu::InputCount; ++slot)
            {
                if (std::strcmp(name, LoomHintNames[slot]) == 0 && !m_menu->getSlot(slot)->hasItem()
                    && m_menu->GetQuickCraftItem(m_playerId, slot)->isEmpty())
                {
                    m_slotDrawPending = true;
                    drawLoomHint(*rect, slot);
                    return;
                }
            }
        }
    }
    UIScene_AbstractContainerMenu::customDraw(name, rect);
}

UIControl* UIScene_FurnaceMenu::getSection(IUIScene_AbstractContainerMenu::ESceneSection section)
{
    UIControl* control = 0;
    switch (section)
    {
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceResult:
        control = &m_resultSlot;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel:
        control = &m_fuelSlot;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceIngredient:
        control = &m_ingredientSlot;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceInventory:
        control = &m_inventoryList;
        break;
    case IUIScene_AbstractContainerMenu::ESceneSection_FurnaceHotbar:
        control = &m_hotbarList;
        break;
    default:
        break;
    }
    return control;
}

void UIScene_FurnaceMenu::handleSectionClick(IUIScene_AbstractContainerMenu::ESceneSection)
{
}

void UIScene_FurnaceMenu::handleSlotListClicked(IUIScene_AbstractContainerMenu::ESceneSection section, int buttonNum,
                                               int quickKeyHeld)
{
    if ((!mStonecutter && !mLoom) || section != IUIScene_AbstractContainerMenu::ESceneSection_FurnaceFuel)
    {
        UIScene_AbstractContainerMenu::handleSlotListClicked(section, buttonNum, quickKeyHeld);
        return;
    }
    auto* minecraft = Minecraft::GetInstance();
    auto player = minecraft->mLocalPlayers[m_playerId];
    const int recipe = mLoom ? getCurrentIndex(section) - LoomMenu::PatternStart
        : m_useStonecutterMovie ? getCurrentIndex(section) - StonecutterMenu::RecipeSlotStart
                                            : StonecutterMenu::NextRecipe;
    if (m_menu->clickMenuButton(player, recipe))
        minecraft->mGameModes[m_playerId]->handleInventoryButtonClick(m_menu->m_containerId, recipe);
}

int UIScene_FurnaceMenu::getSceneType()
{
    return EUIScene_FurnaceMenu;
}
