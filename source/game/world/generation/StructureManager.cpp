#include "game/world/generation/StructureManager.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>

#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/NbtIo.hpp"
#include "game/core/SharedConstants.hpp"
#include "game/core/StringConversion.hpp"
#include "game/core/StringUtils.hpp"
#include "game/data/CompoundTag.hpp"
#include "game/data/DataFixerUpper.hpp"
#include "game/world/generation/StructureTemplate.hpp"
#include "game/world/generation/NativeVillageRegistry.hpp"

#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
#include "platform/pc/Log.hpp"
#include "platform/pc/PcPaths.hpp"

namespace
{
const std::wstring localVillagePrefix = L"LocalVillage/";
constexpr Logger sLog("VillageTemplates");

bool isPoolId(const std::wstring& name)
{
    if (name.empty() || name.size() > 192 || name.front() == L'/' || name.back() == L'/'
        || name.find(L"//") != std::wstring::npos)
        return false;
    return std::all_of(name.begin(), name.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9')
            || ch == L'_' || ch == L'-' || ch == L'/';
    });
}

bool isBiomeId(const std::wstring& name)
{
    return name == L"*" || name == L"plains" || name == L"desert" || name == L"savanna"
        || name == L"taiga" || name == L"snowy";
}

int readWeight(const std::wstring& text)
{
    if (text.empty() || text.size() > 7)
        return 0;
    int result = 0;
    for (const auto ch : text)
    {
        if (ch < L'0' || ch > L'9')
            return 0;
        result = result * 10 + (ch - L'0');
    }
    return result <= 1000000 ? result : 0;
}

std::wstring templateId(const std::wstring& file)
{
    if (file.size() <= 4)
        return {};
    auto extension = file.substr(file.size() - 4);
    for (auto& ch : extension)
        if (ch >= L'A' && ch <= L'Z')
            ch += L'a' - L'A';
    return extension == L".nbt" ? file.substr(0, file.size() - 4) : std::wstring{};
}
}

class LocalVillageCatalog
{
public:
    explicit LocalVillageCatalog(const std::wstring& root)
    {
        auto files = PcListVillageTemplates(root);
        std::sort(files.begin(), files.end());
        for (const auto& file : files)
        {
            const auto id = templateId(file);
            if (id.empty() || id.size() > 2048)
            {
                LOG_WARN(sLog, "Skipping template with an invalid relative path: %s", convWstringToString(file).c_str());
                continue;
            }
            if (mTemplates.find(id) != mTemplates.end())
            {
                LOG_WARN(sLog, "Skipping duplicate template ID: %s", convWstringToString(file).c_str());
                continue;
            }
            auto bytes = PcReadVillageTemplate(root, file);
            const arrayWithLength<unsigned char> view(bytes.data(), static_cast<unsigned int>(bytes.size()));
            std::unique_ptr<CompoundTag> tag(NbtIo::readChecked(view));
            auto structure = std::make_unique<StructureTemplate>();
            std::wstring problem;
            if (!tag || !structure->loadLocalVillage(tag.get(), problem))
            {
                if (!tag)
                    problem = L"NBT is unreadable, malformed or too large";
                LOG_WARN(sLog, "Skipping %s: %s", convWstringToString(file).c_str(), convWstringToString(problem).c_str());
                continue;
            }
            mTemplates.emplace(id, std::move(structure));

            const auto first = id.find(L'/');
            const auto second = first == std::wstring::npos ? first : id.find(L'/', first + 1);
            if (second == std::wstring::npos)
                continue;
            const auto biome = id.substr(0, first);
            const auto pool = id.substr(first + 1, second - first - 1);
            if (isBiomeId(biome) && isPoolId(pool))
                mInferredPools[{biome, pool}].push_back({localVillagePrefix + id, 1, pool == L"roads"});
        }
        auto config = PcReadVillagePoolConfiguration(root);
        if (!config.empty())
            loadConfiguration(std::move(config));
        if (!files.empty() || !mStartPools.empty() || !mConfiguredPools.empty())
            LOG_INFO(sLog, "Imported %zu of %zu NBT templates from %s",
                     mTemplates.size(), files.size(), convWstringToString(root).c_str());
        for (const auto& start : mStartPools)
            if (start.first != L"*" && pool(start.first, start.second).empty())
                LOG_WARN(sLog, "Start pool %s for %s has no usable templates",
                         convWstringToString(start.second).c_str(), convWstringToString(start.first).c_str());
    }

