#include "platform/wiiu/WiiULiveSignIn.hpp"
#include "game/core/CriticalSection.hpp"
#include "game/core/System.hpp"
#include "platform/wiiu/WiiURelayTls.hpp"
#include "net/HttpResponse.hpp"
#include "net/NetLink.hpp"
#if LCE_PLATFORM_SWITCH
#include "platform/pc/PcPaths.hpp"
#include "platform/switch/SwitchLiveWorker.hpp"
#include <cstdio>
#else
#include "platform/wiiu/CProfile.hpp"
#include <coreinit/dynload.h>
#include <coreinit/ios.h>
#include <coreinit/thread.h>
#include <nn/act/client_cpp.h>
#include <nn/save/save.h>
#include <nsysnet/nssl.h>
#endif
#include <curl/curl.h>
#include <cstring>
#include <algorithm>
#include <cwchar>
#include <deque>
#include <utility>

namespace WiiULiveSignIn
{
namespace
{
const char kStartUrl[] = "https://api.lcereborn.xyz/v1/device/start";
const char kTokenUrl[] = "https://api.lcereborn.xyz/v1/device/token";
const char kMeUrl[] = "https://api.lcereborn.xyz/v1/me";
const char kPresenceUrl[] = "https://api.lcereborn.xyz/v1/presence";
const char kApiUrl[] = "https://api.lcereborn.xyz/v1";
const char kSessionFile[] = "lce_live_session.dat";
const char kLowerHex[] = "0123456789abcdef";
const char kUsernameChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";
const size_t kTokenLength = 64;
const size_t kUuidLength = 32;
#if !LCE_PLATFORM_SWITCH
// Cafe nlibcurl uses an NSSL verification bitmask in place of standard curl's TLS options.
const CURLoption kNsslVerifyOptions = static_cast<CURLoption>(211);
const long kVerifyPeerHostnameAndDate = 7;
#endif
const size_t kMaxResponse = 256 * 1024;
const size_t kMaxSkinResponse = 512 * 1024;

enum class Request { None, Start, Poll, Restore, Presence, Api };

struct ApiRequest
{
    std::string url;
    std::string body;
    bool post;
    std::function<void(long, const std::string&)> complete;
};
std::deque<ApiRequest> pendingApi;
ApiRequest activeApi;

#if LCE_PLATFORM_SWITCH
SwitchLiveWorker thread;
#else
alignas(32) OSThread thread;
alignas(32) unsigned char stack[128 * 1024];
#endif
Request request = Request::None;
Status status = Status::Idle;
bool restartAfterRequest = false;
bool restoreForMenu = false;
std::string deviceCode;
std::string userCode;
std::string username;
std::string playerUuid;
C4JCriticalSection identityLock;
bool apiSignedIn = false;
std::string sessionToken;
std::string savedUuid;
#if !LCE_PLATFORM_SWITCH
FSClient saveClient;
FSCmdBlock saveCommand;
unsigned char saveSlot;
#endif
bool saveClientReady = false;
bool sessionNeedsSave = false;
long long saveRetryAt = 0;
std::string response;
long responseStatus = 0;
CURLcode curlResult = CURLE_OK;
int failedOption = -1;
bool workerFailed = false;
std::wstring transportError;
long long expiresAt = 0;
long long pollAfter = 0;
long long pollInterval = 0;
long long presenceAfter = 0;
long long presenceRetryAt = 0;
bool requestedPlaying = false;
bool reportedPlaying = false;
unsigned int requestedMiniGame = 0;
unsigned int reportedMiniGame = 0;

#if LCE_PLATFORM_SWITCH
std::string SessionPath()
{
    return PcSaveRoot() + "/" + kSessionFile;
}
#endif

void ReadSavedSession()
{
    if (!saveClientReady)
        return;
    // One fixed-size record with a hex token and a zero-padded optional UUID.
    alignas(64) uint8_t buffer[kTokenLength + kUuidLength + 1];
#if LCE_PLATFORM_SWITCH
    FILE* file = std::fopen(SessionPath().c_str(), "rb");
    if (file == NULL)
        return;
    const int count = static_cast<int>(std::fread(buffer, 1, sizeof(buffer), file));
    std::fclose(file);
#else
    FSFileHandle handle;
    gProfileInstance->EnterInitSaveDirCS();
    int count = SAVEOpenFile(&saveClient, &saveCommand, saveSlot, kSessionFile, "r",
                             &handle, FS_ERROR_FLAG_ALL);
    if (count == FS_STATUS_OK)
    {
        count = FSReadFile(&saveClient, &saveCommand, buffer, 1, sizeof(buffer), handle, 0, FS_ERROR_FLAG_ALL);
        FSCloseFile(&saveClient, &saveCommand, handle, FS_ERROR_FLAG_ALL);
    }
    gProfileInstance->LeaveInitSaveDirCS();
#endif
    if (count != static_cast<int>(kTokenLength + kUuidLength))
        return;
    const char* bytes = reinterpret_cast<const char*>(buffer);
    const std::string token(bytes, kTokenLength);
    const std::string uuid(bytes + kTokenLength, kUuidLength);
    if (token.find_first_not_of(kLowerHex) != std::string::npos)
        return;
    sessionToken = token;
    if (uuid.find_first_not_of(kLowerHex) == std::string::npos)
        savedUuid = uuid;
}

bool WriteSavedSession()
{
    if (!saveClientReady)
        return false;
    alignas(64) uint8_t buffer[kTokenLength + kUuidLength] = {};
    std::memcpy(buffer, sessionToken.data(), kTokenLength);
    std::memcpy(buffer + kTokenLength, savedUuid.data(), savedUuid.size());
#if LCE_PLATFORM_SWITCH
    const std::string path = SessionPath();
    const std::string partial = path + ".tmp";
    FILE* file = std::fopen(partial.c_str(), "wb");
    if (file == NULL)
        return false;
    const bool written = std::fwrite(buffer, 1, sizeof(buffer), file) == sizeof(buffer);
    const bool closed = std::fclose(file) == 0;
    if (written && closed && std::rename(partial.c_str(), path.c_str()) == 0)
        return true;
    std::remove(partial.c_str());
    return false;
#else
    FSFileHandle handle;
    bool saved = false;
    gProfileInstance->EnterInitSaveDirCS();
    if (SAVEOpenFile(&saveClient, &saveCommand, saveSlot, kSessionFile, "w", &handle, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
    {
        const int count = FSWriteFile(&saveClient, &saveCommand, buffer, 1, sizeof(buffer), handle, 0, FS_ERROR_FLAG_ALL);
        const int closed = FSCloseFile(&saveClient, &saveCommand, handle, FS_ERROR_FLAG_ALL);
        saved = count == static_cast<int>(sizeof(buffer)) && closed == FS_STATUS_OK
            && SAVEFlushQuota(&saveClient, &saveCommand, saveSlot, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
        if (!saved)
            SAVERollbackQuota(&saveClient, &saveCommand, saveSlot, FS_ERROR_FLAG_ALL);
    }
    gProfileInstance->LeaveInitSaveDirCS();
    return saved;
#endif
}

void ClearSession()
{
    sessionNeedsSave = false;
    sessionToken.clear();
    savedUuid.clear();
    EnterCriticalSection(&identityLock);
    username.clear();
    apiSignedIn = false;
    LeaveCriticalSection(&identityLock);
    if (!saveClientReady)
        return;
#if LCE_PLATFORM_SWITCH
    std::remove(SessionPath().c_str());
#else
    gProfileInstance->EnterInitSaveDirCS();
    if (SAVERemove(&saveClient, &saveCommand, saveSlot, kSessionFile, FS_ERROR_FLAG_ALL) == FS_STATUS_OK)
        SAVEFlushQuota(&saveClient, &saveCommand, saveSlot, FS_ERROR_FLAG_ALL);
    gProfileInstance->LeaveInitSaveDirCS();
#endif
}

size_t WriteResponse(char* bytes, size_t size, size_t count, void* destination)
{
    std::string& output = *static_cast<std::string*>(destination);
    const bool cosmetic = request == Request::Api && !activeApi.post
        && activeApi.url.compare(0, sizeof(kApiUrl) - 1, kApiUrl) == 0
        && activeApi.url.size() >= 5 && (activeApi.url.compare(activeApi.url.size() - 5, 5, "/skin") == 0
                                        || activeApi.url.compare(activeApi.url.size() - 5, 5, "/cape") == 0);
    const size_t limit = cosmetic ? kMaxSkinResponse : kMaxResponse;
    if (size == 0 || count > (limit - output.size()) / size)
        return 0;
    const size_t length = size * count;
    output.append(bytes, length);
    return length;
}

#if LCE_PLATFORM_SWITCH
CURLcode SetNativeTrustRoots(CURL*, void* context, void*)
{
    static const unsigned char rootX1[] = {
#include "wiiu_live_root_ca.inc"
    };
    static const unsigned char rootX2[] = {
#include "wiiu_live_root_x2_ca.inc"
    };
    auto* ssl = static_cast<SslContext*>(context);
    const Result x1 = sslContextImportServerPki(ssl, rootX1, sizeof(rootX1), SslCertificateFormat_Der, nullptr);
    const Result x2 = sslContextImportServerPki(ssl, rootX2, sizeof(rootX2), SslCertificateFormat_Der, nullptr);
    return R_SUCCEEDED(x1) && R_SUCCEEDED(x2) ? CURLE_OK : CURLE_SSL_CACERT_BADFILE;
}
#else
bool IsNativeConsole()
{
    // Cemu implements nlibcurl through its host TLS stack, but not this IOS device.
    const IOSHandle service = IOS_Open("/dev/nsec/nssl", IOS_OPEN_READWRITE);
    if (service < 0) return false;
    IOS_Close(service);
    return true;
}

int SendConsoleRequest(const char* url, bool post, const std::string& body,
                       const std::string& authorization)
{
    const std::string prefix = "https://api.lcereborn.xyz";
    const std::string target(url);
    if (target.compare(0, prefix.size(), prefix) != 0 || target.size() <= prefix.size()
        || target[prefix.size()] != '/' || target.find_first_of("\r\n") != std::string::npos
        || authorization.find_first_of("\r\n") != std::string::npos)
    {
        curlResult = CURLE_URL_MALFORMAT;
        return 0;
    }
    // IOS-CRYPTO's GenerateRand (0x15) supplies fresh entropy before sign-in,
    // when no session token exists yet. Never seed TLS from clocks or identifiers.
    alignas(64) unsigned char random[32] = {};
    const IOSHandle crypto = IOS_Open("/dev/crypto", IOS_OPEN_READWRITE);
    if (crypto < 0)
    {
        transportError = L"Could not open the console's secure random service.";
        curlResult = CURLE_FAILED_INIT;
        return 0;
    }
    const IOSError generated = IOS_Ioctl(crypto, 0x15, NULL, 0, random, sizeof(random));
    IOS_Close(crypto);
    if (generated < 0)
    {
        transportError = L"Could not obtain secure random data from the console.";
        curlResult = CURLE_FAILED_INIT;
        return 0;
    }
    std::string seed(reinterpret_cast<const char*>(random), sizeof(random));
    std::memset(random, 0, sizeof(random));
    struct Deadline : lcenet::CancelFlag
    {
        long long until;
        Deadline() : until(lcenet::NowMs() + 15000) {}
        bool Cancelled() const override { return lcenet::NowMs() >= until; }
    } deadline;
    lcenet::Address address;
    std::string error;
    lcenet::TcpSocket socket;
    if (!lcenet::Resolve("api.lcereborn.xyz:443", address, error)
        || !socket.Connect(address, 5000, deadline, error))
    {
        curlResult = CURLE_COULDNT_CONNECT;
        return 0;
    }
    const bool connected = socket.EnableRelayTls("api.lcereborn.xyz", seed, 8000, deadline);
    std::fill(seed.begin(), seed.end(), 0);
    if (!connected)
    {
        transportError = L"Secure connection failed. Check the console date and time.";
        curlResult = CURLE_SSL_CONNECT_ERROR;
        return 0;
    }
    std::string message = std::string(post ? "POST " : "GET ") + target.substr(prefix.size())
        + " HTTP/1.1\r\nHost: api.lcereborn.xyz\r\nConnection: close\r\n"
          "Accept-Encoding: identity\r\nContent-Type: application/json\r\n";
    if (!authorization.empty()) message += authorization + "\r\n";
    if (post) message += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    message += "\r\n";
    if (post) message += body;
    if (!socket.SendAll(message.data(), message.size()))
    {
        curlResult = CURLE_SEND_ERROR;
        return 0;
    }
    std::string wire;
    char buffer[8192];
    while (!deadline.Cancelled())
    {
        const int count = socket.Receive(buffer, sizeof(buffer), 100);
        if (count < 0) { curlResult = CURLE_RECV_ERROR; return 0; }
        if (!count) continue;
        wire.append(buffer, static_cast<size_t>(count));
        const auto parsed = lcenet::ParseHttpResponse(wire, kMaxSkinResponse, responseStatus, response);
        if (parsed == lcenet::HttpParse::Complete) { curlResult = CURLE_OK; return 0; }
        if (parsed == lcenet::HttpParse::Invalid) { curlResult = CURLE_WEIRD_SERVER_REPLY; return 0; }
    }
    curlResult = CURLE_OPERATION_TIMEDOUT;
    return 0;
}

#endif

template <typename T>
bool SetOption(CURL* curl, CURLoption option, T value)
{
    curlResult = curl_easy_setopt(curl, option, value);
    if (curlResult == CURLE_OK)
        return true;
    failedOption = static_cast<int>(option);
    return false;
}

int SendRequest(int, const char**)
{
    static bool curlReady = false;
    if (!curlReady)
    {
        curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curlResult != CURLE_OK)
            return 0;
        curlReady = true;
    }

    // Reuse one handle for this worker. Cemu 2.6's cleanup reads the guest
    // handle after freeing it, and presence requests run for the whole session.
    static CURL* curl = NULL;
    if (curl == NULL)
        curl = curl_easy_init();
    if (curl == NULL)
    {
        curlResult = CURLE_FAILED_INIT;
        return 0;
    }
#if !LCE_PLATFORM_SWITCH
    static const bool nativeConsole = IsNativeConsole();
#endif
    const char* url = kStartUrl;
    static std::string body;
    static std::string authorization;
    static char contentType[] = "Content-Type: application/json";
    // nlibcurl borrows the list. Keep it in worker-owned storage and avoid
    // Cemu 2.6's allocation callbacks in curl_slist_append.
    static curl_slist headers = {contentType, NULL};
    static curl_slist bearer = {NULL, NULL};
    body = "{}";
    bool post = true;
    headers.next = NULL;
    if (request == Request::Poll)
    {
        url = kTokenUrl;
        body = "{\"device_code\":\"" + deviceCode + "\"}";
    }
    else if (request == Request::Restore)
    {
        url = kMeUrl;
        post = false;
    }
    else if (request == Request::Api)
    {
        url = activeApi.url.c_str();
        body = activeApi.body;
        post = activeApi.post;
    }
    else if (request == Request::Presence)
    {
        url = kPresenceUrl;
#if LCE_PLATFORM_SWITCH
        static const char* const activities[] = {"world", "battle", "tumble", "glide", "build"};
        if (!requestedPlaying)
            body = "{\"status\":\"launcher\",\"platform\":\"switch\"}";
        else if (requestedMiniGame == 0)
            body = "{\"status\":\"game\",\"activity\":\"world\",\"platform\":\"switch\"}";
        else
            body = std::string("{\"status\":\"minigame\",\"platform\":\"switch\",\"activity\":\"")
                + activities[requestedMiniGame] + "\"}";
#else
        body = requestedPlaying ? "{\"status\":\"game\",\"activity\":\"world\",\"platform\":\"wiiu\"}"
                                : "{\"status\":\"launcher\",\"platform\":\"wiiu\"}";
#endif
    }
    if (request == Request::Api || request == Request::Presence || request == Request::Restore)
    {
        authorization = "Authorization: Bearer " + sessionToken;
        bearer.data = &authorization[0];
        headers.next = &bearer;
    }
    else
    {
        authorization.clear();
        bearer.data = NULL;
    }
#if !LCE_PLATFORM_SWITCH
    if (nativeConsole)
        return SendConsoleRequest(url, post, body, authorization);
#endif
    if (SetOption(curl, CURLOPT_URL, url)
        && SetOption(curl, CURLOPT_HTTPHEADER, &headers)
        && (post ? SetOption(curl, CURLOPT_POST, 1L) && SetOption(curl, CURLOPT_POSTFIELDS, body.c_str())
                 : SetOption(curl, CURLOPT_HTTPGET, 1L))
        && SetOption(curl, CURLOPT_TIMEOUT, 10L)
        && SetOption(curl, CURLOPT_CONNECTTIMEOUT, 5L)
        && SetOption(curl, CURLOPT_FOLLOWLOCATION, 0L)
#if LCE_PLATFORM_SWITCH
        && SetOption(curl, CURLOPT_SSL_VERIFYPEER, 1L)
        && SetOption(curl, CURLOPT_SSL_VERIFYHOST, 2L)
        && SetOption(curl, CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2))
        && SetOption(curl, CURLOPT_SSL_CTX_FUNCTION, SetNativeTrustRoots)
#else
        && SetOption(curl, kNsslVerifyOptions, kVerifyPeerHostnameAndDate)
#endif
        && SetOption(curl, CURLOPT_WRITEFUNCTION, WriteResponse)
        && SetOption(curl, CURLOPT_WRITEDATA, &response))
    {
        curlResult = curl_easy_perform(curl);
        if (curlResult == CURLE_OK)
            curlResult = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseStatus);
    }
    return 0;
}

bool Send(Request next)
{
    response.clear();
    responseStatus = 0;
    curlResult = CURLE_OK;
    failedOption = -1;
    workerFailed = false;
    transportError.clear();
    request = next;
#if LCE_PLATFORM_SWITCH
    if (!thread.Start([] { SendRequest(0, nullptr); }))
#else
    if (!OSCreateThread(&thread, SendRequest, 0, NULL, stack + sizeof(stack), sizeof(stack), 16, 0))
#endif
    {
        workerFailed = true;
        request = Request::None;
        return false;
    }
#if !LCE_PLATFORM_SWITCH
    OSResumeThread(&thread);
#endif
    return true;
}

size_t Field(const char* name)
{
    const std::string key = std::string("\"") + name + "\"";
    size_t position = response.find(key);
    if (position == std::string::npos)
        return position;
    position = response.find_first_not_of(" \t\r\n", position + key.size());
    if (position == std::string::npos || response[position] != ':')
        return std::string::npos;
    return response.find_first_not_of(" \t\r\n", position + 1);
}

std::string StringField(const char* name, const char* alphabet, size_t minLength, size_t maxLength)
{
    const size_t position = Field(name);
    if (position == std::string::npos || response[position] != '"')
        return std::string();
    const size_t start = position + 1;
    const size_t end = response.find('"', start);
    if (end == std::string::npos || end - start < minLength || end - start > maxLength)
        return std::string();
    const std::string value = response.substr(start, end - start);
    return value.find_first_not_of(alphabet) == std::string::npos ? value : std::string();
}

int SecondsField(const char* name)
{
    size_t position = Field(name);
    int seconds = 0;
    while (position < response.size() && response[position] >= '0' && response[position] <= '9')
    {
        seconds = seconds * 10 + response[position++] - '0';
        if (seconds > 3600)
            return 0;
    }
    return seconds;
}

Status SignedIn(const std::string& name)
{
    EnterCriticalSection(&identityLock);
    username = name;
    playerUuid = savedUuid;
    apiSignedIn = true;
    LeaveCriticalSection(&identityLock);
    presenceAfter = 0;
    presenceRetryAt = 0;
    return Status::SignedIn;
}

Status Finish(Request finished, long long now)
{
    if (finished == Request::Restore)
    {
        if (responseStatus == 401)
        {
            ClearSession();
            restartAfterRequest = restoreForMenu;
            return Status::Idle;
        }
        if (responseStatus == 200)
        {
            const std::string name = StringField("username", kUsernameChars, 3, 20);
            if (!name.empty())
                return SignedIn(name);
        }
        return Status::Failed;
    }
    if (finished == Request::Start && responseStatus == 201)
    {
        deviceCode = StringField("device_code", kLowerHex, kTokenLength, kTokenLength);
        userCode = StringField("user_code", "0123456789ABCDEF", 12, 12);
        const int expiresIn = SecondsField("expires_in");
        const int interval = SecondsField("interval");
        if (!deviceCode.empty() && !userCode.empty() && expiresIn > 0 && interval > 0)
        {
            expiresAt = now + expiresIn * 1000;
            pollInterval = interval * 1000;
            pollAfter = now + pollInterval;
            return Status::Waiting;
        }
    }
    if (finished == Request::Poll)
    {
        if (responseStatus == 202 || responseStatus == 0 || responseStatus == 429 || responseStatus >= 500)
        {
            pollAfter = now + (responseStatus == 429 ? 2 * pollInterval : pollInterval);
            return Status::Waiting;
        }
        if (responseStatus == 403)
            return Status::Denied;
        if (responseStatus == 410)
            return Status::Expired;
        if (responseStatus == 201)
        {
            const std::string token = StringField("token", kLowerHex, kTokenLength, kTokenLength);
            const std::string name = StringField("username", kUsernameChars, 3, 20);
            if (!token.empty() && !name.empty())
            {
                sessionToken = token;
                savedUuid = StringField("player_uuid", kLowerHex, kUuidLength, kUuidLength);
                sessionNeedsSave = !WriteSavedSession();
                saveRetryAt = now + 10000;
                return SignedIn(name);
            }
        }
    }
    return Status::Failed;
}
}

void Init() { InitializeCriticalSection(&identityLock); }

void RestoreSavedSession()
{
#if LCE_PLATFORM_SWITCH
    saveClientReady = true;
#else
    saveSlot = nn::act::GetSlotNo();
    FSInitCmdBlock(&saveCommand);
    saveClientReady = FSAddClient(&saveClient, FS_ERROR_FLAG_ALL) == FS_STATUS_OK;
#endif
    ReadSavedSession();
    if (!sessionToken.empty())
        status = Send(Request::Restore) ? Status::Restoring : Status::Failed;
}

void Start()
{
    Update();
    if (status == Status::SignedIn || status == Status::Starting || status == Status::Waiting)
        return;
    if (!sessionToken.empty())
    {
        restoreForMenu = true;
        if (status != Status::Restoring)
            status = Send(Request::Restore) ? Status::Restoring : Status::Failed;
        return;
    }
    if (request != Request::None)
    {
        restartAfterRequest = true;
        status = Status::Starting;
        return;
    }
    status = Send(Request::Start) ? Status::Starting : Status::Failed;
}

void Cancel()
{
    restartAfterRequest = false;
    restoreForMenu = false;
    if (status == Status::Starting || status == Status::Waiting)
        status = Status::Idle;
}

Status Update()
{
    const long long now = System::processTimeInMilliSecs();
#if LCE_PLATFORM_SWITCH
    if (request != Request::None && thread.Finished())
    {
        thread.Join();
#else
    if (request != Request::None && OSIsThreadTerminated(&thread))
    {
        OSJoinThread(&thread, NULL);
#endif
        const Request finished = request;
        request = Request::None;
        if ((finished == Request::Presence || finished == Request::Api) && responseStatus == 401)
        {
            ClearSession();
            status = Status::Idle;
        }
        if (finished == Request::Api)
        {
            activeApi.complete(responseStatus, response);
            activeApi = ApiRequest();
        }
        else if (finished == Request::Presence)
        {
            if (responseStatus == 200)
            {
                reportedPlaying = requestedPlaying;
                reportedMiniGame = requestedMiniGame;
                presenceAfter = now + 45000;
            }
            else
            {
                presenceRetryAt = now + 10000;
            }
        }
        else if (finished == Request::Restore)
        {
            status = Finish(finished, now);
            restoreForMenu = false;
        }
        else if (!restartAfterRequest && (status == Status::Starting || status == Status::Waiting))
            status = Finish(finished, now);
        response.clear();
        if (status != Status::Waiting)
        {
            deviceCode.clear();
            userCode.clear();
        }
    }
    if (status != Status::SignedIn)
    {
        while (!pendingApi.empty())
        {
            auto complete = pendingApi.front().complete;
            pendingApi.pop_front();
            complete(401, std::string());
        }
    }
    if (restartAfterRequest && request == Request::None)
    {
        restartAfterRequest = false;
        status = Send(Request::Start) ? Status::Starting : Status::Failed;
    }
    if (status == Status::Waiting && request == Request::None)
    {
        if (now >= expiresAt)
            status = Status::Expired;
        else if (now >= pollAfter && !Send(Request::Poll))
            status = Status::Failed;
    }
    return status;
}

void TickPresence(bool playing, unsigned int miniGame)
{
    const Status current = Update();
    if (current != Status::SignedIn && !playing)
    {
        EnterCriticalSection(&identityLock);
        playerUuid.clear();
        LeaveCriticalSection(&identityLock);
    }
    if (current != Status::SignedIn || request != Request::None)
        return;
    const long long now = System::processTimeInMilliSecs();
    if (sessionNeedsSave && now >= saveRetryAt)
    {
        sessionNeedsSave = !WriteSavedSession();
        saveRetryAt = now + 10000;
    }
    const unsigned int activity = playing && miniGame <= 4 ? miniGame : 0;
    if (now >= presenceRetryAt && (playing != reportedPlaying || activity != reportedMiniGame || now >= presenceAfter))
    {
        requestedPlaying = playing;
        requestedMiniGame = activity;
        if (Send(Request::Presence))
            return;
        presenceRetryAt = now + 10000;
    }
    if (!pendingApi.empty())
    {
        activeApi = std::move(pendingApi.front());
        pendingApi.pop_front();
        if (!Send(Request::Api))
        {
            activeApi.complete(0, std::string());
            activeApi = ApiRequest();
        }
    }
}

bool IsSignedIn()
{
    EnterCriticalSection(&identityLock);
    const bool signedIn = apiSignedIn;
    LeaveCriticalSection(&identityLock);
    return signedIn;
}

#if LCE_PLATFORM_SWITCH
void Shutdown()
{
    thread.Join();
}

std::string AccountName()
{
    EnterCriticalSection(&identityLock);
    const std::string name = username;
    LeaveCriticalSection(&identityLock);
    return name;
}
#endif

std::string PlayerUuid()
{
    EnterCriticalSection(&identityLock);
    std::string uuid = playerUuid;
    LeaveCriticalSection(&identityLock);
    return uuid;
}

bool RequestApi(const char* path, const char* body,
                const std::function<void(long, const std::string&)>& complete)
{
    if (!IsSignedIn() || path == NULL || *path != '/' || !complete || pendingApi.size() >= 12)
        return false;
    ApiRequest entry;
    entry.url = std::string(kApiUrl) + path;
    entry.post = body != NULL;
    if (entry.post)
        entry.body = body;
    entry.complete = complete;
    pendingApi.push_back(std::move(entry));
    return true;
}

std::string RelaySeed(const std::string& nonce)
{
    return sessionToken.empty() || nonce.empty() ? std::string() : lcenet::RelayTlsSeed(sessionToken, nonce);
}

const std::string& UserCode() { return userCode; }
const std::string& Username() { return username; }

std::wstring ErrorDetail()
{
    if (workerFailed)
        return L"Could not start the network worker.";
    if (!transportError.empty())
        return transportError;
    wchar_t detail[80];
    if (failedOption >= 0)
        std::swprintf(detail, 80, L"Network error %d (option %d).", static_cast<int>(curlResult), failedOption);
    else if (curlResult != CURLE_OK)
        std::swprintf(detail, 80, L"Network error %d.", static_cast<int>(curlResult));
    else if (responseStatus == 201)
        return L"The server returned an invalid sign-in response.";
    else
        std::swprintf(detail, 80, L"Server response HTTP %ld.", responseStatus);
    return detail;
}
}
