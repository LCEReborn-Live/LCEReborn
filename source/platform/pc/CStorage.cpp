#include "platform/pc/CStorage.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_set>

#include "game/world/generation/PlayerUID.hpp"
#include "platform/BuiltInPacks.hpp"
#include "platform/C4JProfile.hpp"
#include "platform/pc/FileApi.hpp"
#include "platform/pc/PcPaths.hpp"

extern C_4JProfile* gProfile;

namespace fs = std::filesystem;

static CStorage* s_storage;
static const unsigned int MAX_SAVES_LISTED = 100;
static const unsigned int PROFILE_WRITE_INTERVAL = 300000;
static const unsigned int DLC_MOUNT_PENDING = 0x3e5;
static const char* const SAVE_DATA_FILE = "world.bin";
static const char* const SAVE_TITLE_FILE = "title.txt";
static const char* const SAVE_IMAGE_FILE = "image.png";
static const char* const SAVE_THUMBNAIL_FILE = "thumbnail.png";
static const char* const SAVE_MODIFIED_FILE = "modified.txt";
C4JStorage* gStorage = new CStorage();

enum EWriteResult
{
    eWriteResult_Ok,
    eWriteResult_Failed,
    eWriteResult_NoSpace
};

static inline unsigned int ReadPngChunkWord(const unsigned char* data)
{
    return ((unsigned int)data[0] << 24) | ((unsigned int)data[1] << 16) | ((unsigned int)data[2] << 8) | data[3];
}

static inline void WritePngChunkWord(unsigned char* data, unsigned int value)
{
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)(value >> 16);
    data[2] = (unsigned char)(value >> 8);
    data[3] = (unsigned char)value;
}

static unsigned int CurrentTickCount()
{
    return static_cast<unsigned int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

static long long CurrentTimeMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

static void AppendUtf8(std::string& output, unsigned int codePoint)
{
    if (codePoint < 0x80)
        output += static_cast<char>(codePoint);
    else if (codePoint < 0x800)
    {
        output += static_cast<char>(0xc0 | (codePoint >> 6));
        output += static_cast<char>(0x80 | (codePoint & 0x3f));
    }
    else if (codePoint < 0x10000)
    {
        output += static_cast<char>(0xe0 | (codePoint >> 12));
        output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (codePoint & 0x3f));
    }
    else
    {
        output += static_cast<char>(0xf0 | (codePoint >> 18));
        output += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f));
        output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
        output += static_cast<char>(0x80 | (codePoint & 0x3f));
    }
}

static std::string WideToUtf8(const wchar_t* text, size_t maxLength)
{
    std::string output;
    size_t index = 0;
    while (index < maxLength && text[index] != L'\0')
    {
        unsigned int codePoint = static_cast<unsigned int>(text[index++]);
        if (sizeof(wchar_t) == 2 && codePoint >= 0xd800 && codePoint < 0xdc00 && index < maxLength)
        {
            unsigned int low = static_cast<unsigned int>(text[index]);
            if (low >= 0xdc00 && low < 0xe000)
            {
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            }
        }
        if (codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint < 0xe000))
            codePoint = 0xfffd;
        AppendUtf8(output, codePoint);
    }
    return output;
}

static void Utf8ToWide(const std::string& text, wchar_t* output, size_t capacity)
{
    size_t length = 0;
    size_t index = 0;
    while (index < text.size())
    {
        unsigned int lead = static_cast<unsigned char>(text[index]);
        unsigned int codePoint;
        size_t extra;
        if (lead < 0x80)
        {
            codePoint = lead;
            extra = 0;
        }
        else if ((lead & 0xe0) == 0xc0)
        {
            codePoint = lead & 0x1f;
            extra = 1;
        }
        else if ((lead & 0xf0) == 0xe0)
        {
            codePoint = lead & 0x0f;
            extra = 2;
        }
        else if ((lead & 0xf8) == 0xf0)
        {
            codePoint = lead & 0x07;
            extra = 3;
        }
        else
        {
            codePoint = 0xfffd;
            extra = 0;
        }
        ++index;
        for (size_t continuation = 0; continuation < extra; ++continuation)
        {
            if (index >= text.size() || (static_cast<unsigned char>(text[index]) & 0xc0) != 0x80)
            {
                codePoint = 0xfffd;
                break;
            }
            codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[index++]) & 0x3f);
        }
        if (sizeof(wchar_t) == 2 && codePoint >= 0x10000)
        {
            if (length + 2 >= capacity)
                break;
            codePoint -= 0x10000;
            output[length++] = static_cast<wchar_t>(0xd800 + (codePoint >> 10));
            output[length++] = static_cast<wchar_t>(0xdc00 + (codePoint & 0x3ff));
        }
        else
        {
            if (length + 1 >= capacity)
                break;
            output[length++] = static_cast<wchar_t>(codePoint);
        }
    }
    output[length] = L'\0';
}

static std::string EscapeFileName(const std::string& name)
{
    static const char* const reservedNames[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                                "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                                "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    static const char hexDigits[] = "0123456789ABCDEF";
    std::string upper;
    for (size_t index = 0; index < name.size(); ++index)
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(name[index])));
    bool reserved = false;
    for (size_t index = 0; index < sizeof(reservedNames) / sizeof(reservedNames[0]); ++index)
        if (upper == reservedNames[index])
            reserved = true;

    std::string escaped;
    for (size_t index = 0; index < name.size(); ++index)
    {
        unsigned char character = static_cast<unsigned char>(name[index]);
        bool plain = std::isalnum(character) || character == '_' || character == '-' || character == ' '
                     || character == '.' || character >= 0x80;
        if ((index == 0 && reserved) || (index + 1 == name.size() && (character == '.' || character == ' ')))
            plain = false;
        if (plain)
            escaped += static_cast<char>(character);
        else
        {
            escaped += '%';
            escaped += hexDigits[character >> 4];
            escaped += hexDigits[character & 0x0f];
        }
    }
    if (escaped.empty())
        escaped = "%00";
    return escaped;
}

static fs::path SaveRoot()
{
    return fs::path(PcSaveRoot());
}

static fs::path GamesFolder()
{
    return SaveRoot() / "games";
}

static fs::path SaveFolder(const char* uniqueName)
{
    return GamesFolder() / fs::u8path(uniqueName);
}

static std::string PlayerFolderName(int pad)
{
    PlayerUID player(0);
    gProfile->GetXUID(pad, &player, false);
    std::wstring identifier = player.toString();
    return EscapeFileName(WideToUtf8(identifier.c_str(), identifier.size()));
}

static fs::path GenericDataPath(int pad, const std::wstring& name)
{
    return SaveRoot() / "generic" / fs::u8path(PlayerFolderName(pad))
           / fs::u8path(EscapeFileName(WideToUtf8(name.c_str(), name.size())) + ".dat");
}

static fs::path TMSPath(const wchar_t* name)
{
    return SaveRoot() / "tms" / fs::u8path(EscapeFileName(WideToUtf8(name, std::wcslen(name))));
}

static unsigned long long FileSizeOrZero(const fs::path& path)
{
    std::error_code error;
    unsigned long long size = fs::file_size(path, error);
    return error ? 0 : size;
}

static bool ReadWholeFile(const fs::path& path, std::vector<unsigned char>& contents)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    stream.seekg(0, std::ios::end);
    std::streamoff size = stream.tellg();
    if (size < 0)
        return false;
    stream.seekg(0, std::ios::beg);
    contents.resize(static_cast<size_t>(size));
    if (size > 0 && !stream.read(reinterpret_cast<char*>(contents.data()), size))
        return false;
    return true;
}

static EWriteResult WriteWholeFile(const fs::path& path, const void* data, size_t size)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error)
        return error == std::errc::no_space_on_device ? eWriteResult_NoSpace : eWriteResult_Failed;

    unsigned long long replaced = FileSizeOrZero(path);
    fs::space_info space = fs::space(path.parent_path(), error);
    if (!error && size > replaced && space.available < size - replaced)
        return eWriteResult_NoSpace;

    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return eWriteResult_Failed;
        if (size > 0)
            stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        stream.flush();
        if (!stream)
        {
            stream.close();
            fs::remove(temporary, error);
            return eWriteResult_Failed;
        }
    }
    fs::rename(temporary, path, error);
    if (error)
    {
        fs::remove(temporary, error);
        return eWriteResult_Failed;
    }
    return eWriteResult_Ok;
}

static EWriteResult WriteTextFile(const fs::path& path, const std::string& text)
{
    return WriteWholeFile(path, text.data(), text.size());
}

static EWriteResult WriteModifiedTime(const fs::path& folder)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%lld", CurrentTimeMilliseconds());
    return WriteTextFile(folder / SAVE_MODIFIED_FILE, std::string(text));
}

static long long ReadModifiedTime(const fs::path& folder)
{
    std::vector<unsigned char> contents;
    if (!ReadWholeFile(folder / SAVE_MODIFIED_FILE, contents))
        return 0;
    std::string text(contents.begin(), contents.end());
    return std::strtoll(text.c_str(), NULL, 10);
}

static EWriteResult CombineWriteResults(EWriteResult first, EWriteResult second)
{
    if (first == eWriteResult_NoSpace || second == eWriteResult_NoSpace)
        return eWriteResult_NoSpace;
    if (first == eWriteResult_Failed || second == eWriteResult_Failed)
        return eWriteResult_Failed;
    return eWriteResult_Ok;
}

void C4JStorage::SetSaveDeviceSelected(unsigned int, bool)
{
}

CStorage::DriveMapping::DriveMapping()
{
}

CStorage::DriveMapping::DriveMapping(const std::string& driveName, const std::string& mountedPath)
    : drive(driveName),
      path(mountedPath)
{
}

