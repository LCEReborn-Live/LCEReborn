#include "game/core/DLCManager.hpp"
#include "game/core/StringIds.hpp"

#include <algorithm>
#include <sstream>
#include <cstring>
#include <wchar.h>

#include "game/core/ConsoleUIController.hpp"
#include "game/core/CMinecraftApp.hpp"
#include "game/core/CConsoleMinecraftApp.hpp"
#include "game/core/DLCFile.hpp"
#include "game/core/DLCPack.hpp"
#include "game/core/GameRuleManager.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/System.hpp"
#include "game/client/render/TexturePackRepository.hpp"
#include "platform/CProfile.hpp"
#include "game/core/StringConversion.hpp"
#include "platform/FileSystem.hpp"
#include "platform/FileApi.hpp"
#include "platform/Runtime.hpp"

extern ConsoleUIController gConsoleUIController;

char* wstringtofilename(const std::wstring& text, char (&fileName)[256]);

const wchar_t* s_parameterNames[] = {L"DISPLAYNAME",
                                     L"THEMENAME",
                                     L"LOC_KEY",
                                     L"FREE",
                                     L"CREDIT",
                                     L"CAPEPATH",
                                     L"BOX",
                                     L"ANIM",
                                     L"GAME_FLAGS",
                                     L"PACKID",
                                     L"NETHERPARTICLECOLOUR",
                                     L"ENCHANTTEXTCOLOUR",
                                     L"ENCHANTTEXTFOCUSCOLOUR",
                                     L"DATAPATH",
                                     L"PACKVERSION",
                                     L"DISPLAYNAMEID",
                                     L"THEMENAMEID",
                                     L"CLOUDHEIGHT",
                                     L"CREDITID",
                                     L"DISABLEMIPMAP",
                                     L"OFFSET",
                                     L"XMLVERSION"};

char s_dlcDataFileName[256];

static bool DLCHostIsBigEndian()
{
    const unsigned int one = 1;
    return *(const unsigned char*)&one == 0;
}

static unsigned int ReadDLCWord(unsigned char* pData, unsigned int offset, bool bigEndian, bool convert)
{
    unsigned char* pWord = pData + offset;
    unsigned int value = bigEndian ? ((unsigned int)pWord[0] << 24) | ((unsigned int)pWord[1] << 16)
                                         | ((unsigned int)pWord[2] << 8) | (unsigned int)pWord[3]
                                   : ((unsigned int)pWord[3] << 24) | ((unsigned int)pWord[2] << 16)
                                         | ((unsigned int)pWord[1] << 8) | (unsigned int)pWord[0];
    if (convert)
    {
        std::memcpy(pWord, &value, sizeof(value));
    }
    return value;
}

static bool ReadDLCText(unsigned char* pData, unsigned int size, unsigned int offset, unsigned int length,
                        bool bigEndian, bool convert, std::wstring* pText)
{
    if (offset > size || (size - offset) / 2 < 2 || (size - offset) / 2 - 2 < length)
    {
        return false;
    }

    for (unsigned int i = 0; i < length + 2; i++)
    {
        unsigned char* pCharacter = pData + offset + i * 2;
        unsigned short character = bigEndian ? (unsigned short)((pCharacter[0] << 8) | pCharacter[1])
                                             : (unsigned short)((pCharacter[1] << 8) | pCharacter[0]);
        if (convert)
        {
            std::memcpy(pCharacter, &character, sizeof(character));
        }
        if (pText != NULL && i < length)
        {
            pText->push_back((wchar_t)character);
        }
    }

    return true;
}

