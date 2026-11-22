#include "game/core/LceLeaderboardManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <boost/shared_ptr.hpp>

#include "game/core/ByteArrayInputStream.hpp"
#include "game/core/ByteArrayOutputStream.hpp"
#include "game/core/DataInputStream.hpp"
#include "game/core/DataOutputStream.hpp"
#include "game/core/LeaderboardBoards.hpp"
#include "game/core/Minecraft.hpp"
#include "game/core/System.hpp"
#include "game/network/ClientPacketListener.hpp"
#include "game/network/LeaderboardReadListener.hpp"
#include "game/network/ServerboundCustomPayloadPacket.hpp"
#include "platform/CProfile.hpp"
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
#include "platform/pc/PcLiveBridge.hpp"
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
#include "platform/pc/PcJson.hpp"
#include "platform/wiiu/WiiULiveSignIn.hpp"
#endif

LeaderboardManager* g_pLeaderboardManager = new LceLeaderboardManager();

namespace
{
const long long kServerTimeoutMs = 5000;
const long long kLiveTimeoutMs = 15000;

#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
int LiveBoardIndex(LeaderboardManager::EStatsType type)
{
    const int board = static_cast<int>(type);
    return board >= 0 && board < 4 ? board : -1;
}

bool WriteLiveFile(const wchar_t* name, const std::string& contents)
{
    if (name == NULL || !PcLiveBridge::Enabled())
        return false;
    std::wstring temporary(name);
    temporary += L".tmp";
    FILE* file = PcLiveBridge::Open(temporary.c_str(), L"wb");
    if (file == NULL)
        return false;
    const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed)
    {
        PcLiveBridge::Remove(temporary.c_str());
        return false;
    }
    PcLiveBridge::Remove(name);
    if (!PcLiveBridge::Rename(temporary.c_str(), name))
    {
        PcLiveBridge::Remove(temporary.c_str());
        return false;
    }
    return true;
}

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    for (;;)
    {
        const std::string::size_type tab = line.find('\t', start);
        parts.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
        if (tab == std::string::npos)
            break;
        start = tab + 1;
    }
    return parts;
}

std::wstring AsciiWide(const std::string& value)
{
    return std::wstring(value.begin(), value.end());
}
#endif
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
const char* LiveBoardName(int board)
{
    static const char* const names[] = {"traveling", "mining", "farming", "kills"};
    return board >= 0 && board < 4 ? names[board] : NULL;
}

const JsonValue& LiveField(const JsonValue& value, const wchar_t* name)
{
    static const JsonValue empty;
    const auto found = value.members.find(name);
    return found == value.members.end() ? empty : found->second;
}

unsigned int LiveNumber(const JsonValue& value)
{
    if (value.type != JsonValue::Number || value.text.empty()
        || value.text.find_first_not_of(L"0123456789") != std::wstring::npos)
        return 0;
    const std::string ascii(value.text.begin(), value.text.end());
    const unsigned long long number = std::strtoull(ascii.c_str(), NULL, 10);
    return number <= 0xffffffffULL ? static_cast<unsigned int>(number) : 0;
}
#endif
}

LceLeaderboardManager::LceLeaderboardManager()
    : m_pending(ePending_None),
      m_result(eStatsReturn_Failed),
      m_listener(NULL),
      m_board(0),
      m_pad(0),
      m_totalRows(0),
      m_fallbackToLocal(false),
      m_sentAt(0),
      m_requestId(0)
{
}

LceLeaderboardManager::~LceLeaderboardManager()
{
}