CStorage::CStorage()
{
    s_storage = this;
    m_crcReady = false;
    m_savingMessageCallback = NULL;
    m_savingMessageContext = NULL;
    m_saveState = C4JStorage::ESaveGame_Idle;
    m_previousSaveState = C4JStorage::ESaveGame_Idle;
    m_savesInfoState = 0;
    m_savesInfoAvailable = false;
    m_savesInfo = NULL;
    m_selectedSave = NULL;
    std::memset(m_saveUniqueFilename, 0, sizeof(m_saveUniqueFilename));
    std::memset(m_saveTitle, 0, sizeof(m_saveTitle));
    std::memset(m_gameSaveFolderTitle, 0, sizeof(m_gameSaveFolderTitle));
    std::memset(m_saveCacheFolderTitle, 0, sizeof(m_saveCacheFolderTitle));
    std::memset(m_optionsFolderTitle, 0, sizeof(m_optionsFolderTitle));
    std::memset(m_corruptSaveName, 0, sizeof(m_corruptSaveName));
    std::memset(m_renameTitle, 0, sizeof(m_renameTitle));
    std::memset(m_gameSaveFolderPrefix, 0, sizeof(m_gameSaveFolderPrefix));
    m_incompleteSaveCallback = NULL;
    m_incompleteSaveContext = NULL;
    m_savesInfoCallback = NULL;
    m_savesInfoContext = NULL;
    m_saveCallback = NULL;
    m_saveCallbackContext = NULL;
    m_loadCallback = NULL;
    m_loadCallbackContext = NULL;
    m_deleteCallback = NULL;
    m_deleteCallbackContext = NULL;
    m_renameCallback = NULL;
    m_renameCallbackContext = NULL;
    m_copyCallback = NULL;
    m_copyProgressCallback = NULL;
    m_copyCallbackContext = NULL;
    m_renameSaveIndex = 0;
    m_maxSaves = 99;
    m_saveData = NULL;
    m_saveSize = 0;
    m_saveImage = NULL;
    m_saveImageSize = 0;
    m_saveThumbnail = NULL;
    m_saveThumbnailSize = 0;
    m_saveDisabled = false;
    m_defaultImage = NULL;
    m_defaultImageSize = 0;
    m_defaultThumbnail = NULL;
    m_defaultThumbnailSize = 0;
    m_incompleteReason = 0;
    for (int profile = 0; profile < 4; ++profile)
    {
        m_profileReadBuffers[profile] = NULL;
        m_profileBuffers[profile] = NULL;
        m_profileUpdateRequired[profile] = false;
        m_profileWriteState[profile].state = 0;
        m_profileWriteState[profile].timer = 0;
        m_profileReadState[profile].state = 0;
        m_profileReadState[profile].value = 0;
        m_dashboardProfileSettings[profile].values[0] = 0;
        m_dashboardProfileSettings[profile].values[1] = 0;
        m_dashboardProfileSettings[profile].values[2] = 0;
        m_dashboardProfileSettings[profile].flags = 0;
    }
    m_profileVersion = 0;
    m_profileCount = 0;
    m_optionCount = 0;
    m_optionsIcon = NULL;
    m_optionsIconSize = 0;
    m_optionDefaults = NULL;
    m_gameProfileSizes = NULL;
    m_gameProfileStride = 0;
    m_gameProfileData = NULL;
    m_profileResult = 0;
    m_defaultOptionsCallback = NULL;
    m_defaultOptionsContext = NULL;
    m_optionsDataCallback = NULL;
    m_optionsDataContext = NULL;
    m_oldProfileVersionCallback = NULL;
    m_oldProfileVersionContext = NULL;
    m_genericState = 0;
    m_genericSlot = 0;
    m_genericDataSize = 0;
    m_genericWriteData = NULL;
    m_genericLoadedData = NULL;
    m_genericLoadedSize = 0;
    m_genericReadSize = NULL;
    m_genericReadData = NULL;
    m_genericCallback = NULL;
    m_genericContext = NULL;
    m_installedCallback = NULL;
    m_installedContext = NULL;
    m_dlcMountCallback = NULL;
    m_dlcMountContext = NULL;
    m_mountedDLCIndex = 0;
    m_dlcMountResult = 0;
    std::memset(m_defaultDLCMountName, 0, sizeof(m_defaultDLCMountName));
    m_openDir = NULL;
    m_readDir = NULL;
    m_closeDir = NULL;
    m_dlcDirectory = -1;
    std::memset(&m_dlcDirectoryEntry, 0, sizeof(m_dlcDirectoryEntry));
    DLCOnCreate();
}

CStorage::~CStorage()
{
    for (int index = 0; index < 6; ++index)
        JoinThread(m_saveThreads[index]);
    for (int profile = 0; profile < 4; ++profile)
    {
        JoinThread(m_profileSaveThreads[profile]);
        JoinThread(m_profileLoadThreads[profile]);
        std::free(m_profileReadBuffers[profile]);
        std::free(m_profileBuffers[profile]);
    }
    JoinThread(m_genericThread);
    ClearSavesInfo();
    for (size_t index = 0; index < m_retiredSavesInfo.size(); ++index)
        delete m_retiredSavesInfo[index];
    std::free(m_saveData);
    std::free(m_saveImage);
    std::free(m_saveThumbnail);
    std::free(m_defaultImage);
    std::free(m_defaultThumbnail);
    std::free(m_optionsIcon);
    std::free(m_gameProfileData);
    std::free(m_genericLoadedData);
    delete[] m_optionDefaults;
    if (s_storage == this)
        s_storage = NULL;
}

void CStorage::JoinThread(std::thread& thread)
{
    if (thread.joinable())
        thread.join();
}

void CStorage::StartThread(std::thread& thread, unsigned int (CStorage::*procedure)())
{
    JoinThread(thread);
    thread = std::thread([this, procedure]() { (this->*procedure)(); });
}

void CStorage::StartThread(std::thread& thread, unsigned int (CStorage::*procedure)(int), int argument)
{
    JoinThread(thread);
    thread = std::thread([this, procedure, argument]() { (this->*procedure)(argument); });
}

void CStorage::DisplaySavingMessages()
{
    C4JStorage::ESaveGameState saveState = GetSaveState();
    int optionsWriting = IsOptionsWriteInProgress();
    if (optionsWriting == 0 && saveState != C4JStorage::ESaveGame_Save)
    {
        if (m_savingMessageCallback != NULL)
            m_savingMessageCallback(m_savingMessageContext, C4JStorage::ESavingMessage_0, 0);
    }
    else if (m_savingMessageCallback != NULL)
        m_savingMessageCallback(m_savingMessageContext, C4JStorage::ESavingMessage_2, 0);
}

void CStorage::Tick()
{
    DisplaySavingMessages();
    TickSaveGame();
    TickDLC();
    TickOptions();
    GenericDataTick();
}

void CStorage::Init(SavingMessageCallback callback, void* context, const char*)
{
    m_savingMessageCallback = callback;
    m_savingMessageContext = context;
}

void CStorage::Init(unsigned int device, const wchar_t* title, const char* prefix, int maxSaves,
                    SavingMessageCallback callback, void* context, const char* extra, bool* exists)
{
    InitSaveGame(device, title, prefix, maxSaves, exists);
    Init(callback, context, extra);
}

void CStorage::SetDefaultImages(unsigned char* icon, unsigned int iconSize, unsigned char* image,
                                unsigned int imageSize, unsigned char* thumbnail, unsigned int thumbnailSize)
{
    SetOptionsIcon(icon, iconSize);
    SetDefaultImages(image, imageSize, thumbnail, thumbnailSize);
}

int CStorage::DoesSaveExist(bool* exists)
{
    *exists = true;
    return 0;
}

int CStorage::EnoughSpaceForAMinSaveGame()
{
    return 1;
}

void CStorage::MakeCRCTable()
{
    for (unsigned int n = 0; n < 256; ++n)
    {
        unsigned int value = n;
        for (int k = 0; k < 8; ++k)
        {
            if (value & 1)
                value = 0xedb88320U ^ (value >> 1);
            else
                value = value >> 1;
        }
        m_crcTable[n] = value;
    }
    m_crcReady = true;
}

unsigned int CStorage::UpdateCRC(unsigned int crc, unsigned char* data, int length)
{
    unsigned int value = crc;
    if (!m_crcReady)
        MakeCRCTable();
    for (int offset = 0; offset < length; ++offset)
        value = m_crcTable[(value ^ data[offset]) & 0xff] ^ (value >> 8);
    return value;
}

unsigned int CStorage::CRC(unsigned char* data, int length)
{
    return ~UpdateCRC(0xffffffffU, data, length);
}

int CStorage::GetSaveDeviceSelected(unsigned int)
{
    return 1;
}

int CStorage::ReadTMSFile(int pad, C4JStorage::eGlobalStorage, C4JStorage::eTMS_FileType type, wchar_t* name,
                          unsigned char** data, unsigned int* size,
                          int (*callback)(void*, wchar_t*, int, bool, C4JStorage::eTMS_FileType), void* context, int)
{
    if (name == NULL || data == NULL || size == NULL)
        return 0;
    std::vector<unsigned char> contents;
    bool loaded;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);
        loaded = ReadWholeFile(TMSPath(name), contents);
    }
    if (loaded)
    {
        *size = static_cast<unsigned int>(contents.size());
        *data = static_cast<unsigned char*>(std::malloc(contents.empty() ? 1 : contents.size()));
        if (*data == NULL)
        {
            *size = 0;
            loaded = false;
        }
        else if (!contents.empty())
            std::memcpy(*data, contents.data(), contents.size());
    }
    else
    {
        *data = NULL;
        *size = 0;
    }
    if (callback != NULL)
        callback(context, name, pad, loaded, type);
    return loaded ? 1 : 0;
}

int CStorage::WriteTMSFile(int, C4JStorage::eGlobalStorage, wchar_t* name, unsigned char* data, unsigned int size)
{
    if (name == NULL || (data == NULL && size != 0))
        return 0;
    std::lock_guard<std::mutex> lock(m_storageMutex);
    return WriteWholeFile(TMSPath(name), data, size) == eWriteResult_Ok ? 1 : 0;
}