static bool WalkDLCData(unsigned char* pData, unsigned int size, bool nestedPacks, bool bigEndian, bool toHost)
{
    if (size < 8)
    {
        return false;
    }

    const bool convert = toHost && bigEndian != DLCHostIsBigEndian();
    ReadDLCWord(pData, 0, bigEndian, convert);
    unsigned int parameterCount = ReadDLCWord(pData, 4, bigEndian, convert);
    unsigned int offset = 8;
    bool bHasXMLVersion = false;

    for (unsigned int i = 0; i < parameterCount; i++)
    {
        if (offset > size || size - offset < 8)
        {
            return false;
        }

        ReadDLCWord(pData, offset, bigEndian, convert);
        unsigned int nameLength = ReadDLCWord(pData, offset + 4, bigEndian, convert);
        std::wstring parameterName;
        if (!ReadDLCText(pData, size, offset + 8, nameLength, bigEndian, convert, &parameterName))
        {
            return false;
        }

        if (DLCManager::getParameterType(parameterName) == DLCManager::eDLCParameterType_XMLVersion)
        {
            bHasXMLVersion = true;
        }

        offset += nameLength * 2 + 12;
    }

    if (bHasXMLVersion)
    {
        if (offset > size || size - offset < 4)
        {
            return false;
        }

        ReadDLCWord(pData, offset, bigEndian, convert);
        offset += 4;
    }

    if (offset > size || size - offset < 4)
    {
        return false;
    }

    unsigned int itemCount = ReadDLCWord(pData, offset, bigEndian, convert);
    offset += 4;

    std::vector<unsigned int> itemSizes;
    std::vector<unsigned int> itemTypes;
    for (unsigned int i = 0; i < itemCount; i++)
    {
        if (offset > size || size - offset < 12)
        {
            return false;
        }

        itemSizes.push_back(ReadDLCWord(pData, offset, bigEndian, convert));
        itemTypes.push_back(ReadDLCWord(pData, offset + 4, bigEndian, convert));
        unsigned int nameLength = ReadDLCWord(pData, offset + 8, bigEndian, convert);
        if (!ReadDLCText(pData, size, offset + 12, nameLength, bigEndian, convert, NULL))
        {
            return false;
        }

        offset += nameLength * 2 + 16;
    }

    for (unsigned int i = 0; i < itemCount; i++)
    {
        if (offset > size || size - offset < 4)
        {
            return false;
        }

        unsigned int itemParameterCount = ReadDLCWord(pData, offset, bigEndian, convert);
        offset += 4;

        for (unsigned int j = 0; j < itemParameterCount; j++)
        {
            if (offset > size || size - offset < 8)
            {
                return false;
            }

            ReadDLCWord(pData, offset, bigEndian, convert);
            unsigned int valueLength = ReadDLCWord(pData, offset + 4, bigEndian, convert);
            if (!ReadDLCText(pData, size, offset + 8, valueLength, bigEndian, convert, NULL))
            {
                return false;
            }

            offset += valueLength * 2 + 12;
        }

        if (offset > size || size - offset < itemSizes[i])
        {
            return false;
        }

        if (nestedPacks
            && (itemTypes[i] == DLCManager::eDLCType_TexturePack || itemTypes[i] == DLCManager::eDLCType_SkinPack
                || itemTypes[i] == DLCManager::eDLCType_Audio))
        {
            const bool nested = itemTypes[i] != DLCManager::eDLCType_Audio;
            bool nestedBigEndian = bigEndian;
            if (!WalkDLCData(pData + offset, itemSizes[i], nested, nestedBigEndian, false))
            {
                nestedBigEndian = !bigEndian;
                if (!WalkDLCData(pData + offset, itemSizes[i], nested, nestedBigEndian, false))
                {
                    return false;
                }
            }

            if (toHost && !WalkDLCData(pData + offset, itemSizes[i], nested, nestedBigEndian, true))
            {
                return false;
            }
        }

        offset += itemSizes[i];
    }

    return true;
}

static bool ConvertDLCDataToHost(unsigned char* pData, unsigned int size)
{
    if (size < 8)
    {
        return false;
    }

    for (int pass = 0; pass < 2; pass++)
    {
        const bool bigEndian = pass == 0 ? DLCHostIsBigEndian() : !DLCHostIsBigEndian();
        const unsigned int version = ReadDLCWord(pData, 0, bigEndian, false);
        if (version >= 3 && version <= 0xFF && WalkDLCData(pData, size, true, bigEndian, false))
        {
            return WalkDLCData(pData, size, true, bigEndian, true);
        }
    }

    return false;
}

DLCManager::DLCManager()
{
    m_bCorruptDLCFound = true;
}

DLCManager::~DLCManager()
{
    for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
    {
        delete *it;
    }
}

