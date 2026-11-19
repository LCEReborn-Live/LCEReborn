#include "platform/pc/CProfile.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#if !defined(_WIN32)
#include <pwd.h>
#include <unistd.h>
#endif

#include "game/client/render/Textures.hpp"
#include "game/core/BufferedImage.hpp"
#include "game/core/CAchievements.hpp"
#include "game/core/ConsoleUIController.hpp"
#include "game/core/Minecraft.hpp"
#include "platform/C4JStorage.hpp"
#include "platform/CInput.hpp"
#include "platform/pc/C4JThreadImpl.hpp"
#include "platform/pc/Log.hpp"
#include "platform/pc/PcAvatar.hpp"
#include "platform/pc/PcDiscord.hpp"
#include "platform/pc/PcPlayerProfiles.hpp"
#include "platform/pc/PcProfilePicker.hpp"
#include "platform/pc/PcLaunchOptions.hpp"
#include "platform/pc/PcPaths.hpp"

PlayerUID getPlayerUID(int, bool);
int getSlotId(int);

extern C4JStorage* gStorage;

C_4JProfile* gProfile;
CProfile* gProfileInstance = NULL;

PlayerUID getPlayerUID(int quadrant, bool includeOffline)
{
    PlayerUID uid(0);
    if (gProfileInstance->IsValidPlayer(quadrant))
        gProfileInstance->GetXUID(quadrant, &uid, includeOffline);
    return uid;
}

int getSlotId(int quadrant)
{
    PlayerUID uid = getPlayerUID(quadrant, false);
    if ((uid.flags & PlayerUID::FlagUuid) != 0)
        return gProfileInstance->GetSlotId(quadrant);
    return 0;
}