void LceLeaderboardManager::Tick()
{
    if (m_pending == ePending_Live)
    {
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
        if (ReadLiveResponse())
            return;
#endif
        if (System::processTimeInMilliSecs() - m_sentAt > kLiveTimeoutMs)
            Finish(m_fallbackToLocal && AddLocalScore() ? eStatsReturn_Success : eStatsReturn_Failed);
        return;
    }
    if (m_pending == ePending_Server && System::processTimeInMilliSecs() - m_sentAt > kServerTimeoutMs)
        Finish(eStatsReturn_Failed);
    if (m_pending != ePending_Ready)
        return;
    m_pending = ePending_None;
    LeaderboardReadListener* listener = m_listener;
    m_listener = NULL;
    if (listener == NULL)
        return;
    ReadView view;
    view.m_uiNumScores = static_cast<unsigned int>(m_rows.size());
    view.m_pScores = m_rows.empty() ? NULL : &m_rows[0];
    listener->OnStatsReadComplete(m_result, static_cast<int>(m_totalRows), view);
}

bool LceLeaderboardManager::OpenSession()
{
    return true;
}

void LceLeaderboardManager::CloseSession()
{
}

void LceLeaderboardManager::DeleteSession()
{
}

bool LceLeaderboardManager::WriteStats(unsigned int count, RegisterScore* scores)
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    if (!PcLiveBridge::Enabled() || scores == NULL || count == 0)
        return true;
    std::ostringstream contents;
    bool any = false;
    for (unsigned int i = 0; i < count; ++i)
    {
        const int board = LiveBoardIndex(scores[i].m_eStatsType);
        if (board < 0 || scores[i].m_usNumStats == 0 || scores[i].m_usNumStats > MAX_STATS_PER_SCORE)
            continue;
        contents << board;
        for (unsigned int column = 0; column < scores[i].m_usNumStats; ++column)
            contents << '\t' << scores[i].m_auiStats[column];
        contents << '\n';
        any = true;
    }
    if (any)
    {
        static unsigned int submitId = 0;
        const std::wstring name = L"leaderboard-submit-" +
            std::to_wstring(static_cast<unsigned long long>(System::processTimeInMilliSecs())) + L"-" +
            std::to_wstring(++submitId) + L".tsv";
        WriteLiveFile(name.c_str(), contents.str());
    }
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    if (scores != NULL && WiiULiveSignIn::IsSignedIn())
    {
        for (unsigned int index = 0; index < count; ++index)
        {
            const RegisterScore& score = scores[index];
            const char* board = LiveBoardName(score.m_eStatsType);
            if (!board || score.m_usNumStats != LeaderboardBoards::ColumnCount(score.m_eStatsType)
                || score.m_usNumStats > MAX_STATS_PER_SCORE)
                continue;
            std::string body = "{\"values\":[";
            for (unsigned int column = 0; column < score.m_usNumStats; ++column)
            {
                if (column) body += ',';
                body += std::to_string(score.m_auiStats[column]);
            }
            body += "]}";
            const std::string path = std::string("/leaderboards/") + board;
            WiiULiveSignIn::RequestApi(path.c_str(), body.c_str(), [](long, const std::string&) {});
        }
    }
#else
    (void)count;
    (void)scores;
#endif
    return true;
}

void LceLeaderboardManager::FlushStats()
{
}

void LceLeaderboardManager::CancelOperation()
{
    m_pending = ePending_None;
    m_listener = NULL;
}

bool LceLeaderboardManager::isIdle()
{
    return m_pending == ePending_None;
}

void LceLeaderboardManager::Begin(LeaderboardReadListener* listener, int userIndex, EStatsType board)
{
    m_listener = listener;
    m_pad = userIndex >= 0 && userIndex < 4 ? userIndex : gProfile->GetPrimaryPad();
    m_board = static_cast<int>(board);
    m_rows.clear();
    m_totalRows = 0;
    m_fallbackToLocal = false;
}

