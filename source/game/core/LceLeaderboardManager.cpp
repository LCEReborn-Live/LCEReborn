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

LeaderboardManager* g_pLeaderboardManager = new LceLeaderboardManager();

namespace
{
const long long kServerTimeoutMs = 5000;
const long long kLiveTimeoutMs = 15000;

#ifdef _WIN32
int LiveBoardIndex(LeaderboardManager::EStatsType type)
{
    const int board = static_cast<int>(type);
    return board >= 0 && board < 4 ? board : -1;
}

std::wstring LiveDirectory()
{
    const wchar_t* directory = _wgetenv(L"LCE_LIVE_DIR");
    return directory == NULL ? std::wstring() : std::wstring(directory);
}

std::wstring LivePath(const wchar_t* name)
{
    const std::wstring directory = LiveDirectory();
    return directory.empty() ? std::wstring() : directory + L"\\" + name;
}

bool WriteLiveFile(const std::wstring& path, const std::string& contents)
{
    if (path.empty())
        return false;
    const std::wstring temporary = path + L".tmp";
    FILE* file = NULL;
    if (_wfopen_s(&file, temporary.c_str(), L"wb") != 0 || file == NULL)
        return false;
    const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed)
    {
        _wremove(temporary.c_str());
        return false;
    }
    _wremove(path.c_str());
    if (_wrename(temporary.c_str(), path.c_str()) != 0)
    {
        _wremove(temporary.c_str());
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
}

LceLeaderboardManager::LceLeaderboardManager()
    : m_pending(ePending_None),
      m_result(eStatsReturn_Failed),
      m_listener(NULL),
      m_board(0),
      m_pad(0),
      m_totalRows(0),
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
#ifdef _WIN32
        if (ReadLiveResponse())
            return;
#endif
        if (System::processTimeInMilliSecs() - m_sentAt > kLiveTimeoutMs)
            Finish(eStatsReturn_Failed);
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
#ifdef _WIN32
    if (LiveDirectory().empty() || scores == NULL || count == 0)
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
        WriteLiveFile(LivePath(name.c_str()), contents.str());
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
#ifdef _WIN32
    if (!LiveDirectory().empty())
    {
        RequestFromLive("friends", uiStartRank, uiNumRows);
        return true;
    }
#endif
    const int pad = m_pad;
    Minecraft* minecraft = Minecraft::GetInstance();
    StatsCounter* counter = minecraft != NULL && pad >= 0 && pad < 4 ? minecraft->mStats[pad] : NULL;
    const int columns = LeaderboardBoards::ColumnCount(m_board);
    if (counter == NULL || columns == 0)
    {
        Finish(eStatsReturn_Failed);
        return true;
    }
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
    Finish(eStatsReturn_Success);
    return true;
}

bool LceLeaderboardManager::ReadStats_MyScore(LeaderboardReadListener* pReadListener, int iUserIndex,
                                              EStatsType eStatsType, PlayerUID, unsigned int uiNumRows)
{
    Begin(pReadListener, iUserIndex, eStatsType);
#ifdef _WIN32
    if (!LiveDirectory().empty())
    {
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
#ifdef _WIN32
    if (!LiveDirectory().empty())
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
#ifdef _WIN32
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
    _wremove(LivePath(L"leaderboard.response").c_str());
    if (!WriteLiveFile(LivePath(L"leaderboard.request"), request.str()))
    {
        Finish(eStatsReturn_Failed);
        return;
    }
    m_sentAt = System::processTimeInMilliSecs();
    m_pending = ePending_Live;
#else
    (void)filter;
    (void)startRank;
    (void)count;
    Finish(eStatsReturn_Failed);
#endif
}

bool LceLeaderboardManager::ReadLiveResponse()
{
#ifdef _WIN32
    const std::wstring responsePath = LivePath(L"leaderboard.response");
    FILE* file = NULL;
    if (responsePath.empty() || _wfopen_s(&file, responsePath.c_str(), L"rb") != 0 || file == NULL)
        return false;
    std::string contents;
    char buffer[1024];
    while (std::fgets(buffer, sizeof(buffer), file) != NULL)
        contents += buffer;
    std::fclose(file);
    _wremove(responsePath.c_str());
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
        Finish(eStatsReturn_Failed);
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