static void AppendUtf8(std::string& text, unsigned int codePoint)
{
    if (codePoint < 0x80)
    {
        text.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint < 0x800)
    {
        text.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
    else if (codePoint < 0x10000)
    {
        text.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
    else
    {
        text.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
}

static std::string ToUtf8(const std::wstring& text)
{
    std::string result;
    for (std::wstring::size_type index = 0; index < text.length(); ++index)
    {
        unsigned int codePoint = static_cast<unsigned int>(text[index]);
        if (sizeof(wchar_t) == 2 && codePoint >= 0xd800 && codePoint < 0xdc00 && index + 1 < text.length())
        {
            unsigned int low = static_cast<unsigned int>(text[index + 1]);
            if (low >= 0xdc00 && low < 0xe000)
            {
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            }
        }
        AppendUtf8(result, codePoint);
    }
    return result;
}

static std::wstring FromUtf8(const char* text)
{
    std::wstring result;
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
    while (*bytes != 0)
    {
        unsigned int codePoint = *bytes;
        int continuation = 0;
        if (codePoint >= 0xf0)
        {
            codePoint &= 0x07;
            continuation = 3;
        }
        else if (codePoint >= 0xe0)
        {
            codePoint &= 0x0f;
            continuation = 2;
        }
        else if (codePoint >= 0xc0)
        {
            codePoint &= 0x1f;
            continuation = 1;
        }
        else if (codePoint >= 0x80)
        {
            codePoint = 0xfffd;
        }
        ++bytes;
        while (continuation > 0 && (*bytes & 0xc0) == 0x80)
        {
            codePoint = (codePoint << 6) | (*bytes & 0x3f);
            ++bytes;
            --continuation;
        }
        if (continuation != 0)
            codePoint = 0xfffd;
        if (sizeof(wchar_t) == 2 && codePoint >= 0x10000)
        {
            result.push_back(static_cast<wchar_t>(0xd800 + ((codePoint - 0x10000) >> 10)));
            codePoint = 0xdc00 + ((codePoint - 0x10000) & 0x3ff);
        }
        result.push_back(static_cast<wchar_t>(codePoint));
    }
    return result;
}

static std::wstring EnvironmentString(const char* variable)
{
#if defined(_WIN32)
    std::wstring wideVariable(variable, variable + std::strlen(variable));
    const wchar_t* value = _wgetenv(wideVariable.c_str());
    return value != NULL ? std::wstring(value) : std::wstring();
#else
    const char* value = std::getenv(variable);
    return value != NULL ? FromUtf8(value) : std::wstring();
#endif
}

static std::wstring TrimName(const std::wstring& name)
{
    std::wstring::size_type first = 0;
    std::wstring::size_type last = name.length();
    while (first < last && std::iswspace(name[first]))
        ++first;
    while (last > first && std::iswspace(name[last - 1]))
        --last;
    return name.substr(first, last - first);
}

static std::wstring OperatingSystemUserName()
{
#if defined(_WIN32)
    return EnvironmentString("USERNAME");
#else
    std::wstring name = EnvironmentString("USER");
    if (name.empty())
    {
        const passwd* entry = getpwuid(geteuid());
        if (entry != NULL && entry->pw_name != NULL)
            name = FromUtf8(entry->pw_name);
    }
    return name;
#endif
}

static void MultiplyFnv128(unsigned long long& high, unsigned long long& low)
{
    const unsigned long long primeLow = 0x13b;
    unsigned long long lowLow = (low & 0xffffffffULL) * primeLow;
    unsigned long long lowHigh = (low >> 32) * primeLow;
    unsigned long long carry = (lowHigh + (lowLow >> 32)) >> 32;
    unsigned long long productLow = lowLow + (lowHigh << 32);
    unsigned long long productHigh = high * primeLow + carry + (low << 24);
    high = productHigh;
    low = productLow;
}

CProfile::PROFILE_DETAILS::PROFILE_DETAILS() : signedIn(false), signingIn(false), onlineAccount(false), state(0)
{
    std::memset(&uuid, 0, sizeof(uuid));
    std::memset(name, 0, sizeof(name));
}

CProfile::CONTEXTVAL::CONTEXTVAL() : std::unordered_map<int, int>(11)
{
}

CProfile::CProfile()
    : mPresenceContexts(11), mPresenceStrings(11), mPresenceContextStrings(11), mInitialisedSaveDirectories(0)
{
    gProfileInstance = this;
    mFatalTrophyErrorId = 0;
    mProfileProcessState = eProfileProcessState_Idle;
    mPlayerName = LocalPlayerName();
    mPrimaryPlayerChanged = false;
    SetSignInChange(false);
    mSignedInUserCount = 0;
    mSignedInUsersMask = 0;
    mRequestOnline = false;
    mRequestGuest = false;
    mRequestSubAccount = false;
    mRequestForce = false;
    mRequestTrial = false;
    mRequestFromGame = false;
    mLockedProfile = -1;
    mSignInUIHandler = NULL;
    mSignInChoicesHandler = NULL;
    mSignInUIContext = NULL;
    mSignInChoicesPending = false;
    mSignInChoicesAccepted = false;
    mSignInChoice = 0;
    mSignInChoiceExtra = 0;
    mOfflineProfileHandler = NULL;
    mOfflineProfileContext = NULL;
    mOfflineProfileQuadrant = -1;
    mGetStringHandler = NULL;
    mPlayerListTitleId = 0;
    mServiceId = NULL;
    mMinimumAge = 99;
    mNetworkAvailable = false;
    mNetworkStatus = false;
    mFriendPresenceOnline = false;
    std::memset(mNetworkAccountId, 0, sizeof(mNetworkAccountId));
    InitializeCriticalSection(&mInitSaveDirCriticalSection);
    mLeaderboardDisable = false;
    mPrimaryPad = 0;
    mLiveConnectionStatus = 0;
    mSignInChangeHandler = NULL;
    mSignInChangeContext = NULL;
    mProfileReadErrorHandler = NULL;
    mProfileReadErrorContext = NULL;
    mNotificationsHandler = NULL;
    mNotificationsContext = NULL;
    mPresenceUpdatePending = false;
    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        mCurrentGameActivity[quadrant] = 0;
        mAchievementsByPad[quadrant] = NULL;
    }
    mRichPresenceStringHandler = NULL;
    mFriendPresenceContext = NULL;
    SystemOnCreate();
    AwardManagerOnCreate();
    RichPresenceOnCreate();
}

CProfile::~CProfile()
{
    AwardManagerOnDestroy();
    DeleteCriticalSection(&mInitSaveDirCriticalSection);
    DeleteCriticalSection(&mAwardMutex);
    if (gProfileInstance == this)
        gProfileInstance = NULL;
}

std::wstring CProfile::LocalPlayerName()
{
    std::wstring name = TrimName(FromUtf8(PcGetLaunchOptions().username.c_str()));
    if (name.empty())
        name = TrimName(OperatingSystemUserName());
    if (name.empty())
        name = L"Player";
    if (name.length() > kMaxPlayerNameLength)
    {
        name.resize(kMaxPlayerNameLength);
        unsigned int last = static_cast<unsigned int>(name[name.length() - 1]);
        if (sizeof(wchar_t) == 2 && last >= 0xd800 && last < 0xdc00)
            name.resize(name.length() - 1);
    }
    return name;
}

void CProfile::SetPlayerName(const std::wstring& name)
{
    mPlayerName = name.substr(0, kMaxPlayerNameLength);
}

ACTUuid CProfile::LocalPlayerUuid(const std::wstring& name)
{
    unsigned long long high = 0x6c62272e07bb0142ULL;
    unsigned long long low = 0x62b821756295c58dULL;
    std::string bytes = ToUtf8(name);
    for (std::string::size_type index = 0; index < bytes.length(); ++index)
    {
        low ^= static_cast<unsigned char>(bytes[index]);
        MultiplyFnv128(high, low);
    }
    ACTUuid uuid;
    for (int index = 0; index < 8; ++index)
    {
        uuid.data[index] = static_cast<unsigned char>(high >> (56 - index * 8));
        uuid.data[index + 8] = static_cast<unsigned char>(low >> (56 - index * 8));
    }
    return uuid;
}

void CProfile::SetSignInChange(bool changed)
{
    mSignInChange = changed;
}

void CProfile::Initialise(const void*, const void*, unsigned short, unsigned int, unsigned int, unsigned int*, int,
                          unsigned int*)
{
    if (!mProfiles[0].signedIn)
        SignInUser(-1);
}

int CProfile::GetLockedProfile()
{
    return mLockedProfile;
}

void CProfile::SetLockedProfile(int quadrant)
{
    if (mLockedProfile != quadrant)
    {
        if (quadrant >= 0)
            OnSignin(quadrant);
        mLockedProfile = quadrant;
    }
}

bool CProfile::IsSignedIn(int quadrant)
{
    return quadrant >= 0 && quadrant < 4 && mProfiles[quadrant].signedIn;
}

bool CProfile::IsSignedInLive(int quadrant)
{
    return IsSignedIn(quadrant) && mNetworkAvailable && mFriendPresenceOnline;
}

bool CProfile::IsSigningIn(int quadrant)
{
    return quadrant >= 0 && quadrant < 4 && mProfiles[quadrant].signingIn;
}

bool CProfile::IsGuest(int)
{
    return false;
}

int CProfile::RequestSignInUI(bool online, bool guest, bool subAccount, bool trial, bool fromGame,
                              SignInUIHandler handler, void* context, int quadrant)
{
    return RequestSignInUI(online, guest, subAccount, trial, false, fromGame, handler, context, quadrant);
}

int CProfile::RequestSignInUI(bool online, bool guest, bool subAccount, bool trial, bool force, bool fromGame,
                              SignInUIHandler handler, void* context, int quadrant)
{
    mRequestOnline = online;
    mRequestGuest = guest;
    mRequestSubAccount = subAccount;
    mRequestForce = force;
    mRequestTrial = trial;
    mRequestFromGame = fromGame;
    if (quadrant >= 4 && quadrant < 9 && mPickerPad >= 0)
        return 0;
    mSignInUIHandler = handler;
    mSignInUIContext = context;
    if (quadrant >= 4 && quadrant < 9)
    {
        std::vector<PcPlayerProfile> choices = UnusedLauncherProfiles();
        if (!choices.empty())
        {
            mPickerPad = quadrant;
            gConsoleUIController.SetRawPadMenuOpen(quadrant, true);
            if (gConsoleUIController.NavigateToScene(quadrant, EUIScene_CustomMenu,
                                                     new PcProfilePickerPage(quadrant, choices),
                                                     EUILayer_Fullscreen, EUIGroup_Fullscreen))
                return 0;
            mPickerPad = -1;
            gConsoleUIController.SetRawPadMenuOpen(quadrant, false);
        }
    }
    int choice = 0;
    for (int player = 0; player < 4; ++player)
    {
        if (!IsSignedIn(player))
        {
            choice = player;
            break;
        }
    }
    SignInUser(quadrant);
    SetSignInChoicesComplete(true, choice, quadrant);
    return 0;
}

void CProfile::ProfilePickerClosed(int pad, const PcPlayerProfile* chosen)
{
    if (pad != mPickerPad)
        return;
    mPickerPad = -1;
    gConsoleUIController.SetRawPadMenuOpen(pad, false);
    if (chosen == NULL)
    {
        SetSignInChoicesComplete(false, -1, pad);
        return;
    }
    const int quadrant = SignInAs(pad, chosen);
    SetSignInChoicesComplete(quadrant >= 0, quadrant, pad);
}

std::vector<PcPlayerProfile> CProfile::UnusedLauncherProfiles()
{
    std::vector<PcPlayerProfile> unused;
    std::vector<PcPlayerProfile> profiles = PcReadPlayerProfiles();
    for (size_t index = 0; index < profiles.size(); ++index)
    {
        const ACTUuid uuid = UuidFromHex(profiles[index].uuid);
        bool used = false;
        for (int quadrant = 0; quadrant < 4 && !used; ++quadrant)
            used = mProfiles[quadrant].signedIn && mProfiles[quadrant].uuid == uuid;
        if (!used)
            unused.push_back(profiles[index]);
    }
    return unused;
}

ACTUuid CProfile::UuidFromHex(const std::string& hex)
{
    ACTUuid uuid;
    for (int index = 0; index < 16; ++index)
        uuid.data[index] = static_cast<unsigned char>(std::strtoul(hex.substr(index * 2, 2).c_str(), NULL, 16));
    return uuid;
}

int CProfile::SignInUser(int pad)
{
    return SignInAs(pad, NULL);
}

int CProfile::SignInAs(int pad, const PcPlayerProfile* chosen)
{
    int quadrant = 0;
    while (quadrant < 4 && mProfiles[quadrant].signedIn)
        ++quadrant;
    if (quadrant == 4)
    {
        SetSignInChange(true);
        return -1;
    }
    PROFILE_DETAILS& profile = mProfiles[quadrant];
    std::wstring name = chosen != NULL ? chosen->username : mPlayerName;
    if (chosen == NULL && quadrant != 0)
        name += L" (" + std::to_wstring(quadrant + 1) + L")";
    std::memset(profile.name, 0, sizeof(profile.name));
    std::wcsncpy(profile.name, name.c_str(), sizeof(profile.name) / sizeof(profile.name[0]) - 1);
    profile.uuid = LocalPlayerUuid(std::wstring(profile.name));
    const std::string& launchUuid = PcGetLaunchOptions().uuid;
    if (chosen != NULL)
        profile.uuid = UuidFromHex(chosen->uuid);
    // The launcher's profile UUID is the persistent world identity. Never
    // replace it with a generated Live-account UUID during launch.
    if (quadrant == 0 && launchUuid.size() == 32)
        profile.uuid = UuidFromHex(launchUuid);
    profile.onlineAccount = false;
    profile.signedIn = true;
    OnSignin(pad);
    if (pad >= 0)
        g_pInput->LockPad(pad, quadrant);
    SetSignInChange(true);
    return quadrant;
}

void CProfile::SignOutUser(int quadrant)
{
    if (quadrant >= 0 && quadrant < 4 && IsSignedIn(quadrant))
        mSignOutQueue.push_back(quadrant);
}

void CProfile::SetUserSigningIn(int quadrant, bool signingIn)
{
    if (quadrant >= 0 && quadrant < 4)
        mProfiles[quadrant].signingIn = signingIn;
}

int CProfile::DisplayOfflineProfile(OfflineProfileHandler handler, void* context, int quadrant)
{
    mOfflineProfileHandler = handler;
    mOfflineProfileContext = context;
    mOfflineProfileQuadrant = quadrant;
    mProfileProcessState = eProfileProcessState_OfflineProfile;
    return 0;
}

void CProfile::RequestConvertOfflineToGuestUI(SignInUIHandler handler, void* context, int quadrant)
{
    RequestSignInUI(false, false, false, false, true, true, handler, context, quadrant);
}

void CProfile::SetPrimaryPlayerChanged(bool changed)
{
    mPrimaryPlayerChanged = changed;
}

int CProfile::QuerySigninStatus()
{
    mSignedInUserCount = 0;
    mSignedInUsersMask = 0;
    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        if (mProfiles[quadrant].signedIn)
        {
            ++mSignedInUserCount;
            mSignedInUsersMask |= 1 << quadrant;
        }
    }
    return 0;
}

PlayerUID CProfile::QuadrantOfflineXUID(int quadrant)
{
    PlayerUID uid(0);
    if (quadrant < 0 || quadrant >= 4 || !mProfiles[quadrant].signedIn)
        return uid;
    uid.uuid = mProfiles[quadrant].uuid;
    uid.flags = PlayerUID::FlagUuid | (mProfiles[quadrant].onlineAccount ? PlayerUID::FlagOnlineAccount : 0u);
    return uid;
}

PlayerUID CProfile::QuadrantOnlineXUID(int quadrant)
{
    if (mFriendPresenceOnline)
        return QuadrantOfflineXUID(quadrant);
    return PlayerUID(0);
}

void CProfile::GetXUID(int quadrant, PlayerUID* uid, bool online)
{
    *uid = online ? QuadrantOnlineXUID(quadrant) : QuadrantOfflineXUID(quadrant);
}

int CProfile::AreXUIDSEqual(PlayerUID first, PlayerUID second)
{
    return first == second;
}

bool CProfile::AllowedToPlayMultiplayer(int quadrant)
{
    return IsSignedInLive(quadrant);
}

void CProfile::AllowedPlayerCreatedContent(int quadrant, bool singlePlayer, int* allowed, int* communicationAllowed)
{
    int result = true;
    for (int player = 0; player < 4; ++player)
    {
        if (!singlePlayer || player == quadrant)
        {
            if (IsSignedIn(player) && !IsGuest(player))
            {
                bool contentRestricted = false;
                GetChatAndContentRestrictions(player, true, NULL, &contentRestricted, NULL);
                result = !contentRestricted;
            }
            if (!result)
            {
                break;
            }
        }
    }
    *allowed = result;
    *communicationAllowed = result;
}

bool CProfile::CanViewPlayerCreatedContent(int quadrant, bool singlePlayer, PlayerUID*, unsigned int)
{
    bool result = true;
    for (int player = 0; player < 4; ++player)
    {
        if (!singlePlayer || player == quadrant)
        {
            int allowed = 1;
            int communicationAllowed = 1;
            AllowedPlayerCreatedContent(player, true, &allowed, &communicationAllowed);
            if (!allowed && !communicationAllowed)
            {
                result = false;
                break;
            }
        }
    }
    return result;
}

bool CProfile::GetProfileAvatar(int quadrant, AvatarHandler handler, void* context)
{
    AvatarRequest request = {quadrant, context, handler};
    mAvatarRequests.push_back(request);
    return true;
}

void CProfile::CancelProfileAvatarRequest()
{
}

void CProfile::SetGetStringFunc(GetStringHandler handler)
{
    mGetStringHandler = handler;
}

void CProfile::SetPlayerListTitleID(int titleId)
{
    mPlayerListTitleId = titleId;
}

void CProfile::SetFatalTrophyErrorID(int stringId)
{
    mFatalTrophyErrorId = stringId;
}

wchar_t* CProfile::GetMiiName(int quadrant)
{
    static wchar_t empty[] = L"";
    return IsSignedIn(quadrant) ? mProfiles[quadrant].name : empty;
}

wchar_t* CProfile::GetGamertag(int quadrant)
{
    return GetMiiName(quadrant);
}

std::wstring CProfile::GetDisplayName(int quadrant)
{
    return std::wstring(GetGamertag(quadrant));
}

char* CProfile::GetNetworkAccountID()
{
    return mNetworkAccountId;
}

void CProfile::SetSignInChoicesComplete(bool accepted, int choice, int extra)
{
    mSignInChoicesAccepted = accepted;
    mSignInChoice = choice;
    mSignInChoiceExtra = extra;
    mSignInChoicesPending = true;
}

void CProfile::ResetProfileProcessState()
{
    mProfileProcessState = eProfileProcessState_Idle;
    gStorage->SetSaveDisabled(false);
}

bool CProfile::GetSignInChange()
{
    return mSignInChange;
}

void CProfile::CheckForSignInChange()
{
    if (GetSignInChange())
    {
        SetSignInChange(false);
        if (QuerySigninStatus() == 0)
            CallSignInChangeCallback(mPrimaryPlayerChanged);
        else
        {
            mPrimaryPlayerChanged = true;
            CallSignInChangeCallback(true);
        }
    }
    if (mSignInChoicesPending)
    {
        mSignInChoicesPending = false;
        if (mSignInUIHandler != NULL)
            mSignInUIHandler(mSignInUIContext, mSignInChoicesAccepted, mSignInChoice, mSignInChoiceExtra);
    }
}

bool CProfile::PrimaryPlayerChanged()
{
    return mPrimaryPlayerChanged;
}

bool CProfile::PrimaryPlayerHasSignedOutDialogBox(bool)
{
    return false;
}

void CProfile::SetProfileProcessState(EProfileProcessState state)
{
    mProfileProcessState = state;
}

void CProfile::Tick()
{
    TickRichPresence();
    CheckForSignInChange();
    AwardManagerTick();
    while (!mAvatarRequests.empty())
    {
        AvatarRequest request = mAvatarRequests.back();
        mAvatarRequests.pop_back();
        if (request.handler != NULL)
            request.handler(request.context, NULL, static_cast<unsigned int>(CreateAvatarTexture(request.quadrant)));
    }
    if (!mSignOutQueue.empty())
    {
        int quadrant = mSignOutQueue.front();
        if (gStorage->IsOptionsWriteInProgressOrPending(quadrant) == 0)
        {
            if (mProfiles[quadrant].signedIn)
            {
                std::memset(&mProfiles[quadrant], 0, sizeof(PROFILE_DETAILS));
                g_pInput->UnlockPad(quadrant);
            }
            SetSignInChange(true);
            mSignOutQueue.pop_front();
        }
    }
    if (mProfileProcessState == eProfileProcessState_Idle)
    {
        SetLeaderboardDisable(false);
        if (PrimaryPlayerChanged() && GetLockedProfile() != -1)
            SetLockedProfile(-1);
        else
            SetPrimaryPlayerChanged(false);
    }
    else if (mProfileProcessState == eProfileProcessState_Unknown7)
    {
        SetProfileProcessState(eProfileProcessState_Unknown12);
    }
    else if (mProfileProcessState == eProfileProcessState_Unknown11)
    {
        if (PrimaryPlayerChanged() && PrimaryPlayerHasSignedOutDialogBox(true))
            SetLockedProfile(-1);
    }
}

int CProfile::CreateAvatarTexture(int quadrant)
{
    if (!IsSignedIn(quadrant))
        return 0;
    Minecraft* minecraft = Minecraft::GetInstance();
    if (minecraft == NULL || minecraft->getTextureManager() == NULL)
        return 0;
    BufferedImage* avatar = PcRenderAvatarHead(quadrant, kAvatarSize);
    if (avatar == NULL)
        return 0;
    return minecraft->getTextureManager()->getTexture(avatar, C4JRender::eTextureFormat_RGBA8, false);
}

void CProfile::SetFriendPresenceOnline(bool online)
{
    mFriendPresenceOnline = online;
}

void CProfile::SetNetworkStatus(bool online)
{
    mFriendPresenceOnline = online;
    mNetworkAvailable = online;
    if (mNetworkStatus != online)
    {
        mNetworkStatus = online;
        SetSignInChange(true);
    }
}

void CProfile::SetNetworkStatusWithoutSigninChange(bool online)
{
    mFriendPresenceOnline = online;
    mNetworkAvailable = online;
    mNetworkStatus = online;
}

void CProfile::TryToConnectOnline()
{
}

unsigned int CProfile::GetSignedInUsersMask()
{
    return mSignedInUsersMask;
}

void CProfile::RefreshChatAndContentRestrictions(ProfileReadErrorHandler, void*)
{
}

bool CProfile::GetChatAndContentRestrictions(int, bool* chatRestricted, bool* contentRestricted, int* age)
{
    if (chatRestricted != NULL)
        *chatRestricted = false;
    if (contentRestricted != NULL)
        *contentRestricted = false;
    if (age != NULL)
        *age = 100;
    return true;
}

bool CProfile::GetChatAndContentRestrictions(int quadrant, bool, bool* chatRestricted, bool* contentRestricted,
                                             int* age)
{
    return GetChatAndContentRestrictions(quadrant, chatRestricted, contentRestricted, age);
}

void CProfile::SetServiceID(char* serviceId)
{
    mServiceId = serviceId;
}

void CProfile::HandleNetworkTicket(int, void*)
{
}

void CProfile::SetMinimumAge(int age)
{
    mMinimumAge = age;
}

unsigned int CProfile::getUserID(int)
{
    return 0;
}

int CProfile::getQuadrant(ACTUuid& uuid)
{
    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        if (mProfiles[quadrant].signedIn && mProfiles[quadrant].uuid == uuid)
            return quadrant;
    }
    return -1;
}