int CStorage::DeleteTMSFile(int, C4JStorage::eGlobalStorage, wchar_t* name)
{
    if (name == NULL)
        return 0;
    std::lock_guard<std::mutex> lock(m_storageMutex);
    std::error_code error;
    return fs::remove(TMSPath(name), error) && !error ? 1 : 0;
}

void CStorage::InitialiseProfileData(unsigned short version, unsigned int profileCount, unsigned int optionCount,
                                     unsigned int* defaults, int gameDataSize, unsigned int* gameDataSizes)
{
    m_profileVersion = version;
    delete[] m_optionDefaults;
    m_optionDefaults = new unsigned int[optionCount];
    m_profileCount = profileCount;
    m_optionCount = optionCount;
    m_optionsDataCallback = NULL;
    m_optionsDataContext = NULL;
    m_oldProfileVersionCallback = NULL;
    if (optionCount != 0)
        std::memcpy(m_optionDefaults, defaults, optionCount * sizeof(unsigned int));

    for (int profile = 0; profile < 4; ++profile)
    {
        m_profileWriteState[profile].state = 0;
        m_profileWriteState[profile].timer = 0;
        m_profileReadState[profile].state = 0;
    }

    std::free(m_gameProfileData);
    m_gameProfileData = static_cast<unsigned char*>(std::calloc(gameDataSize > 0 ? gameDataSize : 1, 1));
    m_gameProfileStride = gameDataSize / 4;
    *gameDataSizes = 0;
    m_gameProfileSizes = gameDataSizes;

    m_optionsIcon = NULL;
    m_optionsIconSize = 0;
    m_profileResult = 0;
}

int CStorage::SetDefaultOptionsCallback(DefaultOptionsCallback callback, void* context)
{
    m_defaultOptionsCallback = callback;
    m_defaultOptionsContext = context;
    return 0;
}

void CStorage::SetOptionsDataCallback(OptionsDataCallback callback, void* context)
{
    m_optionsDataCallback = callback;
    m_optionsDataContext = context;
}

int CStorage::SetOldProfileVersionCallback(OldProfileVersionCallback callback, void* context)
{
    m_oldProfileVersionCallback = callback;
    m_oldProfileVersionContext = context;
    return 0;
}

C4JStorage::PROFILESETTINGS* CStorage::GetDashboardProfileSettings(int profile)
{
    return &m_dashboardProfileSettings[profile];
}

unsigned char* CStorage::GetGameDefinedProfileData(int profile)
{
    return m_gameProfileData + m_gameProfileStride * profile;
}

void CStorage::ReadFromProfile(int profile, int value)
{
    if (profile < 0 || profile >= 4 || m_profileReadState[profile].state != 0)
        return;
    m_profileReadState[profile].value = value;
    m_profileReadState[profile].state = 5;
}

void CStorage::ClearProfileWriteTimer(int profile)
{
    if (profile >= 0 && profile < 4)
        m_profileWriteState[profile].timer = 0;
}

void CStorage::WriteToProfile(int profile, bool write, bool force)
{
    if (profile < 0 || profile >= 4 || !write)
        return;
    if (m_profileWriteState[profile].state == 0)
        m_profileWriteState[profile].state = 1;
    else if (m_profileWriteState[profile].state != 1)
        return;
    if (force)
        ClearProfileWriteTimer(profile);
}

void CStorage::DeleteOptionsData(int profile)
{
    if (profile >= 0 && profile < 4)
        DeleteOptionsFile(profile);
}

void CStorage::ForceQueuedProfileWrites(int profile)
{
    if (profile == 255)
    {
        for (int index = 0; index < 4; ++index)
            ClearProfileWriteTimer(index);
    }
    else
        ClearProfileWriteTimer(profile);
}

int CStorage::IsOptionsWriteInProgressOrPending(int profile)
{
    if (m_profileWriteState[profile].state == 2 || m_profileWriteState[profile].state == 1)
        return 1;
    return 0;
}

void CStorage::SetOptionsIcon(unsigned char* data, unsigned int size)
{
    std::free(m_optionsIcon);
    m_optionsIcon = NULL;
    m_optionsIconSize = 0;
    if (data == NULL || size == 0)
        return;
    m_optionsIcon = static_cast<unsigned char*>(std::malloc(size));
    if (m_optionsIcon == NULL)
        return;
    std::memcpy(m_optionsIcon, data, size);
    m_optionsIconSize = size;
}

unsigned int CStorage::GetOptionsIconSize()
{
    return m_optionsIconSize;
}

unsigned char* CStorage::GetOptionsIconData()
{
    return m_optionsIcon;
}

std::string CStorage::GetOptionsFilename(int profile)
{
    return std::string("options_") + PlayerFolderName(profile);
}

unsigned int CStorage::SaveOptionsThreadProc(int profile)
{
    std::lock_guard<std::mutex> lock(m_storageMutex);
    if (!gProfile->IsSignedIn(profile) || gProfile->IsGuest(profile))
    {
        m_profileWriteState[profile].state = 4;
        return 0;
    }
    fs::path path = SaveRoot() / "options" / fs::u8path(GetOptionsFilename(profile) + ".dat");
    EWriteResult result = WriteWholeFile(path, m_profileBuffers[profile], m_gameProfileStride + 2);
    if (result == eWriteResult_Ok)
        m_profileWriteState[profile].state = 3;
    else
    {
        if (result == eWriteResult_NoSpace)
            m_profileResult = 4;
        m_profileWriteState[profile].state = 4;
    }
    return 0;
}

unsigned int CStorage::LoadOptionsThreadProc(int profile)
{
    std::lock_guard<std::mutex> lock(m_storageMutex);
    m_profileUpdateRequired[profile] = false;
    if (!gProfile->IsSignedIn(profile) || gProfile->IsGuest(profile))
    {
        m_profileReadState[profile].state = 8;
        return 0;
    }
    fs::path path = SaveRoot() / "options" / fs::u8path(GetOptionsFilename(profile) + ".dat");
    std::error_code error;
    if (!fs::exists(path, error))
    {
        m_profileReadState[profile].state = 8;
        return 0;
    }
    std::vector<unsigned char> contents;
    if (!ReadWholeFile(path, contents) || contents.size() < 2)
    {
        m_profileUpdateRequired[profile] = true;
        m_profileReadState[profile].state = 8;
        return 0;
    }

    unsigned short storedVersion;
    std::memcpy(&storedVersion, contents.data(), sizeof(storedVersion));
    size_t stride = static_cast<size_t>(m_gameProfileStride);
    size_t available = contents.size() - 2;
    if (storedVersion != m_profileVersion && m_oldProfileVersionCallback == NULL)
    {
        m_profileReadState[profile].state = 8;
        return 0;
    }
    if (storedVersion == m_profileVersion && available < stride)
    {
        m_profileUpdateRequired[profile] = true;
        m_profileReadState[profile].state = 8;
        return 0;
    }

    unsigned char* buffer = static_cast<unsigned char*>(std::calloc(stride > 0 ? stride : 1, 1));
    if (buffer == NULL)
    {
        m_profileReadState[profile].state = 8;
        return 0;
    }
    std::memcpy(buffer, contents.data() + 2, std::min(available, stride));
    m_profileReadBuffers[profile] = buffer;
    if (storedVersion != m_profileVersion)
        m_oldProfileVersionCallback(m_oldProfileVersionContext, buffer, storedVersion, profile);
    m_profileReadState[profile].state = 7;
    return 0;
}

void CStorage::CheckForProfileUpdatesRequired()
{
    unsigned int now = CurrentTickCount();
    for (int profile = 0; profile < 4; ++profile)
    {
        if (m_profileWriteState[profile].timer != 0 && GetSaveDisabled())
            m_profileWriteState[profile].timer = CurrentTickCount() + PROFILE_WRITE_INTERVAL;
        if (m_profileWriteState[profile].state == 1 && now >= m_profileWriteState[profile].timer)
        {
            unsigned int bytes = m_gameProfileStride + 2;
            unsigned char* buffer = static_cast<unsigned char*>(std::malloc(bytes));
            if (buffer == NULL)
            {
                m_profileWriteState[profile].state = 4;
                continue;
            }
            std::free(m_profileBuffers[profile]);
            m_profileBuffers[profile] = buffer;
            std::memcpy(buffer, &m_profileVersion, 2);
            if (m_gameProfileStride > 0)
                std::memcpy(buffer + 2, m_gameProfileData + m_gameProfileStride * profile, m_gameProfileStride);
            m_profileWriteState[profile].state = 2;
            m_profileWriteState[profile].timer = CurrentTickCount() + PROFILE_WRITE_INTERVAL;
            StartThread(m_profileSaveThreads[profile], &CStorage::SaveOptionsThreadProc, profile);
        }
    }
    for (int profile = 0; profile < 4; ++profile)
    {
        if (m_profileReadState[profile].state == 5)
        {
            m_profileReadState[profile].state = 6;
            StartThread(m_profileLoadThreads[profile], &CStorage::LoadOptionsThreadProc, profile);
        }
    }
}