DLCManager::EDLCParameterType DLCManager::getParameterType(const std::wstring& name)
{
    EDLCParameterType result = eDLCParameterType_Invalid;

    for (int i = 0; i < eDLCParameterType_NumTypes; i++)
    {
        const wchar_t* parameterName = s_parameterNames[i];

        if (name == parameterName)
        {
            result = (EDLCParameterType)i;
            break;
        }
    }

    return result;
}

int DLCManager::getPackCount(EDLCType type)
{
    if (type != eDLCType_All)
    {
        int count = 0;

        for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
        {
            if ((*it)->getDLCItemsCount(type) > 0)
            {
                count++;
            }
        }

        return count;
    }

    return m_packs.size();
}

void DLCManager::addPack(DLCPack* pPack)
{
    m_packs.push_back(pPack);
}

void DLCManager::removePack(DLCPack* pPack)
{
    if (pPack != NULL)
    {
        std::vector<DLCPack*>::iterator it = std::find(m_packs.begin(), m_packs.end(), pPack);

        if (it != m_packs.end())
        {
            m_packs.erase(it);
        }

        delete pPack;
    }
}

void DLCManager::LanguageChanged()
{
    for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
    {
        DLCPack* pPack = *it;
        pPack->UpdateLanguage();
    }
}

DLCPack* DLCManager::getPack(const std::wstring& name)
{
    DLCPack* pFound = NULL;

    for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
    {
        DLCPack* pPack = *it;
        const std::wstring& packName = pPack->getName();

        if (packName == name)
        {
            pFound = pPack;
            break;
        }
    }

    return pFound;
}

DLCPack* DLCManager::getPackFromPackID(int packID)
{
    std::vector<DLCPack*>::iterator it;
    std::vector<DLCPack*>::iterator end;
    for (it = m_packs.begin(), end = m_packs.end(); it != end; ++it)
    {
        DLCPack* pPack = *it;

        if ((int)pPack->GetPackId() == packID)
        {
            return pPack;
        }
    }

    return NULL;
}

DLCPack* DLCManager::getPack(unsigned int index, EDLCType type)
{
    if (type != eDLCType_All)
    {
        unsigned int matchIndex = 0;

        for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
        {
            DLCPack* pPack = *it;

            if (pPack->getDLCItemsCount(type) > 0)
            {
                if (matchIndex == index)
                {
                    return pPack;
                }

                matchIndex++;
            }
        }
    }
    else if (index < m_packs.size())
    {
        return m_packs[index];
    }

    return NULL;
}

DLCFile* DLCManager::getSkinFile(const std::wstring& fileName)
{
    DLCFile* pFile = NULL;

    for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
    {
        DLCPack* pPack = *it;
        pFile = pPack->getSkinFile(fileName);

        if (pFile != NULL)
        {
            break;
        }
    }

    return pFile;
}

DLCPack* DLCManager::getPackContainingSkin(const std::wstring& fileName)
{
    DLCPack* pFound = NULL;

    for (std::vector<DLCPack*>::iterator it = m_packs.begin(); it != m_packs.end(); ++it)
    {
        DLCPack* pPack = *it;

        if (pPack->getDLCItemsCount(eDLCType_Skin) > 0 && pPack->doesPackContainSkin(fileName))
        {
            pFound = pPack;
            break;
        }
    }

    return pFound;
}

DLCFile* DLCManager::getSkinFile(int skinIndex, bool& bIsDefaultSkin)
{
    DLCFile* pFile = NULL;

    bIsDefaultSkin = false;

    if (((skinIndex >> 31) & 1) != 0)
    {
        wchar_t fileName[256];

        swprintf(fileName, 256, L"dlcskin%08d.png", skinIndex & 0x7FFFFFFF);

        DLCPack* pPack = getPackContainingSkin(fileName);

        if (pPack != NULL)
        {
            pFile = pPack->getSkinFile(fileName);
        }
    }
    else if ((skinIndex & 0x7FFFFFE0) == 0)
    {
        bIsDefaultSkin = true;
    }

    return pFile;
}