void CProfile::SetSignInChoicesCallback(SignInChoicesHandler handler)
{
    mSignInChoicesHandler = handler;
}

bool CProfile::IsHasAchievementsFile(int quadrant)
{
    CAchievements* achievements = getAchievements(quadrant);
    return achievements != NULL && achievements->mHasAchievementsFile;
}

bool CProfile::IsValidPlayer(int quadrant)
{
    return quadrant >= 0 && quadrant < 4 && (mSignedInUsersMask & (1 << quadrant)) != 0;
}

int CProfile::GetSlotId(int quadrant)
{
    return IsSignedIn(quadrant) ? quadrant + 1 : 0;
}

void CProfile::Terminate()
{
    AwardManagerOnDestroy();
}

void CProfile::RichPresenceRegisterPresenceString(int id, int stringId)
{
    mPresenceStrings[id] = stringId;
}

void CProfile::RichPresenceRegisterContext(int id, int contextId)
{
    mPresenceContexts[id] = contextId;
    for (int quadrant = 0; quadrant < 4; ++quadrant)
        mContextValues[quadrant][id] = -1;
}

void CProfile::RichPresenceRegisterContextString(int context, int value, int stringId)
{
    mPresenceContextStrings[context][value] = stringId;
}

void CProfile::RichPresenceInit(int, int titleId)
{
    mRichPresenceTitleId = titleId;
    mCurrentPresenceString = L"";
    mPresenceUpdatePending = false;
}