bool LceLeaderboardManager::AddLocalScore()
{
    const int pad = m_pad;
    Minecraft* minecraft = Minecraft::GetInstance();
    StatsCounter* counter = minecraft != NULL && pad >= 0 && pad < 4 ? minecraft->mStats[pad] : NULL;
    const int columns = LeaderboardBoards::ColumnCount(m_board);
    if (counter == NULL || columns == 0)
        return false;

    ReadScore score;
    std::memset(score.m_auiStats, 0, sizeof(score.m_auiStats));
    gProfile->GetXUID(pad, &score.m_uid, false);
    score.m_uiRank = 1;
    score.m_wsGamertag = gProfile->GetDisplayName(pad);
    score.m_uiRating = 0;
    score.m_usNumStats = static_cast<unsigned short>(columns);
    score.m_iMessageStringId = 0;
    for (int column = 0; column < columns; ++column)
        score.m_auiStats[column] = LeaderboardBoards::CounterValue(counter, m_board, column);
    m_rows.push_back(score);
    m_totalRows = 1;
    return true;
}

void LceLeaderboardManager::Finish(eStatsReturn result)
{
    m_result = result == eStatsReturn_Success && m_rows.empty() ? eStatsReturn_NoScores : result;
    m_pending = ePending_Ready;
}

bool LceLeaderboardManager::ReadStats_Friends(LeaderboardReadListener* pReadListener, int iUserIndex,
                                              EStatsType eStatsType, PlayerUID, unsigned int uiStartRank,
                                              unsigned int uiNumRows)
{
    Begin(pReadListener, iUserIndex, eStatsType);
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    if (PcLiveBridge::Enabled())
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    if (m_pad == 0 && WiiULiveSignIn::IsSignedIn())
#endif
#if LCE_PLATFORM_PC || LCE_PLATFORM_WIIU
    {
        m_fallbackToLocal = true;
        RequestFromLive("friends", uiStartRank, uiNumRows);
        return true;
    }
#endif
    if (!AddLocalScore())
    {
        Finish(eStatsReturn_Failed);
        return true;
    }
    Finish(eStatsReturn_Success);
    return true;
}

bool LceLeaderboardManager::ReadStats_MyScore(LeaderboardReadListener* pReadListener, int iUserIndex,
                                              EStatsType eStatsType, PlayerUID, unsigned int uiNumRows)
{
    Begin(pReadListener, iUserIndex, eStatsType);
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    if (PcLiveBridge::Enabled())
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    if (m_pad == 0 && WiiULiveSignIn::IsSignedIn())
#endif
#if LCE_PLATFORM_PC || LCE_PLATFORM_WIIU
    {
        m_fallbackToLocal = true;
        RequestFromLive("mine", 1, uiNumRows);
        return true;
    }
#endif
    RequestFromServer(true, 1, uiNumRows);
    return true;
}

bool LceLeaderboardManager::ReadStats_TopRank(LeaderboardReadListener* pReadListener, int iUserIndex,
                                              EStatsType eStatsType, unsigned int uiStartRank, unsigned int uiNumRows)
{
    Begin(pReadListener, iUserIndex, eStatsType);
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    if (PcLiveBridge::Enabled())
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    if (m_pad == 0 && WiiULiveSignIn::IsSignedIn())
#endif
#if LCE_PLATFORM_PC || LCE_PLATFORM_WIIU
    {
        RequestFromLive("overall", uiStartRank, uiNumRows);
        return true;
    }
#endif
    RequestFromServer(false, uiStartRank, uiNumRows);
    return true;
}

void LceLeaderboardManager::RequestFromLive(const char* filter, unsigned int startRank, unsigned int count)
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    const int board = LiveBoardIndex(static_cast<EStatsType>(m_board));
    if (board < 0 || filter == NULL || count == 0 || count > 50)
    {
        Finish(eStatsReturn_Failed);
        return;
    }
    ++m_requestId;
    if (m_requestId == 0)
        ++m_requestId;
    std::ostringstream request;
    request << m_requestId << '\t' << board << '\t' << filter << '\t' << startRank << '\t' << count << '\n';
    PcLiveBridge::Remove(L"leaderboard.response");
    if (!WriteLiveFile(L"leaderboard.request", request.str()))
    {
        Finish(m_fallbackToLocal && AddLocalScore() ? eStatsReturn_Success : eStatsReturn_Failed);
        return;
    }
    m_sentAt = System::processTimeInMilliSecs();
    m_pending = ePending_Live;
