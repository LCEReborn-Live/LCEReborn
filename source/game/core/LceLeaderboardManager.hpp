#ifndef GAME_CORE_LCELEADERBOARDMANAGER_HPP
#define GAME_CORE_LCELEADERBOARDMANAGER_HPP

#include <vector>
#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
#include <array>
#include <string>
#endif

#include "game/core/LeaderboardManager.hpp"
#include "game/world/biome/arrayWithLength.hpp"

class LceLeaderboardManager : public LeaderboardManager
{
public:
    LceLeaderboardManager();
    virtual ~LceLeaderboardManager();

    virtual void Tick();
    virtual bool OpenSession();
    virtual void CloseSession();
    virtual void DeleteSession();
    virtual bool WriteStats(unsigned int uiNumScores, RegisterScore* pScores);
    virtual bool ReadStats_Friends(LeaderboardReadListener* pReadListener, int iUserIndex, EStatsType eStatsType,
                                   PlayerUID uid, unsigned int uiStartRank, unsigned int uiNumRows);
    virtual bool ReadStats_MyScore(LeaderboardReadListener* pReadListener, int iUserIndex, EStatsType eStatsType,
                                   PlayerUID uid, unsigned int uiNumRows);
    virtual bool ReadStats_TopRank(LeaderboardReadListener* pReadListener, int iUserIndex, EStatsType eStatsType,
                                   unsigned int uiStartRank, unsigned int uiNumRows);
    virtual void FlushStats();
    virtual void CancelOperation();
    virtual bool isIdle();

    void OnServerRows(arrayWithLength<unsigned char> data);

private:
    enum EPending
    {
        ePending_None,
        ePending_Server,
        ePending_Live,
        ePending_Ready
    };

    void Begin(LeaderboardReadListener* listener, int userIndex, EStatsType board);
    void Finish(eStatsReturn result);
    bool AddLocalScore();
    void RequestFromServer(bool aroundMe, unsigned int startRank, unsigned int count);
    void RequestFromLive(const char* filter, unsigned int startRank, unsigned int count);
    bool ReadLiveResponse();

#if LCE_PLATFORM_WIIU || LCE_PLATFORM_SWITCH
    void TickLiveUploads();

    using LiveScoreValues = std::array<unsigned int, MAX_STATS_PER_SCORE>;
    std::array<LiveScoreValues, 4> m_liveScores{};
    std::string m_liveScoreAccount;
    unsigned int m_liveSeenBoards = 0;
    unsigned int m_liveDirtyBoards = 0;
    unsigned int m_liveUploadBatch = 0;
    long long m_liveUploadAfter = 0;
    bool m_liveUploadInFlight = false;
#endif

    EPending m_pending;
    eStatsReturn m_result;
    LeaderboardReadListener* m_listener;
    int m_board;
    int m_pad;
    unsigned int m_totalRows;
    bool m_fallbackToLocal;
    long long m_sentAt;
    unsigned int m_requestId;
    std::vector<ReadScore> m_rows;
};

#endif