const wchar_t* CProfile::getStringFromID(int stringId)
{
    const wchar_t* text = mRichPresenceStringHandler(stringId);
    return text != NULL ? text : L"";
}

void CProfile::CreatePresenceString(int quadrant, std::wstring& text)
{
    text.assign(getStringFromID(mPresenceStrings[mCurrentGameActivity[quadrant]]));
    for (std::unordered_map<int, int>::const_iterator context = mPresenceContexts.begin();
         context != mPresenceContexts.end(); ++context)
    {
        const wchar_t* formatToken = getStringFromID(context->second);
        if (formatToken[0] == L'\0')
            continue;
        std::wstring::size_type position = text.find(formatToken);
        if (position != std::wstring::npos)
        {
            int setting = mContextValues[quadrant][context->first];
            const wchar_t* replacement = getStringFromID(mPresenceContextStrings[context->first][setting]);
            text.replace(position, std::wcslen(formatToken), replacement);
        }
    }
}

void CProfile::SetPresenceString(int presence)
{
    if (presence != GetPrimaryPad())
        return;
    if (mRichPresenceStringHandler == NULL)
    {
        mPresenceUpdatePending = true;
        return;
    }
    std::wstring text;
    CreatePresenceString(presence, text);
    mPresenceUpdatePending = false;
    if (text != mCurrentPresenceString)
    {
        DebugPrintf("Rich presence - %ls\n", text.c_str());
        mCurrentPresenceString = text;
        PcDiscordSetPresence(text);
    }
}