    const StructureTemplate* find(const std::wstring& id) const
    {
        const auto it = mTemplates.find(id);
        return it == mTemplates.end() ? nullptr : it->second.get();
    }

    std::vector<LocalVillageTemplateEntry> pool(const std::wstring& biome, const std::wstring& id) const
    {
        const auto exact = mConfiguredPools.find({biome, id});
        if (exact != mConfiguredPools.end())
            return exact->second;
        const auto common = mConfiguredPools.find({L"*", id});
        if (common != mConfiguredPools.end())
            return common->second;
        const auto inferred = mInferredPools.find({biome, id});
        return inferred == mInferredPools.end() ? std::vector<LocalVillageTemplateEntry>{} : inferred->second;
    }

    std::vector<LocalVillageTemplateEntry> starts(const std::wstring& biome) const
    {
        auto it = mStartPools.find(biome);
        if (it == mStartPools.end())
            it = mStartPools.find(L"*");
        return pool(biome, it == mStartPools.end() ? L"center" : it->second);
    }

private:
    using PoolMap = std::map<std::pair<std::wstring, std::wstring>, std::vector<LocalVillageTemplateEntry>>;
    std::map<std::wstring, std::unique_ptr<StructureTemplate>> mTemplates;
    PoolMap mInferredPools;
    PoolMap mConfiguredPools;
    std::map<std::wstring, std::wstring> mStartPools;

    void loadConfiguration(std::string config)
    {
        if (config.compare(0, 3, "\xEF\xBB\xBF") == 0)
            config.erase(0, 3);
        std::istringstream stream(config);
        std::string line;
        for (int lineNumber = 1; std::getline(stream, line); ++lineNumber)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line.front() == '#')
                continue;
            std::vector<std::wstring> fields;
            std::istringstream row(line);
            std::string field;
            while (std::getline(row, field, '\t'))
                fields.push_back(convStringToWstring(field));
            if (fields.size() < 3 || !isBiomeId(fields[1]) || !isPoolId(fields[2]))
            {
                LOG_WARN(sLog, "village-pools.tsv line %d: invalid entry, biome or pool ID", lineNumber);
                continue;
            }
            if (fields[0] == L"start" && fields.size() == 3)
            {
                if (!mStartPools.emplace(fields[1], fields[2]).second)
                    LOG_WARN(sLog, "village-pools.tsv line %d: duplicate start pool for %s",
                             lineNumber, convWstringToString(fields[1]).c_str());
                continue;
            }
            if (fields[0] != L"pool" || fields.size() < 5 || fields.size() > 6)
            {
                LOG_WARN(sLog, "village-pools.tsv line %d: expected a start or pool entry", lineNumber);
                continue;
            }
            const auto type = fields.size() == 6 ? fields[5] : (fields[2] == L"roads" ? L"terrain" : L"rigid");
            const int weight = readWeight(fields[4]);
            if (weight == 0 || (type != L"rigid" && type != L"terrain"))
            {
                LOG_WARN(sLog, "village-pools.tsv line %d: invalid weight or placement mode", lineNumber);
                continue;
            }
            const auto id = templateId(fields[3]);
            auto& entries = mConfiguredPools[{fields[1], fields[2]}];
            if (mTemplates.find(id) == mTemplates.end())
            {
                LOG_WARN(sLog, "village-pools.tsv line %d: template is unavailable: %s",
                         lineNumber, convWstringToString(fields[3]).c_str());
                continue;
            }
            const auto path = localVillagePrefix + id;
            if (std::any_of(entries.begin(), entries.end(), [&](const LocalVillageTemplateEntry& entry) {
                    return entry.path == path;
                }))
            {
                LOG_WARN(sLog, "village-pools.tsv line %d: duplicate pool entry: %s",
                         lineNumber, convWstringToString(fields[3]).c_str());
                continue;
            }
            entries.push_back({path, weight, type == L"terrain"});
        }
    }
};

namespace
{
std::shared_ptr<const LocalVillageCatalog> getLocalVillageCatalog()
{
    const auto root = PcVillageTemplateRoot();
    static std::mutex mutex;
    static std::wstring loadedRoot;
    static std::shared_ptr<const LocalVillageCatalog> catalog;
    std::lock_guard<std::mutex> lock(mutex);
    if (!catalog || root != loadedRoot)
    {
        catalog = std::make_shared<const LocalVillageCatalog>(root);
        loadedRoot = root;
    }
    return catalog;
}
}
#endif

