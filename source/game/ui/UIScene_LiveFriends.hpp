#ifndef GAME_UI_UISCENE_LIVEFRIENDS_HPP
#define GAME_UI_UISCENE_LIVEFRIENDS_HPP

#include <string>
#include <vector>
#include <map>

#include "game/core/IUIScene_LoadCreateJoin.hpp"
#include "game/ui/UIScene.hpp"
#include "game/ui/UIControl.hpp"
#include "game/ui/UIControl_LeftIconButtonList.hpp"
#include "game/ui/UIControl_Touch.hpp"

struct FriendSessionInfo;

class UIScene_LiveFriends : public UIScene
{
public:
    UIScene_LiveFriends(int playerId, void* data, UILayer* layer);
    virtual ~UIScene_LiveFriends();

    virtual std::wstring getMoviePath();
    virtual unsigned int mapElementsAndNames();
    virtual int getSceneType();
    virtual void tick();
    virtual void customDraw(const char* name, fuiRect* rect);
    virtual void handleInput(int playerId, int action, bool repeat, bool pressed, bool released, bool& handled);
    virtual void handlePress(int controlId, int childId);
    virtual void handleFocusChange(int controlId, int childId);
    virtual void updateTooltips();
    virtual void updateComponents();
    virtual void handleReload();

private:
    enum ETab { eTab_Friends, eTab_Requests, eTab_Invites, eTab_Count };
    struct Entry
    {
        char kind;
        unsigned int id;
        std::string username;
        std::string presence;
        std::string activity;
        std::string world;
        bool online;
        bool joinable;
        std::string revision;
        Entry() : kind(0), id(0), online(false), joinable(false), revision("0") {}
    };
    struct FaceTexture
    {
        unsigned int userId;
        int texture;
        std::string revision;
        FaceTexture(unsigned int id, int handle, const std::string& version) : userId(id), texture(handle), revision(version) {}
    };

    bool ReadSocial();
    void PollLive();
    int GetAchievementTexture(int id);
    void PopulateLists();
    void SendAction(char action, unsigned int id);
    void RequestAddFriend();
    static int AddFriendKeyboardReturned(void* context, bool accepted);
    bool JoinEntry(const Entry& entry);
    void SelectTab(ETab tab);
    void SetTabTitles();
    void ApplyReferenceLayout();
    std::wstring RegisterFaceIcon(const Entry& entry);
    int GetFaceTexture(const Entry& entry);
    void ReleaseFaceTextures();
    std::wstring EntryLabel(const Entry& entry) const;
    void ActivateEntry(int selection);

    UIControl m_tabControls[eTab_Count];
    UIControl_LeftIconButtonList m_lists[eTab_Count];
    UIControl m_timers[eTab_Count];
    UIControl_Touch m_touches[eTab_Count];
    std::vector<Entry> m_entries[eTab_Count];
    class GuiComponent* m_gui;
    ETab m_tab;
    long long m_nextPoll = 0;
    long long m_actionStarted = 0;
    int m_achievementPage = 0;
    bool m_haveSnapshot = false;
    bool m_liveConnected = false;
    bool m_achievementsReady = false;
    unsigned int m_profileId = 0;
    unsigned int m_selectedIds[eTab_Count] = {};
    std::string m_snapshot;
    std::string m_pendingAction;
    char m_pendingOperation = 0;
    bool m_addFriendKeyboardPending = false;
    bool m_inviteMode = false;
    std::vector<unsigned int> m_inviteQueue;
    long long m_nextQueuedInviteAt = 0;
    unsigned int m_inFlightInviteId = 0;
    std::wstring m_message;
    std::wstring m_connectionText = L"Connecting to Live...";
    std::vector<int> m_unlocked;
    std::map<int, int> m_achievementTextures;
    std::vector<FaceTexture> m_faceTextures;
};

#endif