void CStorage::ProcessOptionsUpdate()
{
    for (int profile = 0; profile < 4; ++profile)
    {
        if (m_profileReadState[profile].state == 7)
        {
            JoinThread(m_profileLoadThreads[profile]);
            m_profileReadState[profile].state = 0;
            std::memcpy(GetGameDefinedProfileData(profile), m_profileReadBuffers[profile], m_gameProfileStride);
            std::free(m_profileReadBuffers[profile]);
            m_profileReadBuffers[profile] = NULL;
            *m_gameProfileSizes |= 1U << profile;
            if (m_optionsDataCallback != NULL)
                m_optionsDataCallback(m_optionsDataContext, profile, 0, C4JStorage::eOptions_Callback_Read);
        }
        else if (m_profileReadState[profile].state == 8)
        {
            JoinThread(m_profileLoadThreads[profile]);
            m_profileReadState[profile].state = 0;
            std::free(m_profileReadBuffers[profile]);
            m_profileReadBuffers[profile] = NULL;
            if (m_optionsDataCallback != NULL)
            {
                if (m_profileUpdateRequired[profile])
                    m_optionsDataCallback(m_optionsDataContext, profile, 0, C4JStorage::eOptions_Callback_Read_Corrupt);
                else
                    m_optionsDataCallback(m_optionsDataContext, profile, 0, C4JStorage::eOptions_Callback_Read_Fail);
            }
        }
        else if (m_profileWriteState[profile].state == 3)
        {
            JoinThread(m_profileSaveThreads[profile]);
            m_profileWriteState[profile].state = 0;
            std::free(m_profileBuffers[profile]);
            m_profileBuffers[profile] = NULL;
            if (m_optionsDataCallback != NULL)
                m_optionsDataCallback(m_optionsDataContext, profile, 0, C4JStorage::eOptions_Callback_Write);
        }
        else if (m_profileWriteState[profile].state == 4)
        {
            JoinThread(m_profileSaveThreads[profile]);
            m_profileWriteState[profile].state = 0;
            std::free(m_profileBuffers[profile]);
            m_profileBuffers[profile] = NULL;
            if (m_optionsDataCallback != NULL)
            {
                if (m_profileResult == 4)
                {
                    m_profileResult = 0;
                    m_optionsDataCallback(m_optionsDataContext, profile, 0,
                                          C4JStorage::eOptions_Callback_Write_Fail_NoSpace);
                }
                else
                    m_optionsDataCallback(m_optionsDataContext, profile, 0, C4JStorage::eOptions_Callback_Write_Fail);
            }
        }
    }
}

void CStorage::TickOptions()
{
    CheckForProfileUpdatesRequired();
    ProcessOptionsUpdate();
}

int CStorage::IsOptionsWriteInProgress()
{
    for (int profile = 0; profile < 4; ++profile)
        if (m_profileWriteState[profile].state == 2)
            return 1;
    return 0;
}

void CStorage::DeleteOptionsFile(int profile)
{
    bool deleted = false;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);
        if (gProfile->IsSignedIn(profile) && !gProfile->IsGuest(profile))
        {
            std::error_code error;
            deleted = fs::remove(SaveRoot() / "options" / fs::u8path(GetOptionsFilename(profile) + ".dat"), error)
                      && !error;
        }
    }
    if (deleted && m_optionsDataCallback != NULL)
        m_optionsDataCallback(m_optionsDataContext, profile, 0, C4JStorage::eOptions_Callback_Read_CorruptDeleted);
}

void CStorage::SetGameSaveFolderTitle(const wchar_t* title)
{
    std::wcsncpy(m_gameSaveFolderTitle, title, 127);
    m_gameSaveFolderTitle[127] = L'\0';
}

void CStorage::SetSaveCacheFolderTitle(const wchar_t* title)
{
    std::wcsncpy(m_saveCacheFolderTitle, title, 127);
    m_saveCacheFolderTitle[127] = L'\0';
}

void CStorage::SetOptionsFolderTitle(const wchar_t* title)
{
    std::wcsncpy(m_optionsFolderTitle, title, 127);
    m_optionsFolderTitle[127] = L'\0';
}

void CStorage::SetCorruptSaveName(const wchar_t* title)
{
    std::wcsncpy(m_corruptSaveName, title, 127);
    m_corruptSaveName[127] = L'\0';
}

void CStorage::SetGameSaveFolderPrefix(const char* prefix)
{
    std::strncpy(m_gameSaveFolderPrefix, prefix, sizeof(m_gameSaveFolderPrefix) - 1);
    m_gameSaveFolderPrefix[sizeof(m_gameSaveFolderPrefix) - 1] = '\0';
}

void CStorage::SetMaxSaves(int maxSaves)
{
    m_maxSaves = maxSaves;
}

void CStorage::SetIncompleteSaveCallback(IncompleteSaveCallback callback, void* context)
{
    m_incompleteSaveCallback = callback;
    m_incompleteSaveContext = context;
}

void CStorage::SetSaveDisabled(bool disabled)
{
    m_saveDisabled = disabled;
}

unsigned char CStorage::GetSaveDisabled()
{
    return m_saveDisabled ? 1 : 0;
}

void CStorage::ResetSaveData()
{
    std::free(m_saveData);
    m_saveData = NULL;
    m_saveSize = 0;
}

C4JStorage::ESaveGameState CStorage::GetSaveState()
{
    return m_saveState;
}

int CStorage::saveInfoSortfn(const void* left, const void* right)
{
    const SAVE_INFO* first = static_cast<const SAVE_INFO*>(left);
    const SAVE_INFO* second = static_cast<const SAVE_INFO*>(right);
    if (first->modifiedTime > second->modifiedTime)
        return -1;
    return first->modifiedTime != second->modifiedTime;
}

void CStorage::ReleaseSaveEntries(SAVE_DETAILS* details)
{
    if (details->saves != NULL)
    {
        for (int index = 0; index < details->count; ++index)
            delete[] details->saves[index].thumbnail;
        delete[] details->saves;
    }
    details->count = 0;
    details->saves = NULL;
}

unsigned int CStorage::GetSavesInfoThreadProc()
{
    while (m_saveState != C4JStorage::ESaveGame_Idle && m_saveState != C4JStorage::ESaveGame_SaveIncompleteWaiting)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (m_savesInfo != NULL)
    {
        ReleaseSaveEntries(m_savesInfo);
        m_retiredSavesInfo.push_back(m_savesInfo);
    }

    m_savesInfo = new SAVE_DETAILS;
    std::memset(m_savesInfo, 0, sizeof(SAVE_DETAILS));
    m_savesInfo->saves = new SAVE_INFO[MAX_SAVES_LISTED]();

    std::lock_guard<std::mutex> lock(m_storageMutex);
    std::error_code error;
    fs::directory_iterator folders(GamesFolder(), error);
    if (!error)
    {
        for (fs::directory_iterator end; folders != end && m_savesInfo->count < (int)MAX_SAVES_LISTED;
             folders.increment(error))
        {
            if (error)
                break;
            std::error_code entryError;
            if (!folders->is_directory(entryError))
                continue;
            std::string name = folders->path().filename().u8string();
            if (name.empty() || name.size() >= sizeof(m_savesInfo->saves[0].filename))
                continue;
            fs::path folder = folders->path();
            std::vector<unsigned char> title;
            if (!ReadWholeFile(folder / SAVE_TITLE_FILE, title))
                continue;
            std::vector<unsigned char> image;
            ReadWholeFile(folder / SAVE_IMAGE_FILE, image);

            SAVE_INFO& info = m_savesInfo->saves[m_savesInfo->count++];
            std::memset(&info, 0, sizeof(info));
            std::strcpy(info.filename, name.c_str());
            Utf8ToWide(std::string(title.begin(), title.end()), info.title, sizeof(info.title) / sizeof(wchar_t));
            info.modifiedTime = ReadModifiedTime(folder);
            info.thumbnailSize = static_cast<unsigned int>(image.size());
            info.thumbnail = new unsigned char[info.thumbnailSize];
            if (!image.empty())
                std::memcpy(info.thumbnail, image.data(), image.size());
            info.nandSize = static_cast<unsigned int>(FileSizeOrZero(folder / SAVE_DATA_FILE)
                                                      + FileSizeOrZero(folder / SAVE_TITLE_FILE) + image.size());
            m_savesInfo->nandSize += info.nandSize;
        }
        std::qsort(m_savesInfo->saves, m_savesInfo->count, sizeof(SAVE_INFO), saveInfoSortfn);
    }
    m_savesInfoState = 2;
    return 0;
}

int CStorage::GetSavesInfo(int, SavesInfoCallback callback, void* context, const char*)
{
    m_savesInfoCallback = callback;
    m_savesInfoAvailable = false;
    m_savesInfoContext = context;
    if (m_savesInfoState == 0)
    {
        m_savesInfoState = 1;
        StartThread(m_saveThreads[2], &CStorage::GetSavesInfoThreadProc);
    }
    return 0;
}

SAVE_DETAILS* CStorage::ReturnSavesInfo()
{
    return m_savesInfoAvailable ? m_savesInfo : NULL;
}

void CStorage::ClearSavesInfo()
{
    m_savesInfoAvailable = false;
    if (m_savesInfo != NULL)
    {
        ReleaseSaveEntries(m_savesInfo);
        delete m_savesInfo;
        m_savesInfo = NULL;
    }
}

C4JStorage::ESaveGameState CStorage::LoadSaveDataThumbnail(SAVE_INFO* info,
                                                           int (*callback)(void*, unsigned char*, unsigned int),
                                                           void* context)
{
    callback(context, info->thumbnail, info->thumbnailSize);
    return C4JStorage::ESaveGame_GetThumbnail;
}

unsigned int CStorage::LoadGameThreadProc()
{
    std::vector<unsigned char> contents;
    bool loaded;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);
        loaded = ReadWholeFile(SaveFolder(m_saveUniqueFilename) / SAVE_DATA_FILE, contents);
    }
    if (loaded)
    {
        std::free(m_saveData);
        m_saveData = static_cast<unsigned char*>(std::malloc(contents.empty() ? 1 : contents.size()));
        if (m_saveData != NULL)
        {
            if (!contents.empty())
                std::memcpy(m_saveData, contents.data(), contents.size());
            m_saveSize = static_cast<unsigned int>(contents.size());
            m_saveState = C4JStorage::ESaveGame_LoadComplete;
            return 0;
        }
        m_saveSize = 0;
    }
    m_saveState = C4JStorage::ESaveGame_LoadFailed;
    return 0;
}

C4JStorage::ESaveGameState CStorage::LoadSaveData(SAVE_INFO* info, LoadCallback callback, void* context)
{
    m_loadCallback = callback;
    m_loadCallbackContext = context;
    if (m_saveState != C4JStorage::ESaveGame_Idle)
        return C4JStorage::ESaveGame_Idle;
    SetSaveUniqueFilename(info->filename);
    std::memcpy(m_saveTitle, info->title, sizeof(m_saveTitle));
    m_saveState = C4JStorage::ESaveGame_Load;
    StartThread(m_saveThreads[1], &CStorage::LoadGameThreadProc);
    return C4JStorage::ESaveGame_Load;
}