static const wchar_t* STRUCTURE_TEMPLATE_NAMES[eMinecraftStructureTemplate_Count] = {
    L"igloo/igloo_top",
    L"igloo/igloo_middle",
    L"igloo/igloo_bottom",
    L"fossils/fossil_spine_01",
    L"fossils/fossil_spine_02",
    L"fossils/fossil_spine_03",
    L"fossils/fossil_spine_04",
    L"fossils/fossil_spine_01_coal",
    L"fossils/fossil_spine_02_coal",
    L"fossils/fossil_spine_03_coal",
    L"fossils/fossil_spine_04_coal",
    L"fossils/fossil_skull_01",
    L"fossils/fossil_skull_02",
    L"fossils/fossil_skull_03",
    L"fossils/fossil_skull_04",
    L"fossils/fossil_skull_01_coal",
    L"fossils/fossil_skull_02_coal",
    L"fossils/fossil_skull_03_coal",
    L"fossils/fossil_skull_04_coal",
    L"endcity/base_floor",
    L"endcity/base_roof",
    L"endcity/bridge_end",
    L"endcity/bridge_gentle_stairs",
    L"endcity/bridge_piece",
    L"endcity/bridge_steep_stairs",
    L"endcity/fat_tower_base",
    L"endcity/fat_tower_middle",
    L"endcity/fat_tower_top",
    L"endcity/second_floor",
    L"endcity/second_floor_2",
    L"endcity/second_roof",
    L"endcity/ship",
    L"endcity/third_floor",
    L"endcity/third_floor_b",
    L"endcity/third_floor_c",
    L"endcity/third_roof",
    L"endcity/tower_base",
    L"endcity/tower_floor",
    L"endcity/tower_piece",
    L"endcity/tower_top",
    L"mansion/carpet_south",
    L"mansion/carpet_south_2",
    L"mansion/carpet_west",
    L"mansion/carpet_west_2",
    L"mansion/carpet_north",
    L"mansion/carpet_east",
    L"mansion/corridor_floor",
    L"mansion/corridor_floor_2",
    L"mansion/indoors_wall",
    L"mansion/indoors_wall_2",
    L"mansion/indoors_door",
    L"mansion/indoors_door_2",
    L"mansion/roof",
    L"mansion/roof_front",
    L"mansion/small_wall",
    L"mansion/small_wall_corner",
    L"mansion/roof_corner",
    L"mansion/roof_inner_corner",
    L"mansion/entrance",
    L"mansion/wall_corner",
    L"mansion/wall_flat",
    L"mansion/wall_window",
    L"mansion/1x1_a1",
    L"mansion/1x1_a2",
    L"mansion/1x1_a3",
    L"mansion/1x1_a4",
    L"mansion/1x1_a5",
    L"mansion/1x1_as1",
    L"mansion/1x1_as2",
    L"mansion/1x1_as3",
    L"mansion/1x1_as4",
    L"mansion/1x1_b1",
    L"mansion/1x1_b2",
    L"mansion/1x1_b3",
    L"mansion/1x1_b4",
    L"mansion/1x1_b5",
    L"mansion/1x2_a1",
    L"mansion/1x2_a2",
    L"mansion/1x2_a3",
    L"mansion/1x2_a4",
    L"mansion/1x2_a5",
    L"mansion/1x2_a6",
    L"mansion/1x2_a7",
    L"mansion/1x2_a8",
    L"mansion/1x2_a9",
    L"mansion/1x2_b1",
    L"mansion/1x2_b2",
    L"mansion/1x2_b3",
    L"mansion/1x2_b4",
    L"mansion/1x2_b5",
    L"mansion/1x2_c_stairs",
    L"mansion/1x2_c1",
    L"mansion/1x2_c2",
    L"mansion/1x2_c3",
    L"mansion/1x2_c4",
    L"mansion/1x2_d_stairs",
    L"mansion/1x2_d1",
    L"mansion/1x2_d2",
    L"mansion/1x2_d3",
    L"mansion/1x2_d4",
    L"mansion/1x2_d5",
    L"mansion/1x2_s1",
    L"mansion/1x2_s2",
    L"mansion/1x2_se1",
    L"mansion/2x2_a1",
    L"mansion/2x2_a2",
    L"mansion/2x2_a3",
    L"mansion/2x2_a4",
    L"mansion/2x2_b1",
    L"mansion/2x2_b2",
    L"mansion/2x2_b3",
    L"mansion/2x2_b4",
    L"mansion/2x2_b5",
    L"mansion/2x2_s1",
    L"shipwreck/swwithmast",
    L"shipwreck/swupsidedownfull",
    L"shipwreck/swupsidedownfronthalf",
    L"shipwreck/swupsidedownbackhalf",
    L"shipwreck/swsidewaysfull",
    L"shipwreck/swsidewaysfronthalf",
    L"shipwreck/swsidewaysbackhalf",
    L"shipwreck/swrightsideupfull",
    L"shipwreck/swrightsideupfronthalf",
    L"shipwreck/swrightsideupbackhalf",
    L"shipwreck/swwithmastdegraded",
    L"shipwreck/swupsidedownfulldegraded",
    L"shipwreck/swupsidedownfronthalfdegraded",
    L"shipwreck/swupsidedownbackhalfdegraded",
    L"shipwreck/swsidewaysfulldegraded",
    L"shipwreck/swsidewaysfronthalfdegraded",
    L"shipwreck/swsidewaysbackhalfdegraded",
    L"shipwreck/swrightsideupfulldegraded",
    L"shipwreck/swrightsideupfronthalfdegraded",
    L"shipwreck/swrightsideupbackhalfdegraded",
    L"ruin/big_ruin1_brick",
    L"ruin/big_ruin2_brick",
    L"ruin/big_ruin3_brick",
    L"ruin/big_ruin8_brick",
    L"ruin/big_ruin1_mossy",
    L"ruin/big_ruin2_mossy",
    L"ruin/big_ruin3_mossy",
    L"ruin/big_ruin8_mossy",
    L"ruin/big_ruin1_cracked",
    L"ruin/big_ruin2_cracked",
    L"ruin/big_ruin3_cracked",
    L"ruin/big_ruin8_cracked",
    L"ruin/big_ruin_warm4",
    L"ruin/big_ruin_warm5",
    L"ruin/big_ruin_warm6",
    L"ruin/big_ruin_warm7",
    L"ruin/ruin_warm1",
    L"ruin/ruin_warm2",
    L"ruin/ruin_warm3",
    L"ruin/ruin1_brick",
    L"ruin/ruin1_cracked",
    L"ruin/ruin1_mossy",
    L"ruin/ruin2_brick",
    L"ruin/ruin2_cracked",
    L"ruin/ruin2_mossy",
    L"ruin/ruin3_brick",
    L"ruin/ruin3_cracked",
    L"ruin/ruin3_mossy",
    L"ruin/ruin4_brick",
    L"ruin/ruin4_cracked",
    L"ruin/ruin4_mossy",
    L"ruin/ruin5_brick",
    L"ruin/ruin5_cracked",
    L"ruin/ruin5_mossy",
    L"ruin/ruin6_brick",
    L"ruin/ruin6_cracked",
    L"ruin/ruin6_mossy",
    L"ruin/ruin7_brick",
    L"ruin/ruin7_cracked",
    L"ruin/ruin7_mossy",
    L"ruin/ruin8_brick",
    L"ruin/ruin8_cracked",
    L"ruin/ruin8_mossy",
    L"coralcrust/crust1",
    L"coralcrust/crust2",
    L"coralcrust/crust3",
    L"coralcrust/crust4",
    L"coralcrust/crust5",
    L"coralcrust/outcropping1",
    L"coralcrust/outcropping2",
    L"coralcrust/outcropping3",
    L"coralcrust/outcropping4",
    L"coralcrust/outcropping5",
    L"coralcrust/outcropping6",
};