int DLCManager::checkForCorruptDLCAndAlert(bool bShowMessage)
{
    int numCorruptPacks = m_numCorruptPacks;
    DLCPack* pFirstCorruptPack = NULL;

    std::vector<DLCPack*>::iterator it;
    std::vector<DLCPack*>::iterator end;

    for (it = m_packs.begin(), end = m_packs.end(); it != end; ++it)
    {
        DLCPack* pPack = *it;

        if (pPack->IsCorrupt())
        {
            numCorruptPacks++;

            if (pFirstCorruptPack == NULL)
            {
                pFirstCorruptPack = pPack;
            }
        }
    }

    if (numCorruptPacks != 0 && bShowMessage)
    {
        int messageParameters[1];

        messageParameters[0] = IDS_CONFIRM_OK;

        if (numCorruptPacks == 1 && pFirstCorruptPack != NULL)
        {
            wchar_t messageText[132];

            swprintf(messageText, 132, L"%ls\n\n%%ls", pFirstCorruptPack->getName().c_str());

            gConsoleUIController.RequestErrorMessage(IDS_CORRUPT_DLC_TITLE, IDS_CORRUPT_DLC, messageParameters, 1,
                                                     gProfile->GetPrimaryPad(), NULL, NULL, messageText, true);
        }
        else
        {
            gConsoleUIController.RequestErrorMessage(IDS_CORRUPT_DLC_TITLE, 0x75E79C02, messageParameters, 1,
                                                     gProfile->GetPrimaryPad(), NULL, NULL, NULL, true);
        }
    }

    m_bCorruptDLCFound = false;

    return numCorruptPacks;
}