int CStorage::GetSaveSize()
{
    return (int)m_saveSize;
}

void CStorage::GetSaveData(void* destination, unsigned int* size)
{
    if (destination != NULL)
    {
        if (m_saveSize != 0)
            std::memcpy(destination, m_saveData, m_saveSize);
        *size = m_saveSize;
    }
    else
        *size = 0;
}

int CStorage::GetSaveUniqueNumber(int*)
{
    return 1;
}

int CStorage::GetSaveUniqueFilename(char*)
{
    return 0;
}

void CStorage::SetSaveUniqueFilename(char* filename)
{
    std::strncpy(m_saveUniqueFilename, filename, sizeof(m_saveUniqueFilename) - 1);
    m_saveUniqueFilename[sizeof(m_saveUniqueFilename) - 1] = '\0';
}

int CStorage::GetSaveUniqueFileDir(char* filename)
{
    std::strcpy(filename, m_saveUniqueFilename);
    return 1;
}

void CStorage::CreateSaveUniqueName()
{
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm calendar;
#if defined(_WIN32)
    localtime_s(&calendar, &now);
#else
    localtime_r(&now, &calendar);
#endif
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "%2d%02d%02d%02d%02d%02d", calendar.tm_year % 100, calendar.tm_mon,
                  calendar.tm_mday, calendar.tm_hour, calendar.tm_min, calendar.tm_sec);
    std::strcpy(m_saveUniqueFilename, m_gameSaveFolderPrefix);
    std::strcat(m_saveUniqueFilename, suffix);
}

void CStorage::SetSaveTitle(const wchar_t* title)
{
    CreateSaveUniqueName();
    std::wcsncpy(m_saveTitle, title, 127);
    m_saveTitle[127] = L'\0';
}

void* CStorage::AllocateSaveData(unsigned int size)
{
    std::free(m_saveData);
    m_saveData = static_cast<unsigned char*>(std::malloc(size != 0 ? size : 1));
    if (m_saveData != NULL)
        m_saveSize = size;
    return m_saveData;
}

void CStorage::SetSaveDataSize(unsigned int size)
{
    m_saveSize = size;
}

void CStorage::GetDefaultSaveImage(unsigned char** data, unsigned int* size)
{
    *data = m_defaultImage;
    *size = m_defaultImageSize;
}

void CStorage::GetDefaultSaveThumbnail(unsigned char** data, unsigned int* size)
{
    *data = m_defaultThumbnail;
    *size = m_defaultThumbnailSize;
}

void CStorage::AddTextFieldToPNG(unsigned char* data, unsigned int imageSize, unsigned char* text,
                                 unsigned int textSize, unsigned int)
{
    if (imageSize < 8 || data[0] != 0x89 || data[1] != 'P' || data[2] != 'N' || data[3] != 'G' || data[4] != 13
        || data[5] != 10 || data[6] != 26 || data[7] != 10)
        return;

    unsigned int offset = 8;
    while (offset + 8 <= imageSize)
    {
        unsigned int chunkStart = offset;
        unsigned int length = ReadPngChunkWord(data + offset);
        offset += 4;
        unsigned int type = ReadPngChunkWord(data + offset);
        offset += 4;
        if (type == PNG_CHUNK_TYPE_IEND)
        {
            offset = chunkStart;
            WritePngChunkWord(data + offset, textSize);
            offset += 4;
            unsigned char* textChunk = data + offset;
            WritePngChunkWord(data + offset, PNG_CHUNK_TYPE_TEXT);
            offset += 4;
            std::memcpy(data + offset, text, textSize);
            offset += textSize;
            WritePngChunkWord(data + offset, CRC(textChunk, textSize + 4));
            offset += 4;
            WritePngChunkWord(data + offset, 0);
            offset += 4;
            unsigned char* endChunk = data + offset;
            WritePngChunkWord(data + offset, PNG_CHUNK_TYPE_IEND);
            offset += 4;
            WritePngChunkWord(data + offset, CRC(endChunk, 4));
            offset += 4;
        }
        else
        {
            offset += length;
            offset += 4;
        }
    }
}

void CStorage::SetSaveImages(unsigned char* image, unsigned int imageSize, unsigned char* thumbnail,
                             unsigned int thumbnailSize, unsigned char* text, unsigned int textSize)
{
    std::free(m_saveThumbnail);
    m_saveThumbnail = NULL;
    m_saveThumbnailSize = 0;
    if (thumbnail != NULL && thumbnailSize != 0)
    {
        m_saveThumbnail = static_cast<unsigned char*>(std::malloc(thumbnailSize));
        if (m_saveThumbnail != NULL)
        {
            std::memcpy(m_saveThumbnail, thumbnail, thumbnailSize);
            m_saveThumbnailSize = thumbnailSize;
        }
    }

    std::free(m_saveImage);
    m_saveImage = NULL;
    m_saveImageSize = 0;
    if (image == NULL || imageSize == 0)
        return;
    unsigned int size = imageSize;
    if (textSize != 0)
        size += textSize + 12;
    m_saveImage = static_cast<unsigned char*>(std::malloc(size));
    if (m_saveImage == NULL)
        return;
    m_saveImageSize = size;
    std::memset(m_saveImage, 0, size);
    std::memcpy(m_saveImage, image, imageSize);
    if (textSize != 0)
        AddTextFieldToPNG(m_saveImage, imageSize, text, textSize, size);
}

unsigned int CStorage::SaveGameThreadProc()
{
    if (m_savesInfo == NULL)
    {
        m_savesInfo = new SAVE_DETAILS;
        std::memset(m_savesInfo, 0, sizeof(SAVE_DETAILS));
    }
    fs::path folder = SaveFolder(m_saveUniqueFilename);
    std::string title = WideToUtf8(m_saveTitle, sizeof(m_saveTitle) / sizeof(wchar_t));
    unsigned int replacedSize = 0;
    unsigned int requiredSize = m_saveSize + m_saveImageSize + static_cast<unsigned int>(title.size());
    EWriteResult result;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);
        replacedSize = static_cast<unsigned int>(FileSizeOrZero(folder / SAVE_DATA_FILE)
                                                 + FileSizeOrZero(folder / SAVE_TITLE_FILE)
                                                 + FileSizeOrZero(folder / SAVE_IMAGE_FILE));
        result = WriteWholeFile(folder / SAVE_DATA_FILE, m_saveData, m_saveSize);
        if (result == eWriteResult_Ok)
        {
            if (m_saveImageSize != 0)
                result = WriteWholeFile(folder / SAVE_IMAGE_FILE, m_saveImage, m_saveImageSize);
            else
            {
                std::error_code error;
                fs::remove(folder / SAVE_IMAGE_FILE, error);
            }
        }
        if (result == eWriteResult_Ok)
        {
            if (m_saveThumbnailSize != 0)
                result = WriteWholeFile(folder / SAVE_THUMBNAIL_FILE, m_saveThumbnail, m_saveThumbnailSize);
            else
            {
                std::error_code error;
                fs::remove(folder / SAVE_THUMBNAIL_FILE, error);
            }
        }
        if (result == eWriteResult_Ok)
            result = WriteTextFile(folder / SAVE_TITLE_FILE, title);
        if (result == eWriteResult_Ok)
            result = WriteModifiedTime(folder);
    }
    if (result == eWriteResult_NoSpace)
    {
        m_incompleteReason = 1;
        m_saveState = C4JStorage::ESaveGame_SaveIncomplete;
        return 0;
    }
    if (result != eWriteResult_Ok)
    {
        m_saveState = C4JStorage::ESaveGame_SaveFailed;
        return 0;
    }
    m_savesInfo->nandSize += requiredSize - replacedSize;
    m_saveState = C4JStorage::ESaveGame_SaveComplete;
    return 0;
}

C4JStorage::ESaveGameState CStorage::SaveSaveData(SaveCallback callback, void* context)
{
    m_saveCallback = callback;
    m_saveCallbackContext = context;
    if (m_saveState != C4JStorage::ESaveGame_Idle && m_saveState != C4JStorage::ESaveGame_SaveIncompleteWaiting)
        return C4JStorage::ESaveGame_Idle;
    m_saveState = C4JStorage::ESaveGame_Save;
    StartThread(m_saveThreads[0], &CStorage::SaveGameThreadProc);
    return C4JStorage::ESaveGame_Save;
}

void CStorage::ContinueIncompleteOperation()
{
    if (m_saveState == C4JStorage::ESaveGame_SaveIncompleteWaiting)
        SaveSaveData(m_saveCallback, m_saveCallbackContext);
}

void CStorage::CancelIncompleteOperation()
{
    if (m_saveState == C4JStorage::ESaveGame_SaveIncompleteWaiting)
        m_saveState = C4JStorage::ESaveGame_SaveCancelled;
}

unsigned int CStorage::DeleteGameThreadProc()
{
    bool deleted = false;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);
        fs::path folder = SaveFolder(m_selectedSave->filename);
        std::error_code error;
        if (m_selectedSave->filename[0] != '\0' && fs::is_directory(folder, error))
        {
            fs::remove_all(folder, error);
            deleted = !error;
        }
    }
    if (deleted)
    {
        m_savesInfo->nandSize -= m_selectedSave->nandSize;
        m_selectedSave->nandSize = 0;
        m_saveState = C4JStorage::ESaveGame_DeleteComplete;
    }
    else
        m_saveState = C4JStorage::ESaveGame_DeleteFailed;
    return 0;
}

C4JStorage::ESaveGameState CStorage::DeleteSaveData(SAVE_INFO* info, SaveCallback callback, void* context)
{
    C4JStorage::ESaveGameState oldState = m_saveState;
    m_deleteCallback = callback;
    m_deleteCallbackContext = context;
    m_selectedSave = info;
    if (oldState != C4JStorage::ESaveGame_Idle && oldState != C4JStorage::ESaveGame_SaveIncompleteWaiting)
        return C4JStorage::ESaveGame_Idle;
    m_saveState = C4JStorage::ESaveGame_Delete;
    m_previousSaveState = oldState;
    StartThread(m_saveThreads[3], &CStorage::DeleteGameThreadProc);
    return C4JStorage::ESaveGame_Delete;
}