StructureManager::StructureManager(DataFixerUpper* fixer) : templates(11), fixerUpper(fixer)
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    localVillages = getLocalVillageCatalog();
#endif
}

StructureManager::~StructureManager()
{
    for (boost::unordered_map<int, const StructureTemplate*>::iterator it = templates.begin(); it != templates.end();
         ++it)
        delete it->second;
    for (const auto& entry : namedTemplates)
        delete entry.second;
}

void StructureManager::staticCtor()
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    LOG_INFO(sLog, "Registered %zu native village structures and %zu pools",
             NativeVillageRegistry::definitions().size(), NativeVillageRegistry::pools().size());
    getLocalVillageCatalog();
#endif
}

std::wstring StructureManager::fjGetStructureTemplatePath(eMinecraftStructureTemplate type) const
{
    std::wstring name = STRUCTURE_TEMPLATE_NAMES[type];
    return L"Structures\\" + replaceAll(name, L"/", L"\\");
}

void StructureManager::readStructure(eMinecraftStructureTemplate type, ByteArrayInputStream* input)
{
    CompoundTag* tag = NbtIo::readCompressed(input);
    if (!tag)
        return;
    bool hasDataVersion = tag->contains(SharedConstants::DATA_VERSION_TAG, Tag::TAG_ANY_NUMERIC);
    if (!hasDataVersion)
        tag->putInt(SharedConstants::DATA_VERSION_TAG, 500);

    StructureTemplate* structureTemplate = new StructureTemplate();
    CompoundTag* fixedTag = fixerUpper->updateData(DataFixTypes::eDataFixTypes_Structure, tag);
    structureTemplate->load(fixedTag);
    delete tag;

    boost::unordered_map<int, const StructureTemplate*>::iterator it = templates.find(type);
    if (it != templates.end())
        delete it->second;
    templates[type] = structureTemplate;
}