void CProfile::SetRichPresenceContextValue(int context, int value, int setting)
{
    if (context == 0 && mContextValues[0][value] != setting)
    {
        mContextValues[0][value] = setting;
        SetPresenceString(0);
    }
}

void CProfile::SetCurrentGameActivity(int activity, int presence, bool force)
{
    if (activity == 0)
    {
        if (mCurrentGameActivity[0] != presence)
        {
            mCurrentGameActivity[0] = presence;
            SetPresenceString(0);
        }
        if (force)
        {
            for (int quadrant = 1; quadrant < 4; ++quadrant)
            {
                if (mCurrentGameActivity[quadrant] != 0)
                {
                    mCurrentGameActivity[quadrant] = 0;
                    SetPresenceString(quadrant);
                }
            }
        }
    }
}

void CProfile::SetRichPresenceSettingFn(RichPresenceSettingHandler handler)
{
    mRichPresenceSettingHandler = handler;
}

void CProfile::SetFriendPresenceChangeCallback(FriendPresenceHandler handler, void* context)
{
    mFriendPresenceHandler = handler;
    mFriendPresenceContext = context;
}

void CProfile::SetRichPresenceGetStringCallback(RichPresenceStringHandler handler)
{
    mRichPresenceStringHandler = handler;
}

void CProfile::RichPresenceOnCreate()
{
    mRichPresenceTitleId = 0;
    mRichPresenceSettingHandler = NULL;
    mFriendPresenceHandler = NULL;
}