bool DLCManager::processDLCDataFile(unsigned int& numItemsAdded, unsigned char* pData, unsigned int size,
                                    DLCPack* pPack, bool bTakeDataOwnership)
{
    boost::unordered_map<int, EDLCParameterType> parameterTypes;
    bool bHasXMLVersion = false;
    unsigned int xmlVersion = 0;

    if (*(unsigned int*)pData < 3)
    {
        if (bTakeDataOwnership)
            delete[] pData;
        return false;
    }

    pPack->SetDataPointer(pData, bTakeDataOwnership);

    unsigned int parameterCount = ((unsigned int*)pData)[1];
    unsigned int offset = 8;
    unsigned int* pParameter = (unsigned int*)(pData + offset);

    for (unsigned int i = 0; i < parameterCount; i++)
    {
        std::wstring parameterName = char16towstring((const char16_t*)(pParameter + 2));
        EDLCParameterType parameterType = getParameterType(parameterName);

        if (parameterType != eDLCParameterType_Invalid)
        {
            parameterTypes[(int)pParameter[0]] = parameterType;
        }

        offset += pParameter[1] * 2 + 12;
        pParameter = (unsigned int*)(pData + offset);

        if (parameterType == eDLCParameterType_XMLVersion)
        {
            bHasXMLVersion = true;
        }
    }

    if (bHasXMLVersion)
    {
        xmlVersion = *(unsigned int*)(pData + offset);
        offset += 4;
    }

    unsigned int itemCount = *(unsigned int*)(pData + offset);
    offset += 4;

    unsigned int dataOffset = offset;
    unsigned int* pItemData = (unsigned int*)(pData + dataOffset);

    for (unsigned int i = 0; i < itemCount; i++)
    {
        dataOffset += pItemData[2] * 2 + 16;
        pItemData = (unsigned int*)(pData + dataOffset);
    }

    while (itemCount != 0)
    {
        unsigned int* pItemHeader = (unsigned int*)(pData + offset);
        EDLCType itemType = (EDLCType)pItemHeader[1];

        DLCFile* pFile = NULL;
        DLCPack* pTexturePack = NULL;
        DLCPack* pChildPack = NULL;

        if (itemType == eDLCType_TexturePack)
        {
            pTexturePack = new DLCPack(pPack->getName());
        }
        else if (itemType == eDLCType_SkinPack)
        {
            pChildPack = new DLCPack(pPack->getName());
        }
        else if (itemType != eDLCType_Pack)
        {
            pFile = pPack->addFile(itemType, char16towstring((const char16_t*)(pItemHeader + 3)));

            if (itemType == eDLCType_Skin && bHasXMLVersion)
            {
                wchar_t versionText[64];

                swprintf(versionText, 64, L"%i", xmlVersion);

                pFile->addParameter(eDLCParameterType_XMLVersion, versionText);
            }
        }

        unsigned int itemParameterCount = pItemData[0];
        pItemData++;

        for (; itemParameterCount != 0; itemParameterCount--)
        {
            boost::unordered_map<int, EDLCParameterType>::iterator it = parameterTypes.find((int)pItemData[0]);

            if (it != parameterTypes.end())
            {
                if (itemType == eDLCType_Pack)
                {
                    pPack->addParameter(it->second, char16towstring((const char16_t*)(pItemData + 2)));
                    numItemsAdded++;
                }
                else if (pFile != NULL)
                {
                    pFile->addParameter(it->second, char16towstring((const char16_t*)(pItemData + 2)));
                }
                else if (pTexturePack != NULL)
                {
                    pTexturePack->addParameter(it->second, char16towstring((const char16_t*)(pItemData + 2)));
                }
                else if (pChildPack != NULL)
                {
                    pChildPack->addParameter(it->second, char16towstring((const char16_t*)(pItemData + 2)));
                }
            }

            pItemData = (unsigned int*)((unsigned char*)pItemData + pItemData[1] * 2 + 12);
        }

        if (pTexturePack != NULL)
        {
            unsigned int numChildItemsAdded = 0;
            bool bLoaded = processDLCDataFile(numChildItemsAdded, (unsigned char*)pItemData, pItemHeader[0],
                                              pTexturePack, false);

            pPack->SetDataPointer(NULL, false);

            if (!bLoaded || numChildItemsAdded == 0)
            {
                delete pTexturePack;
                numItemsAdded++;
            }
            else
            {
                pPack->addChildPack(pTexturePack);

                if (pTexturePack->getDLCItemsCount(eDLCType_Texture) > 0)
                {
                    unsigned int packID = pTexturePack->GetPackId();

                    Minecraft::GetInstance()->mTexturePackRepository->addTexturePackFromDLC(pTexturePack, packID);
                }

                numItemsAdded++;
            }
        }
        else if (pChildPack != NULL)
        {
            unsigned int numChildItemsAdded = 0;
            bool bLoaded = processDLCDataFile(numChildItemsAdded, (unsigned char*)pItemData, pItemHeader[0], pChildPack,
                                              false);

            pPack->SetDataPointer(NULL, false);

            if (!bLoaded || numChildItemsAdded == 0)
            {
                delete pChildPack;
                numItemsAdded++;
            }
            else
            {
                pPack->addChildPack(pChildPack);
                numItemsAdded++;
            }
        }
        else if (pFile != NULL)
        {
            pFile->addData((unsigned char*)pItemData, pItemHeader[0]);

            if (pItemHeader[1] == eDLCType_Skin)
            {
                gApp.m_dlcSkinNames.push_back(char16towstring((const char16_t*)(pItemHeader + 3)));
            }

            numItemsAdded++;
        }

        itemCount--;
        offset += pItemHeader[2] * 2 + 16;
        pItemData = (unsigned int*)((unsigned char*)pItemData + pItemHeader[0]);
    }

    if (pPack->getDLCItemsCount(eDLCType_GameRules) > 0 || pPack->getDLCItemsCount(eDLCType_GameRulesHeader) > 0)
    {
        gApp.mGameRuleManager.loadGameRules(pPack);
    }

    pPack->getDLCItemsCount(eDLCType_Audio);

    return true;
}

bool DLCManager::readDLCDataFile(unsigned int& numItemsAdded, const std::string& fileName, DLCPack* pPack,
                                 unsigned int packID, bool bTakeDataOwnership)
{
    std::wstring wideFileName = convStringToWstring(fileName);
    int file = CreateFileA(fileName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == -1)
        return false;

    const unsigned int fileSize = GetFileSize(file, NULL);
    unsigned char* pData = new unsigned char[fileSize];
    unsigned int bytesRead;
    if (ReadFile(file, pData, fileSize, &bytesRead, NULL) != 0)
    {
        CloseHandle(file);
    }
    else
    {
        CloseHandle(file);
        delete[] pData;
        pPack->SetIsCorrupt(true);
        m_bCorruptDLCFound = true;

        return false;
    }

    if (!ConvertDLCDataToHost(pData, bytesRead))
    {
        delete[] pData;
        pPack->SetIsCorrupt(true);
        m_bCorruptDLCFound = true;

        return false;
    }

    return processDLCDataFile(numItemsAdded, pData, bytesRead, pPack, true);
}