unsigned int CStorage::CopyGameThreadProc()
{
    std::lock_guard<std::mutex> lock(m_storageMutex);
    fs::path source = SaveFolder(m_saveUniqueFilename);
    std::vector<unsigned char> save;
    std::vector<unsigned char> title;
    if (!ReadWholeFile(source / SAVE_DATA_FILE, save) || !ReadWholeFile(source / SAVE_TITLE_FILE, title))
    {
        m_saveState = C4JStorage::ESaveGame_CopyFailed;
        return 0;
    }
    std::vector<unsigned char> image;
    bool hasImage = ReadWholeFile(source / SAVE_IMAGE_FILE, image);
    std::vector<unsigned char> thumbnail;
    bool hasThumbnail = ReadWholeFile(source / SAVE_THUMBNAIL_FILE, thumbnail);

    unsigned long long required = save.size() + title.size() + image.size() + thumbnail.size();
    std::error_code error;
    fs::create_directories(GamesFolder(), error);
    fs::space_info space = fs::space(GamesFolder(), error);
    if (!error && space.available < required)
    {
        m_saveState = C4JStorage::ESaveGame_CopyFailedNoSpace;
        return 0;
    }

    CreateSaveUniqueName();
    fs::path destination = SaveFolder(m_saveUniqueFilename);
    EWriteResult result = WriteWholeFile(destination / SAVE_DATA_FILE, save.data(), save.size());
    if (result == eWriteResult_Ok && hasImage)
        result = WriteWholeFile(destination / SAVE_IMAGE_FILE, image.data(), image.size());
    if (result == eWriteResult_Ok && hasThumbnail)
        result = WriteWholeFile(destination / SAVE_THUMBNAIL_FILE, thumbnail.data(), thumbnail.size());
    if (result == eWriteResult_Ok)
        result = WriteWholeFile(destination / SAVE_TITLE_FILE, title.data(), title.size());
    if (result == eWriteResult_Ok)
        result = WriteModifiedTime(destination);

    if (result == eWriteResult_Ok)
    {
        m_saveState = C4JStorage::ESaveGame_CopyComplete;
        return 0;
    }
    fs::remove_all(destination, error);
    m_saveState = result == eWriteResult_NoSpace ? C4JStorage::ESaveGame_CopyFailedNoSpace
                                                 : C4JStorage::ESaveGame_CopyFailed;
    return 0;
}

C4JStorage::ESaveGameState CStorage::CopySaveData(SAVE_INFO* info, CopyCallback callback,
                                                  CopyProgressCallback progressCallback, void* context)
{
    m_copyCallback = callback;
    m_copyProgressCallback = progressCallback;
    m_copyCallbackContext = context;
    if (m_saveState != C4JStorage::ESaveGame_Idle)
        return C4JStorage::ESaveGame_Idle;
    SetSaveUniqueFilename(info->filename);
    std::memcpy(m_saveTitle, info->title, sizeof(m_saveTitle));
    m_saveState = C4JStorage::ESaveGame_Copy;
    StartThread(m_saveThreads[5], &CStorage::CopyGameThreadProc);
    return C4JStorage::ESaveGame_Copy;
}

unsigned int CStorage::RenameGameThreadProc()
{
    EWriteResult result = eWriteResult_Failed;
    {
        std::lock_guard<std::mutex> lock(m_storageMutex);
        fs::path folder = SaveFolder(m_savesInfo->saves[m_renameSaveIndex].filename);
        std::error_code error;
        if (fs::exists(folder / SAVE_TITLE_FILE, error))
        {
            result = WriteTextFile(folder / SAVE_TITLE_FILE,
                                   WideToUtf8(m_renameTitle, sizeof(m_renameTitle) / sizeof(wchar_t)));
            result = CombineWriteResults(result, WriteModifiedTime(folder));
        }
    }
    if (result == eWriteResult_Ok)
    {
        std::memcpy(m_savesInfo->saves[m_renameSaveIndex].title, m_renameTitle, sizeof(m_renameTitle));
        m_saveState = C4JStorage::ESaveGame_RenameComplete;
        return 0;
    }
    m_saveState = C4JStorage::ESaveGame_RenameFailed;
    return 0;
}

C4JStorage::ESaveGameState CStorage::RenameSaveData(int index, wchar_t* title, SaveCallback callback, void* context)
{
    m_renameCallbackContext = context;
    m_renameCallback = callback;
    m_renameSaveIndex = index;
    std::wcsncpy(m_renameTitle, title, 127);
    m_renameTitle[127] = L'\0';
    if (m_saveState != C4JStorage::ESaveGame_Idle)
        return C4JStorage::ESaveGame_Idle;
    if (m_savesInfo == NULL || index < 0 || index >= m_savesInfo->count)
        return C4JStorage::ESaveGame_Idle;
    m_saveState = C4JStorage::ESaveGame_Rename;
    StartThread(m_saveThreads[4], &CStorage::RenameGameThreadProc);
    return C4JStorage::ESaveGame_Rename;
}

void CStorage::GetSaveImage(unsigned char** data, int* size)
{
    *data = m_saveImage;
    *size = (int)m_saveImageSize;
}

void CStorage::GetSaveThumbnail(unsigned char** data, int* size)
{
    *data = m_saveThumbnail;
    *size = (int)m_saveThumbnailSize;
}

void CStorage::InitSaveGame(unsigned int, const wchar_t*, const char*, int, bool* exists)
{
    m_saveState = C4JStorage::ESaveGame_Idle;
    std::memset(m_saveUniqueFilename, 0, sizeof(m_saveUniqueFilename));
    std::memset(m_saveTitle, 0, sizeof(m_saveTitle));
    std::memset(m_gameSaveFolderTitle, 0, sizeof(m_gameSaveFolderTitle));
    std::memset(m_saveCacheFolderTitle, 0, sizeof(m_saveCacheFolderTitle));
    std::memset(m_optionsFolderTitle, 0, sizeof(m_optionsFolderTitle));
    std::memset(m_gameSaveFolderPrefix, 0, sizeof(m_gameSaveFolderPrefix));

    m_loadCallback = NULL;
    m_savesInfoCallback = NULL;
    m_maxSaves = 99;
    m_saveCallbackContext = NULL;
    m_saveSize = 0;
    m_copyCallbackContext = NULL;
    m_renameCallback = NULL;
    m_incompleteReason = 0;
    m_deleteCallback = NULL;
    m_savesInfoState = 0;
    m_saveCallback = NULL;
    m_copyProgressCallback = NULL;
    m_deleteCallbackContext = NULL;
    m_copyCallback = NULL;
    m_savesInfoContext = NULL;
    m_savesInfoAvailable = false;
    m_incompleteSaveCallback = NULL;
    m_loadCallbackContext = NULL;
    m_saveDisabled = false;
    m_incompleteSaveContext = NULL;
    m_renameCallbackContext = NULL;

    std::error_code error;
    fs::create_directories(GamesFolder(), error);
    if (exists != NULL)
        *exists = error == std::errc::no_space_on_device;
}

void CStorage::SetDefaultImages(unsigned char* image, unsigned int imageSize, unsigned char* thumbnail,
                                unsigned int thumbnailSize)
{
    std::free(m_defaultImage);
    m_defaultImage = NULL;
    m_defaultImageSize = 0;
    if (image != NULL && imageSize != 0)
    {
        m_defaultImage = static_cast<unsigned char*>(std::malloc(imageSize));
        if (m_defaultImage != NULL)
        {
            std::memcpy(m_defaultImage, image, imageSize);
            m_defaultImageSize = imageSize;
        }
    }
    std::free(m_defaultThumbnail);
    m_defaultThumbnail = NULL;
    m_defaultThumbnailSize = 0;
    if (thumbnail != NULL && thumbnailSize != 0)
    {
        m_defaultThumbnail = static_cast<unsigned char*>(std::malloc(thumbnailSize));
        if (m_defaultThumbnail != NULL)
        {
            std::memcpy(m_defaultThumbnail, thumbnail, thumbnailSize);
            m_defaultThumbnailSize = thumbnailSize;
        }
    }
}