void CProfile::TickRichPresence()
{
    if (mPresenceUpdatePending)
        SetPresenceString(GetPrimaryPad());
}

int CProfile::GetPrimaryPad()
{
    return mPrimaryPad;
}

void CProfile::SetPrimaryPad(int pad)
{
    if (pad >= 0)
        g_pInput->LockPad(pad, 0);
    mPrimaryPad = 0;
}

void CProfile::ClearGameUsers()
{
    g_pInput->UnlockPad(0);
    for (int quadrant = 1; quadrant < 4; ++quadrant)
    {
        if (IsSignedIn(quadrant))
            SignOutUser(quadrant);
    }
}

void CProfile::SetSignInChangeCallback(SignInChangeHandler handler, void* context)
{
    mSignInChangeHandler = handler;
    mSignInChangeContext = context;
}

void CProfile::SetNotificationsCallback(NotificationsHandler handler, void* context)
{
    mNotificationsHandler = handler;
    mNotificationsContext = context;
}

bool CProfile::RegionIsNorthAmerica()
{
    return false;
}

bool CProfile::LocaleIsUSorCanada()
{
    return false;
}

int CProfile::GetLiveConnectionStatus()
{
    return mLiveConnectionStatus;
}

bool CProfile::IsSystemUIDisplayed()
{
    return mSystemUIShowing;
}

void CProfile::SetProfileReadErrorCallback(ProfileReadErrorHandler handler, void* context)
{
    mProfileReadErrorHandler = handler;
    mProfileReadErrorContext = context;
}

void CProfile::DebugPrintf(const char* format, ...)
{
    static const Logger log("Profile");
    thread_local std::string pending;

    char buffer[512];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length < 0)
        pending += format;
    else if (static_cast<std::size_t>(length) < sizeof(buffer))
        pending.append(buffer, static_cast<std::size_t>(length));
    else
    {
        std::string text(static_cast<std::size_t>(length), '\0');
        va_start(arguments, format);
        std::vsnprintf(&text[0], text.size() + 1, format, arguments);
        va_end(arguments);
        pending += text;
    }

    std::string::size_type lineEnd;
    while ((lineEnd = pending.find('\n')) != std::string::npos)
    {
        log.WriteText(LogLevel::Debug, pending.substr(0, lineEnd));
        pending.erase(0, lineEnd + 1);
    }
}

void CProfile::SystemOnCreate()
{
    mSystemUIShowing = false;
}

void CProfile::SetLeaderboardDisable(bool disabled)
{
    mLeaderboardDisable = disabled;
}