bool DLCManager::readDLCDataFile(unsigned int& numItemsAdded, const std::wstring& fileName, DLCPack* pPack,
                                 unsigned int packID, bool bTakeDataOwnership)
{
    std::string narrowFileName = wstringtofilename(fileName, s_dlcDataFileName);
    bool result = readDLCDataFile(numItemsAdded, narrowFileName, pPack, packID, bTakeDataOwnership);

    return result;
}

unsigned int DLCManager::retrievePackID(unsigned char* pData, unsigned int size)
{
    unsigned int packID = 0;
    std::wstring packName = L"";
    boost::unordered_map<int, EDLCParameterType> parameterTypes;
    bool bHasXMLVersion = false;

    if (*(unsigned int*)pData < 3)
    {
        return 0;
    }

    unsigned int parameterCount = ((unsigned int*)pData)[1];
    unsigned int offset = 8;
    unsigned int* pParameter = (unsigned int*)(pData + offset);

    for (unsigned int i = 0; i < parameterCount; i++)
    {
        std::wstring parameterName = char16towstring((const char16_t*)(pParameter + 2));
        EDLCParameterType parameterType = getParameterType(parameterName);

        if (parameterType != eDLCParameterType_Invalid)
        {
            parameterTypes[(int)pParameter[0]] = parameterType;
        }

        offset += pParameter[1] * 2 + 12;
        pParameter = (unsigned int*)(pData + offset);

        if (parameterType == eDLCParameterType_XMLVersion)
        {
            bHasXMLVersion = true;
        }
    }

    if (bHasXMLVersion)
    {
        offset += 4;
    }

    unsigned int itemCount = *(unsigned int*)(pData + offset);
    offset += 4;

    unsigned int dataOffset = offset;
    unsigned int* pItemData = (unsigned int*)(pData + dataOffset);

    for (unsigned int i = 0; i < itemCount; i++)
    {
        dataOffset += pItemData[2] * 2 + 16;
        pItemData = (unsigned int*)(pData + dataOffset);
    }

    while (itemCount != 0)
    {
        unsigned int* pItemHeader = (unsigned int*)(pData + offset);
        unsigned int itemParameterCount = pItemData[0];
        EDLCType itemType = (EDLCType)pItemHeader[1];
        pItemData++;

        for (; itemParameterCount != 0; itemParameterCount--)
        {
            boost::unordered_map<int, EDLCParameterType>::iterator it = parameterTypes.find((int)pItemData[0]);

            if (it != parameterTypes.end())
            {
                if (itemType == eDLCType_Pack)
                {
                    if (it->second == eDLCParameterType_PackID)
                    {
                        {
                            std::wstring packIDText = char16towstring((const char16_t*)(pItemData + 2));
                            std::wstringstream stream;

                            stream << std::dec << packIDText.c_str();
                            stream >> packID;
                        }

                        parameterTypes.clear();

                        return packID;
                    }
                }
                else if (itemType == eDLCType_Skin && packName.empty() && it->second == eDLCParameterType_DisplayName)
                {
                    packName = char16towstring((const char16_t*)(pItemData + 2));
                }
            }

            pItemData = (unsigned int*)((unsigned char*)pItemData + pItemData[1] * 2 + 12);
        }

        itemCount--;
        offset += pItemHeader[2] * 2 + 16;
        pItemData = (unsigned int*)((unsigned char*)pItemData + pItemHeader[0]);
    }

    parameterTypes.clear();

    return packID;
}

unsigned int DLCManager::retrievePackIDFromDLCDataFile(const std::string& fileName)
{
    std::wstring wideFileName = convStringToWstring(fileName);
    int file = CreateFileA(fileName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == -1)
    {
        return 0;
    }

    const unsigned int fileSize = GetFileSize(file, NULL);
    unsigned char* pData = new unsigned char[fileSize];
    unsigned int bytesRead;
    if (ReadFile(file, pData, fileSize, &bytesRead, NULL) != 0)
    {
        CloseHandle(file);
    }
    else
    {
        delete[] pData;

        return 0;
    }

    if (!ConvertDLCDataToHost(pData, bytesRead))
    {
        delete[] pData;

        return 0;
    }

    unsigned int packID = retrievePackID(pData, bytesRead);

    delete[] pData;

    return packID;
}