#elif LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    const char* board = LiveBoardName(m_board);
    if (!board || !filter || count == 0 || count > 50)
    {
        Finish(eStatsReturn_Failed);
        return;
    }
    const unsigned int id = ++m_requestId;
    const std::string path = std::string("/leaderboards?board=") + board + "&filter=" + filter
        + "&start=" + std::to_string(startRank ? startRank : 1) + "&count=" + std::to_string(count);
    if (!WiiULiveSignIn::RequestApi(path.c_str(), NULL, [this, id](long status, const std::string& body) {
        if (m_pending != ePending_Live || m_requestId != id)
            return;
        JsonValue root;
        if (status != 200 || !PcJsonParse(body, root) || LiveField(root, L"rows").type != JsonValue::Array)
        {
            Finish(m_fallbackToLocal && AddLocalScore() ? eStatsReturn_Success : eStatsReturn_Failed);
            return;
        }
        const int columns = LeaderboardBoards::ColumnCount(m_board);
        for (const JsonValue& row : LiveField(root, L"rows").items)
        {
            const JsonValue& values = LiveField(row, L"values");
            const unsigned int rank = LiveNumber(LiveField(row, L"rank"));
            if (rank == 0 || values.type != JsonValue::Array || values.items.size() != static_cast<size_t>(columns)
                || columns <= 0 || columns > MAX_STATS_PER_SCORE || m_rows.size() >= 50)
                continue;
            ReadScore score = {};
            score.m_uiRank = rank;
            score.m_wsGamertag = LiveField(row, L"username").text;
            score.m_usNumStats = static_cast<unsigned short>(columns);
            for (int column = 0; column < columns; ++column)
                score.m_auiStats[column] = LiveNumber(values.items[column]);
            m_rows.push_back(score);
        }
        m_totalRows = LiveNumber(LiveField(root, L"total"));
        if (m_rows.empty() && m_fallbackToLocal && AddLocalScore())
            m_totalRows = 1;
        Finish(eStatsReturn_Success);
    }))
    {
        Finish(m_fallbackToLocal && AddLocalScore() ? eStatsReturn_Success : eStatsReturn_Failed);
        return;
    }
    m_pending = ePending_Live;
    m_sentAt = System::processTimeInMilliSecs();
#else
    (void)filter;
    (void)startRank;
    (void)count;
    Finish(eStatsReturn_Failed);
#endif
}