void CProfile::CallSignInChangeCallback(bool primaryChanged)
{
    if (mSignInChangeHandler != NULL)
        mSignInChangeHandler(mSignInChangeContext, primaryChanged, mSignedInUsersMask);
}

bool CProfile::AddAwardManagerTask(CAchievements* achievements)
{
    EnterCriticalSection(&mAwardMutex);
    mAwardQueue.push_back(achievements);
    LeaveCriticalSection(&mAwardMutex);
    return true;
}

int CProfile::GetRegistrationId(int achievementId) const
{
    if (achievementId < 0 || achievementId >= kAwardCount)
        return -1;
    return mAwardRegistrations[achievementId].registrationId;
}

void CProfile::RegisterAward(int achievementId, int registrationId, eAwardType, int callbackValue, int callbackContext)
{
    if (achievementId < 0 || achievementId >= kAwardCount)
        return;
    AwardRegistration& registration = mAwardRegistrations[achievementId];
    registration.registrationId = registrationId;
    registration.callbackValue = callbackValue;
    registration.callbackContext = callbackContext;
}

int CProfile::GetAwardId(int achievementId)
{
    return achievementId;
}

eAwardType CProfile::GetAwardType(int)
{
    return eAwardType_None;
}

bool CProfile::CanBeAwarded(int, int)
{
    return true;
}

void CProfile::OnSignout(int quadrant)
{
    if (quadrant >= 0 && quadrant < 4 && IsValidPlayer(quadrant) && mAchievementsByPad[quadrant] != NULL)
    {
        mAchievements.insert(mAchievementsByPad[quadrant]);
        mAchievementsByPad[quadrant] = NULL;
    }
}

void CProfile::OnSignin(int quadrant)
{
    if (quadrant < 0 || quadrant >= 4 || !IsValidPlayer(quadrant))
        return;

    PlayerUID uid = getPlayerUID(quadrant, false);
    CAchievements*& achievements = mAchievementsByPad[quadrant];
    if (achievements == NULL)
    {
        achievements = new CAchievements(quadrant);
    }
    else if (achievements->mPlayerUID != uid)
    {
        OnSignout(quadrant);
        OnSignin(quadrant);
    }
}

CAchievements* CProfile::getAchievements(int quadrant)
{
    if (quadrant < 0 || quadrant >= 4)
        return NULL;
    OnSignin(quadrant);
    return mAchievementsByPad[quadrant];
}

CAchievements* CProfile::getAchievementsByPad(int quadrant) const
{
    if (quadrant < 0 || quadrant >= 4)
        return NULL;
    return mAchievementsByPad[quadrant];
}

bool CProfile::callAwardManagerCallback(int quadrant, int achievementId) const
{
    CAchievements* achievements = getAchievementsByPad(quadrant);
    if (achievements != NULL && achievements->mIsSystemPlayer)
    {
        if (mAwardHandler != NULL)
        {
            const AwardRegistration& registration = mAwardRegistrations[achievementId];
            mAwardHandler(mAwardContext, quadrant, achievementId, registration.callbackValue,
                          registration.callbackContext);
            return true;
        }
        return false;
    }
    return false;
}

void CProfile::Award(int quadrant, int achievementId, bool)
{
    PlayerUID uid = getPlayerUID(quadrant, false);
    CAchievements* achievements = getAchievements(quadrant);
    if (achievementId < 0 || achievementId >= kAwardCount || achievements == NULL)
    {
        std::wstring uidString = uid.toString();
        DebugPrintf("<CProfile::IsAwardsFlagSet> Cannot award trophy.Player '%ls', iQuad=%i, iAward=%i, pAch=%p.\n",
                    uidString.c_str(), quadrant, achievementId, achievements);
        return;
    }
    if (!achievements->IsIdle())
    {
        achievements->addPendingAward(achievementId);
        return;
    }
    if (!achievements->SetAwardedBit(achievementId, true))
        callAwardManagerCallback(quadrant, achievementId);
}

bool CProfile::IsAwardsFlagSet(int quadrant, int achievementId)
{
    CAchievements* achievements = getAchievements(quadrant);
    return achievements != NULL && achievementId >= 0 && achievementId < kAwardCount
           && achievements->GetAwardedBit(achievementId);
}

void CProfile::SetOnAwardHandler(AwardHandler handler, void* context)
{
    mAwardHandler = handler;
    mAwardContext = context;
}

CAchievements* CProfile::nextAwardManagerTask()
{
    CAchievements* achievements = NULL;
    EnterCriticalSection(&mAwardMutex);
    if (!mAwardQueue.empty())
    {
        achievements = mAwardQueue.front();
        mAwardQueue.pop_front();
    }
    LeaveCriticalSection(&mAwardMutex);
    return achievements;
}

void CProfile::EnterInitSaveDirCS()
{
    EnterCriticalSection(&mInitSaveDirCriticalSection);
}

void CProfile::LeaveInitSaveDirCS()
{
    LeaveCriticalSection(&mInitSaveDirCriticalSection);
}

bool CProfile::initSaveDirectory(unsigned char slot)
{
    bool result = true;
    unsigned int slotMask = 1U << slot;
    EnterInitSaveDirCS();
    if ((mInitialisedSaveDirectories & slotMask) == 0)
    {
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(PcSaveRoot()), error);
        if (error)
            result = false;
        else
            mInitialisedSaveDirectories |= slotMask;
    }
    LeaveInitSaveDirCS();
    return result;
}