bool StructureManager::load(eMinecraftStructureTemplate type)
{
    std::wstring path = fjGetStructureTemplatePath(type);
    if (gApp.hasArchiveFile(path, false))
    {
        arrayWithLength<unsigned char> data = gApp.getArchiveFile(path, false);
        if (data.length != 0 && data.data != NULL)
        {
            ByteArrayInputStream input(data);
            readStructure(type, &input);
            input.reset();
            delete[] data.data;
            return true;
        }
    }
    return false;
}

const StructureTemplate* StructureManager::get(eMinecraftStructureTemplate type)
{
    if (templates.find(type) != templates.end())
    {
        return templates.find(type)->second;
    }
    else
    {
        load(type);
        if (templates.find(type) != templates.end())
        {
            return templates.find(type)->second;
        }
    }
    // Callers can treat a missing archive entry as an empty template.
    return templates.emplace(type, new StructureTemplate()).first->second;
}

const StructureTemplate* StructureManager::getOrCreate(eMinecraftStructureTemplate type)
{
    const StructureTemplate* structureTemplate = get(type);
    if (structureTemplate == NULL)
    {
        structureTemplate = new StructureTemplate();
        templates[type] = structureTemplate;
    }
    return structureTemplate;
}

const StructureTemplate* StructureManager::getNamed(const std::wstring& archivePath)
{
    const auto cached = namedTemplates.find(archivePath);
    if (cached != namedTemplates.end())
        return cached->second;
    if (const auto* definition = NativeVillageRegistry::find(archivePath))
    {
        const auto* result = NativeVillageRegistry::create(*definition);
        namedTemplates.emplace(archivePath, result);
        return result;
    }
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    if (archivePath.compare(0, localVillagePrefix.size(), localVillagePrefix) == 0)
        return localVillages->find(archivePath.substr(localVillagePrefix.size()));
#endif
    if (!gApp.hasArchiveFile(archivePath, true))
        return nullptr;
    const auto bytes = gApp.getArchiveFile(archivePath, true);
    ByteArrayInputStream input(bytes);
    if (bytes.length == 0 || !bytes.data)
        return nullptr;
    std::unique_ptr<CompoundTag> tag(NbtIo::readCompressed(&input));
    if (!tag)
        return nullptr;
    if (!tag->contains(SharedConstants::DATA_VERSION_TAG, Tag::TAG_ANY_NUMERIC))
        tag->putInt(SharedConstants::DATA_VERSION_TAG, 500);
    auto structure = std::make_unique<StructureTemplate>();
    structure->load(fixerUpper->updateData(DataFixTypes::eDataFixTypes_Structure, tag.get()), true);
    const auto size = structure->getSize();
    if (size.x <= 0 || size.y <= 0 || size.z <= 0)
        return nullptr;
    const auto* result = structure.get();
    namedTemplates.emplace(archivePath, structure.release());
    return result;
}

std::vector<LocalVillageTemplateEntry> StructureManager::getLocalVillageTemplates(const std::wstring& biome,
                                                                                const std::wstring& pool) const
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    return localVillages->pool(biome, pool);
#else
    return {};
#endif
}

std::vector<LocalVillageTemplateEntry> StructureManager::getLocalVillageStarts(const std::wstring& biome) const
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    return localVillages->starts(biome);
#else
    return {};
#endif
}