bool LceLeaderboardManager::ReadLiveResponse()
{
#if LCE_PLATFORM_PC && !LCE_PLATFORM_SWITCH
    FILE* file = PcLiveBridge::Open(L"leaderboard.response", L"rb");
    if (file == NULL)
        return false;
    std::string contents;
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), file) != NULL)
        contents += buffer;
    std::fclose(file);
    PcLiveBridge::Remove(L"leaderboard.response");
    std::istringstream lines(contents);
    std::string line;
    if (!std::getline(lines, line))
        return false;
    const std::vector<std::string> header = SplitTabs(line);
    if (header.size() != 3)
        return false;
    const unsigned int id = static_cast<unsigned int>(std::strtoul(header[0].c_str(), NULL, 10));
    if (id != m_requestId)
        return false;
    if (header[1] == "-1")
    {
        Finish(m_fallbackToLocal && AddLocalScore() ? eStatsReturn_Success : eStatsReturn_Failed);
        return true;
    }
    const unsigned int total = static_cast<unsigned int>(std::strtoul(header[1].c_str(), NULL, 10));
    const unsigned int count = static_cast<unsigned int>(std::strtoul(header[2].c_str(), NULL, 10));
    if (count > 50)
    {
        Finish(eStatsReturn_Failed);
        return true;
    }
    m_rows.clear();
    for (unsigned int i = 0; i < count && std::getline(lines, line); ++i)
    {
        const std::vector<std::string> fields = SplitTabs(line);
        if (fields.size() < 4 || fields.size() > MAX_STATS_PER_SCORE + 3)
            continue;
        ReadScore score;
        std::memset(score.m_auiStats, 0, sizeof(score.m_auiStats));
        score.m_uiRank = static_cast<unsigned int>(std::strtoul(fields[0].c_str(), NULL, 10));
        score.m_wsGamertag = AsciiWide(fields[1]);
        score.m_uid = PlayerUID(0);
        score.m_usNumStats = static_cast<unsigned short>(fields.size() - 3);
        for (unsigned int column = 0; column < score.m_usNumStats; ++column)
            score.m_auiStats[column] = static_cast<unsigned int>(std::strtoul(fields[column + 3].c_str(), NULL, 10));
        score.m_uiRating = 0;
        score.m_iMessageStringId = 0;
        m_rows.push_back(score);
    }
    m_totalRows = total;
    if (m_rows.empty() && m_fallbackToLocal && AddLocalScore())
        m_totalRows = 1;
    Finish(eStatsReturn_Success);
    return true;
#else
    return false;
#endif
}

void LceLeaderboardManager::RequestFromServer(bool aroundMe, unsigned int startRank, unsigned int count)
{
    Minecraft* minecraft = Minecraft::GetInstance();
    const int pad = m_pad;
    ClientPacketListener* connection = minecraft != NULL && pad >= 0 && pad < 4 ? minecraft->getConnection(pad) : NULL;
    if (connection == NULL)
    {
        Finish(eStatsReturn_Failed);
        return;
    }
    ByteArrayOutputStream bytes;
    DataOutputStream output(&bytes);
    output.writeByte(static_cast<unsigned char>(m_board));
    output.writeByte(aroundMe ? 1 : 0);
    output.writeInt(static_cast<int>(startRank));
    output.writeInt(static_cast<int>(count));
    connection->send(boost::shared_ptr<Packet>(
        new ServerboundCustomPayloadPacket(ServerboundCustomPayloadPacket::LEADERBOARD_PACKET, bytes.toByteArray())));
    m_sentAt = System::processTimeInMilliSecs();
    m_pending = ePending_Server;
}

void LceLeaderboardManager::OnServerRows(arrayWithLength<unsigned char> data)
{
    ByteArrayInputStream bytes(data);
    DataInputStream input(&bytes);
    const int board = input.readByte();
    const int total = input.readInt();
    const int rows = input.readShort();
    if (m_pending != ePending_Server || board != m_board)
    {
        bytes.reset();
        input.close();
        return;
    }
    m_rows.clear();
    for (int i = 0; i < rows; ++i)
    {
        ReadScore score;
        std::memset(score.m_auiStats, 0, sizeof(score.m_auiStats));
        score.m_uiRank = static_cast<unsigned int>(input.readInt());
        score.m_wsGamertag = input.readUTF();
        score.m_uid = PlayerUID(input.readUTF());
        const int columns = input.readByte();
        score.m_usNumStats = static_cast<unsigned short>(columns < MAX_STATS_PER_SCORE ? columns : MAX_STATS_PER_SCORE);
        for (int column = 0; column < columns; ++column)
        {
            const unsigned int value = static_cast<unsigned int>(input.readInt());
            if (column < MAX_STATS_PER_SCORE)
                score.m_auiStats[column] = value;
        }
        score.m_uiRating = 0;
        score.m_iMessageStringId = 0;
        m_rows.push_back(score);
    }
    bytes.reset();
    input.close();
    m_totalRows = total < 0 ? 0 : static_cast<unsigned int>(total);
    Finish(eStatsReturn_Success);
}