void CProfile::readAwardManagerFile(CAchievements* achievements)
{
    initSaveDirectory(achievements->mSlotId);
    std::filesystem::path path = std::filesystem::path(PcSaveRoot()) / achievements->GetFilename();
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (file.is_open())
    {
        unsigned char data[kAwardFileSize * 4];
        std::memset(data, 0, sizeof(data));
        file.read(reinterpret_cast<char*>(data), kAwardFileSize);
        int bytesRead = static_cast<int>(file.gcount());
        file.close();
        achievements->mHasAchievementsFile = true;
        achievements->ReadFrom(data, bytesRead);
        return;
    }
    achievements->ReadFrom(NULL, 0);
}

void CProfile::writeAwardManagerFile(CAchievements* achievements)
{
    initSaveDirectory(achievements->mSlotId);
    std::filesystem::path path = std::filesystem::path(PcSaveRoot()) / achievements->GetFilename();
    std::filesystem::path temporaryPath = path;
    temporaryPath += ".tmp";
    std::ofstream file(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        achievements->WriteTo(NULL, 0);
        return;
    }
    unsigned char data[kAwardFileSize * 4];
    std::memset(data, 0, sizeof(data));
    achievements->mHasAchievementsFile = true;
    achievements->WriteTo(data, kAwardFileSize);
    file.write(reinterpret_cast<const char*>(data), kAwardFileSize);
    file.close();
    std::error_code error;
    if (!file.fail())
        std::filesystem::rename(temporaryPath, path, error);
    if (file.fail() || error)
        std::filesystem::remove(temporaryPath, error);
}

int CProfile::AwardManagerThreadProc(void* argument)
{
    CProfile* profile = static_cast<CProfile*>(argument);
    profile->DebugPrintf("<CAwardManager> Thread Starting: %p.\n", argument);
    for (;;)
    {
        EnterCriticalSection(&profile->mAwardMutex);
        bool exiting = profile->mAwardThreadExit;
        LeaveCriticalSection(&profile->mAwardMutex);
        if (!exiting)
            C4JThread::Sleep(kAwardThreadInterval);

        CAchievements* achievements = profile->nextAwardManagerTask();
        if (achievements == NULL)
        {
            if (exiting)
                return 0;
            continue;
        }

        unsigned int state = achievements->GetState();
        if (state == 2)
            profile->writeAwardManagerFile(achievements);
        else if (state == 1)
            profile->readAwardManagerFile(achievements);
    }
}

void CProfile::AwardManagerOnCreate()
{
    mAwardHandler = NULL;
    mAwardContext = NULL;
    InitializeCriticalSection(&mAwardMutex);
    for (int index = 0; index < kAwardCount; ++index)
    {
        mAwardRegistrations[index].registrationId = -1;
        mAwardRegistrations[index].callbackValue = -1;
        mAwardRegistrations[index].callbackContext = -1;
    }
    mAwardThreadExit = false;
    mAwardThread = new C4JThreadImpl(AwardManagerThreadProc, this, "AwardManager", 60000);
    mAwardThread->Run();
}

void CProfile::AwardManagerTick()
{
    std::set<CAchievements*>::iterator deleting = mAchievements.begin();
    while (deleting != mAchievements.end())
    {
        CAchievements* achievements = *deleting;
        if (!achievements->IsIdle())
        {
            ++deleting;
        }
        else if (achievements->hasPendingAwards())
        {
            std::set<int> awardsNotRegistered;
            achievements->awardPending(awardsNotRegistered);
            ++deleting;
        }
        else if (achievements->IsDirty())
        {
            achievements->StartWrite();
            ++deleting;
        }
        else
        {
            mAchievements.erase(deleting++);
            delete achievements;
        }
    }

    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        CAchievements* achievements = getAchievements(quadrant);
        if (achievements == NULL)
            continue;
        if (achievements->hasPendingAwards())
        {
            std::set<int> awardsNotRegistered;
            achievements->awardPending(awardsNotRegistered);
            for (std::set<int>::const_iterator award = awardsNotRegistered.begin(); award != awardsNotRegistered.end();
                 ++award)
                callAwardManagerCallback(quadrant, *award);
        }
        else if (achievements->IsIdle())
        {
            achievements->StartWrite();
        }
    }
}

void CProfile::AwardManagerOnDestroy()
{
    if (mAwardThread == NULL)
        return;

    EnterCriticalSection(&mAwardMutex);
    mAwardThreadExit = true;
    LeaveCriticalSection(&mAwardMutex);
    mAwardThread->WaitForCompletion(-1);
    delete mAwardThread;
    mAwardThread = NULL;

    for (int quadrant = 0; quadrant < 4; ++quadrant)
    {
        if (mAchievementsByPad[quadrant] != NULL)
        {
            mAchievements.insert(mAchievementsByPad[quadrant]);
            mAchievementsByPad[quadrant] = NULL;
        }
    }
    for (std::set<CAchievements*>::iterator it = mAchievements.begin(); it != mAchievements.end(); ++it)
    {
        CAchievements* achievements = *it;
        if (achievements->IsIdle())
        {
            std::set<int> awardsNotRegistered;
            achievements->awardPending(awardsNotRegistered);
            achievements->StartWrite();
        }
    }
    for (CAchievements* achievements = nextAwardManagerTask(); achievements != NULL;
         achievements = nextAwardManagerTask())
    {
        unsigned int state = achievements->GetState();
        if (state == 2)
            writeAwardManagerFile(achievements);
        else if (state == 1)
            readAwardManagerFile(achievements);
    }
    for (std::set<CAchievements*>::iterator it = mAchievements.begin(); it != mAchievements.end(); ++it)
        delete *it;
    mAchievements.clear();
}