void CStorage::TickSaveGame()
{
    C4JStorage::ESaveGameState state = m_saveState;

    if (state == C4JStorage::ESaveGame_SaveComplete)
    {
        JoinThread(m_saveThreads[0]);
        m_saveState = C4JStorage::ESaveGame_Idle;
        state = C4JStorage::ESaveGame_Idle;

        if (m_saveCallback != NULL)
        {
            m_saveCallback(m_saveCallbackContext, true);
            state = m_saveState;
        }
    }
    else if (state == C4JStorage::ESaveGame_SaveFailed || state == C4JStorage::ESaveGame_SaveCancelled)
    {
        JoinThread(m_saveThreads[0]);

        if (m_saveCallback != NULL)
            m_saveCallback(m_saveCallbackContext, false);

        m_saveState = C4JStorage::ESaveGame_Idle;
        state = C4JStorage::ESaveGame_Idle;
    }

    if (state == C4JStorage::ESaveGame_LoadComplete || state == C4JStorage::ESaveGame_LoadFailed)
    {
        JoinThread(m_saveThreads[1]);

        if (m_loadCallback != NULL)
            m_loadCallback(m_loadCallbackContext, state == C4JStorage::ESaveGame_LoadFailed, true);

        m_saveState = C4JStorage::ESaveGame_Idle;
        state = C4JStorage::ESaveGame_Idle;
    }

    if (m_savesInfoState == 2)
    {
        JoinThread(m_saveThreads[2]);
        m_savesInfoAvailable = true;

        if (m_savesInfoCallback != NULL)
            m_savesInfoCallback(m_savesInfoContext, m_savesInfo, true);

        state = m_saveState;
        m_savesInfoState = 0;
    }

    if (state == C4JStorage::ESaveGame_DeleteComplete || state == C4JStorage::ESaveGame_DeleteFailed)
    {
        JoinThread(m_saveThreads[3]);

        if (state == C4JStorage::ESaveGame_DeleteComplete && m_savesInfo != NULL && m_savesInfo->saves != NULL
            && m_selectedSave >= m_savesInfo->saves && m_selectedSave < m_savesInfo->saves + m_savesInfo->count)
        {
            int index = (int)(m_selectedSave - m_savesInfo->saves);
            delete[] m_savesInfo->saves[index].thumbnail;
            for (int i = index; i < m_savesInfo->count - 1; ++i)
                m_savesInfo->saves[i] = m_savesInfo->saves[i + 1];
            --m_savesInfo->count;
            std::memset(&m_savesInfo->saves[m_savesInfo->count], 0, sizeof(SAVE_INFO));
        }

        if (m_deleteCallback != NULL)
            m_deleteCallback(m_deleteCallbackContext, true);

        m_saveState = m_previousSaveState;
        state = m_saveState;
    }

    if (state == C4JStorage::ESaveGame_RenameComplete || state == C4JStorage::ESaveGame_RenameFailed)
    {
        JoinThread(m_saveThreads[4]);

        if (m_renameCallback != NULL)
        {
            m_renameCallback(m_renameCallbackContext, true);
            m_saveState = C4JStorage::ESaveGame_Idle;
            return;
        }
    }
    else if (state == C4JStorage::ESaveGame_CopyComplete)
    {
        JoinThread(m_saveThreads[5]);

        if (m_copyCallback != NULL)
        {
            m_copyCallback(m_copyCallbackContext, true, m_saveState);
            m_saveState = C4JStorage::ESaveGame_Idle;
            return;
        }
    }
    else if (state >= C4JStorage::ESaveGame_CopyFailed && state <= C4JStorage::ESaveGame_CopyFailedOther)
    {
        JoinThread(m_saveThreads[5]);

        if (m_copyCallback != NULL)
        {
            m_copyCallback(m_copyCallbackContext, false, m_saveState);
            m_saveState = C4JStorage::ESaveGame_Idle;
            return;
        }
    }
    else
    {
        if (state != C4JStorage::ESaveGame_SaveIncomplete)
            return;

        JoinThread(m_saveThreads[0]);
        if (m_incompleteSaveCallback != NULL)
        {
            m_saveState = C4JStorage::ESaveGame_SaveIncompleteWaiting;
            m_incompleteSaveCallback(m_incompleteSaveContext, (C4JStorage::ESaveIncompleteType)m_incompleteReason);
            return;
        }

        if (m_saveCallback != NULL)
            m_saveCallback(m_saveCallbackContext, false);
    }

    m_saveState = C4JStorage::ESaveGame_Idle;
}

void CStorage::SetOpenDirFn(DirectoryOpenCallback callback)
{
    m_openDir = callback;
}

void CStorage::SetReadDirFn(DirectoryReadCallback callback)
{
    m_readDir = callback;
}

void CStorage::SetCloseDirFn(DirectoryCloseCallback callback)
{
    m_closeDir = callback;
}

void CStorage::SetDLCPackageRoot(const char* root)
{
    std::strncpy(m_defaultDLCMountName, root, sizeof(m_defaultDLCMountName) - 1);
    m_defaultDLCMountName[sizeof(m_defaultDLCMountName) - 1] = '\0';
}

bool CStorage::ListDirectory(const std::string& path, std::vector<DirectoryEntry>& entries)
{
    entries.clear();
    if (m_openDir == NULL || m_readDir == NULL || m_closeDir == NULL)
        return false;
    if (!m_openDir(path.c_str(), &m_dlcDirectory))
        return false;
    while (m_readDir(&m_dlcDirectory, &m_dlcDirectoryEntry))
    {
        DirectoryEntry entry;
        entry.name = m_dlcDirectoryEntry.name;
        entry.isDirectory = m_dlcDirectoryEntry.isDirectory;
        entry.size = m_dlcDirectoryEntry.size;
        entries.push_back(entry);
    }
    m_closeDir(&m_dlcDirectory);
    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry& left, const DirectoryEntry& right) { return left.name < right.name; });
    return true;
}

void CStorage::AddInstalled(CONTENT_DATA& content)
{
    m_installedContent.push_back(content);
}

static bool IsPackFile(const std::string& name)
{
    if (name.size() < 4)
        return false;
    std::string extension = name.substr(name.size() - 4);
    for (size_t index = 0; index < extension.size(); ++index)
        extension[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(extension[index])));
    return extension == ".pck";
}

void CStorage::AddPack(const std::string& path, unsigned char source, const char* label)
{
    const std::string::size_type slash = path.find_last_of('/');
    CONTENT_DATA content;
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (label != NULL && std::strlen(label) < sizeof(content.name))
        name = label;
    std::memset(&content, 0, sizeof(content));
    if (name.size() >= sizeof(content.name) || path.size() >= sizeof(content.path))
        return;
    content.source = source;
    std::strcpy(content.name, name.c_str());
    std::strcpy(content.path, path.c_str());
    content.index = -1;
    AddInstalled(content);
}

unsigned int CStorage::EnumerateLocalDLCPackages()
{
    m_installedContent.clear();
    for (unsigned int index = 0; index < BUILT_IN_PACK_COUNT; ++index)
        AddPack(BUILT_IN_PACKS[index], EPackSource_BuiltIn);

    // Also discover user-owned DLC in the imported game content. The fixed list
    // above covers packs bundled with the game, while installed/owned content
    // can vary between users and update dumps.
    std::unordered_set<std::string> knownPaths;
    for (std::vector<CONTENT_DATA>::const_iterator it = m_installedContent.begin(); it != m_installedContent.end(); ++it)
    {
        std::string path = it->path;
        std::transform(path.begin(), path.end(), path.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        knownPaths.insert(path);
    }
    const char* const dlcRoots[] = {"/vol/content/WiiU/DLC", "/vol/content/WiiU/CU/DLC"};
    for (unsigned int rootIndex = 0; rootIndex < sizeof(dlcRoots) / sizeof(dlcRoots[0]); ++rootIndex)
    {
        std::vector<DirectoryEntry> dlcEntries;
        if (!ListDirectory(dlcRoots[rootIndex], dlcEntries))
            continue;
        for (std::vector<DirectoryEntry>::const_iterator it = dlcEntries.begin(); it != dlcEntries.end(); ++it)
        {
            if (it->name == "." || it->name == ".." || (!it->isDirectory && !IsPackFile(it->name)))
                continue;
            const std::string path = std::string(dlcRoots[rootIndex]) + "/" + it->name;
            std::string normalizedPath = path;
            std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (knownPaths.insert(normalizedPath).second)
                AddPack(path, EPackSource_BuiltIn);
        }
    }

    const std::string userPacksRoot = "/vol/packs";
    const std::string bundledPacksRoot = PcBundledPacksRoot();
    std::string normalizedUserRoot = fs::u8path(PcPacksRoot()).lexically_normal().generic_string();
    std::string normalizedBundledRoot = fs::u8path(bundledPacksRoot).lexically_normal().generic_string();
    std::transform(normalizedUserRoot.begin(), normalizedUserRoot.end(), normalizedUserRoot.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(normalizedBundledRoot.begin(), normalizedBundledRoot.end(), normalizedBundledRoot.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::error_code error;
    fs::create_directories(fs::u8path(PcPacksRoot()), error);
    for (unsigned int rootIndex = 0; rootIndex < 2; ++rootIndex)
    {
        if (rootIndex == 1 && normalizedBundledRoot == normalizedUserRoot)
            continue;

        const std::string& root = rootIndex == 0 ? userPacksRoot : bundledPacksRoot;
        std::vector<DirectoryEntry> entries;
        if (!ListDirectory(root, entries))
            continue;

        for (size_t index = 0; index < entries.size(); ++index)
        {
            const DirectoryEntry& entry = entries[index];
            if (entry.name == "." || entry.name == "..")
                continue;
            if (entry.isDirectory || IsPackFile(entry.name))
            {
                const std::string path = rootIndex == 0
                                             ? root + "/" + entry.name
                                             : (fs::u8path(root) / fs::u8path(entry.name)).generic_string();
                std::string normalizedPath = path;
                std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (knownPaths.insert(normalizedPath).second)
                    AddPack(path, EPackSource_PacksFolder);
            }
        }
    }
    return 1;
}

int CStorage::GetInstalledDLC(int, InstalledDLCCallback callback, void* context)
{
    if (m_dlcState != 0)
        return 5;
    m_installedCallback = callback;
    m_installedContext = context;
    if (EnumerateLocalDLCPackages() != 0)
    {
        m_dlcState = 1;
        return 5;
    }
    return 1;
}

intptr_t CStorage::GetDLC(unsigned int index)
{
    return reinterpret_cast<intptr_t>(&m_installedContent[0] + index);
}

void CStorage::MountInstalledContent()
{
    CONTENT_DATA& content = m_installedContent[m_mountedDLCIndex];
    {
        std::lock_guard<std::mutex> lock(m_mountedDrivesMutex);
        m_mountedDrives.push_back(DriveMapping(m_dlcMountName, content.path));
    }
    m_dlcMountState = 1;
}

int CStorage::MountInstalledDLC(int, unsigned int index, DLCMountCallback callback, void* context,
                                const char* mountName)
{
    if (m_dlcMountState != 0)
        return DLC_MOUNT_PENDING;
    if (index >= m_installedContent.size())
        return 0;
    m_dlcMountCallback = callback;
    m_dlcMountContext = context;
    m_dlcMountName = mountName == NULL ? m_defaultDLCMountName : mountName;
    m_mountedDLCIndex = index;
    MountInstalledContent();
    return DLC_MOUNT_PENDING;
}

int CStorage::UnmountInstalledDLC(const char* drive)
{
    const char* name = drive == NULL ? m_defaultDLCMountName : drive;
    std::lock_guard<std::mutex> lock(m_mountedDrivesMutex);
    for (std::vector<DriveMapping>::iterator mapping = m_mountedDrives.begin(); mapping != m_mountedDrives.end();
         ++mapping)
    {
        if (mapping->drive == name)
        {
            m_mountedDrives.erase(mapping);
            return 0;
        }
    }
    return 0;
}

int CStorage::GetMountedDLCFileList(const char*, std::vector<std::string>& files)
{
    if (m_mountedDLCIndex >= m_installedContent.size())
        return 0;
    CONTENT_DATA& content = m_installedContent[m_mountedDLCIndex];
    std::vector<DirectoryEntry> entries;
    if (!ListDirectory(content.path, entries))
    {
        if (IsPackFile(content.path))
            files.push_back(content.path);
        return 0;
    }
    for (size_t index = 0; index < entries.size(); ++index)
    {
        if (entries[index].isDirectory)
            continue;
        std::string lowered;
        for (size_t character = 0; character < entries[index].name.size(); ++character)
            lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(entries[index].name[character])));
        if (lowered == "productid.txt")
            continue;
        files.push_back(std::string(content.path) + "/" + entries[index].name);
    }
    return 0;
}

int CStorage::GetMountedDLCIndex()
{
    return (int)m_mountedDLCIndex;
}

std::string CStorage::GetMountedPath(std::string path)
{
    for (std::string::size_type position = 0; position < path.size(); ++position)
    {
        if (path[position] == '/' || path[position] == '\\')
            return std::string();
        if (path[position] == ':')
        {
            std::string drive = path.substr(0, position);
            std::lock_guard<std::mutex> lock(m_mountedDrivesMutex);
            for (std::vector<DriveMapping>::iterator mapping = m_mountedDrives.begin();
                 mapping != m_mountedDrives.end(); ++mapping)
            {
                if (mapping->drive == drive)
                    return mapping->path + path.substr(position + 1);
            }
            break;
        }
    }
    return std::string();
}

std::string CStorage::GetPacksFolder()
{
    return PcPacksRoot();
}

std::string CStorage::GetPackCacheFolder()
{
    return "/vol/save/packcache";
}

int CStorage::AddInstalledPack(const char* path, const char* name)
{
    const size_t count = m_installedContent.size();
    AddPack(path, EPackSource_Server, name);
    return m_installedContent.size() > count ? static_cast<int>(count) : -1;
}

void CStorage::ForgetServerPacks()
{
    std::vector<CONTENT_DATA>::iterator content = m_installedContent.begin();
    while (content != m_installedContent.end())
    {
        if (content->source == EPackSource_Server)
            content = m_installedContent.erase(content);
        else
            ++content;
    }
}

bool CStorage::ListFilesUnder(const std::string& folder, const std::string& prefix, std::vector<std::string>& paths,
                              std::vector<unsigned long long>& sizes)
{
    std::vector<DirectoryEntry> entries;
    if (!ListDirectory(folder, entries))
        return false;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const DirectoryEntry& entry = entries[index];
        if (entry.name == "." || entry.name == "..")
            continue;
        const std::string relative = prefix.empty() ? entry.name : prefix + "/" + entry.name;
        if (entry.isDirectory)
        {
            if (!ListFilesUnder(folder + "/" + entry.name, relative, paths, sizes))
                return false;
        }
        else
        {
            paths.push_back(relative);
            sizes.push_back(entry.size);
        }
    }
    return true;
}

bool CStorage::ListFiles(const std::string& root, std::vector<std::string>& paths,
                         std::vector<unsigned long long>& sizes)
{
    paths.clear();
    sizes.clear();
    return ListFilesUnder(root, std::string(), paths, sizes);
}

bool CStorage::DeletePath(const std::string& path)
{
    return PcRemoveAll(path.c_str());
}

bool CStorage::RenamePath(const std::string& from, const std::string& to)
{
    return PcRename(from.c_str(), to.c_str());
}

bool CStorage::CreateDirectories(const std::string& path)
{
    return PcCreateDirectories(path.c_str());
}

void CStorage::DLCOnCreate()
{
    m_dlcState = 0;
    m_dlcMountState = 0;
}

void CStorage::TickDLC()
{
    if (m_dlcState == 1)
    {
        m_dlcState = 0;
        int count = (int)m_installedContent.size();
        if (m_installedCallback != NULL)
            m_installedCallback(m_installedContext, count, 0);
    }
    if (m_dlcMountState == 1)
    {
        m_dlcMountState = 0;
        if (m_dlcMountCallback != NULL)
            m_dlcMountCallback(m_dlcMountContext, 0, 0, m_dlcMountResult);
    }
}

void CStorage::InitialiseGenericData()
{
    m_genericState = 0;
    m_genericLoadedData = NULL;
    m_genericContext = NULL;
    m_genericWriteData = NULL;
    m_genericCallback = NULL;
    m_genericDataSize = 0;
}

int CStorage::ReadFromGenericData(int slot, const std::wstring& name, void** data, unsigned int* size,
                                  GenericDataCallback callback, void* context)
{
    if (slot >= 0 && slot < 4 && data != NULL && size != NULL && m_genericState == 0)
    {
        m_genericFilename.assign(name);
        m_genericReadSize = size;
        m_genericContext = context;
        m_genericSlot = slot;
        m_genericReadData = data;
        m_genericCallback = callback;
        m_genericState = 7;
        return 1;
    }
    return 0;
}

int CStorage::WriteToGenericData(int slot, const std::wstring& name, void* data, unsigned int size,
                                 GenericDataCallback callback, void* context)
{
    if (slot >= 0 && slot < 4 && data != NULL && size != 0 && m_genericState == 0)
    {
        m_genericFilename = name;
        m_genericWriteData = data;
        m_genericDataSize = size;
        m_genericCallback = callback;
        m_genericSlot = slot;
        m_genericContext = context;
        m_genericState = 1;
        return 1;
    }
    return 0;
}

void CStorage::DeleteGenericData(int slot, const std::wstring& name)
{
    if (slot < 0 || slot >= 4)
        return;
    std::lock_guard<std::mutex> lock(m_storageMutex);
    if (!gProfile->IsSignedIn(slot) || gProfile->IsGuest(slot))
        return;
    std::error_code error;
    fs::remove(GenericDataPath(slot, name), error);
}

unsigned int CStorage::LoadGenericDataThreadProc(int slot)
{
    std::lock_guard<std::mutex> lock(m_storageMutex);
    if (gProfile->IsSignedIn(slot) && !gProfile->IsGuest(slot))
    {
        std::vector<unsigned char> contents;
        if (ReadWholeFile(GenericDataPath(slot, m_genericFilename), contents))
        {
            m_genericLoadedSize = static_cast<unsigned int>(contents.size());
            m_genericLoadedData = static_cast<unsigned char*>(std::malloc(contents.empty() ? 1 : contents.size()));
            if (m_genericLoadedData != NULL)
            {
                if (!contents.empty())
                    std::memcpy(m_genericLoadedData, contents.data(), contents.size());
                m_genericState = 9;
                return 0;
            }
        }
    }
    m_genericState = 10;
    return 0;
}

unsigned int CStorage::SaveGenericDataThreadProc(int slot)
{
    std::lock_guard<std::mutex> lock(m_storageMutex);
    if (gProfile->IsSignedIn(slot) && !gProfile->IsGuest(slot))
    {
        EWriteResult result = WriteWholeFile(GenericDataPath(slot, m_genericFilename), m_genericWriteData,
                                             m_genericDataSize);
        if (result == eWriteResult_Ok)
            m_genericState = 3;
        else
            m_genericState = result == eWriteResult_NoSpace ? 6 : 4;
        return 0;
    }
    m_genericState = 4;
    return 0;
}

void CStorage::GenericDataTick()
{
    switch (m_genericState)
    {
    case 0:
    case 2:
    case 8:
        break;
    case 1:
        m_genericState = 2;
        StartThread(m_genericThread, &CStorage::SaveGenericDataThreadProc, m_genericSlot);
        return;
    case 3:
        JoinThread(m_genericThread);
        m_genericState = 0;
        m_genericWriteData = NULL;
        if (m_genericCallback != NULL)
            m_genericCallback(m_genericContext, m_genericSlot, (C4JStorage::eGeneralData_Callback_Status)3, 0);
        return;
    case 4:
    case 5:
        JoinThread(m_genericThread);
        m_genericState = 0;
        m_genericWriteData = NULL;
        if (m_genericCallback != NULL)
            m_genericCallback(m_genericContext, m_genericSlot, (C4JStorage::eGeneralData_Callback_Status)4, 0);
        return;
    case 6:
        JoinThread(m_genericThread);
        m_genericState = 0;
        m_genericWriteData = NULL;
        if (m_genericCallback != NULL)
            m_genericCallback(m_genericContext, m_genericSlot, (C4JStorage::eGeneralData_Callback_Status)5, 0);
        return;
    case 7:
        m_genericState = 8;
        StartThread(m_genericThread, &CStorage::LoadGenericDataThreadProc, m_genericSlot);
        return;
    case 9:
        JoinThread(m_genericThread);
        *m_genericReadSize = m_genericLoadedSize;
        *m_genericReadData = m_genericLoadedData;
        m_genericLoadedData = NULL;
        m_genericState = 0;
        if (m_genericCallback != NULL)
            m_genericCallback(m_genericContext, m_genericSlot, C4JStorage::eGeneralData_Callback_StatusValue, 0);
        return;
    case 10:
    case 11:
        JoinThread(m_genericThread);
        *m_genericReadSize = 0;
        *m_genericReadData = NULL;
        m_genericState = 0;
        if (m_genericCallback != NULL)
            m_genericCallback(m_genericContext, m_genericSlot, (C4JStorage::eGeneralData_Callback_Status)1, 0);
        if (m_genericLoadedData != NULL)
        {
            std::free(m_genericLoadedData);
            m_genericLoadedData = NULL;
        }
        break;
    }
}
