#include "platform/wiiu/WiiULiveSignIn.hpp"
#include "game/core/System.hpp"
#include <coreinit/thread.h>
#include <curl/curl.h>
#include <cwchar>

namespace WiiULiveSignIn
{
namespace
{
const char kStartUrl[] = "https://api.lcereborn.xyz/v1/device/start";
const char kTokenUrl[] = "https://api.lcereborn.xyz/v1/device/token";
const char kPresenceUrl[] = "https://api.lcereborn.xyz/v1/presence";
// Cafe nlibcurl uses an NSSL verification bitmask in place of standard curl's TLS options.
const CURLoption kNsslVerifyOptions = static_cast<CURLoption>(211);
const long kVerifyPeerHostnameAndDate = 7;
const size_t kMaxResponse = 8192;

enum class Request { None, Start, Poll, Presence };

alignas(32) OSThread thread;
alignas(32) unsigned char stack[128 * 1024];
Request request = Request::None;
Status status = Status::Idle;
bool restartAfterRequest = false;
std::string deviceCode;
std::string userCode;
std::string username;
std::string sessionToken;
std::string response;
long responseStatus = 0;
CURLcode curlResult = CURLE_OK;
int failedOption = -1;
bool workerFailed = false;
long long expiresAt = 0;
long long pollAfter = 0;
long long pollInterval = 0;
long long presenceAfter = 0;
long long presenceRetryAt = 0;
bool requestedPlaying = false;
bool reportedPlaying = false;

size_t WriteResponse(char* bytes, size_t size, size_t count, void* destination)
{
    std::string& output = *static_cast<std::string*>(destination);
    if (size == 0 || count > (kMaxResponse - output.size()) / size)
        return 0;
    const size_t length = size * count;
    output.append(bytes, length);
    return length;
}

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
    const char* url = kStartUrl;
    static std::string body;
    static std::string authorization;
    static char contentType[] = "Content-Type: application/json";
    // nlibcurl borrows the list. Keep it in worker-owned storage and avoid
    // Cemu 2.6's allocation callbacks in curl_slist_append.
    static curl_slist headers = {contentType, NULL};
    static curl_slist bearer = {NULL, NULL};
    body = "{}";
    headers.next = NULL;
    if (request == Request::Poll)
    {
        url = kTokenUrl;
        body = "{\"device_code\":\"" + deviceCode + "\"}";
    }
    else if (request == Request::Presence)
    {
        url = kPresenceUrl;
        body = requestedPlaying ? "{\"status\":\"game\",\"activity\":\"world\",\"platform\":\"wiiu\"}"
                                : "{\"status\":\"launcher\",\"platform\":\"wiiu\"}";
        authorization = "Authorization: Bearer " + sessionToken;
        bearer.data = &authorization[0];
        headers.next = &bearer;
    }
    else
    {
        authorization.clear();
        bearer.data = NULL;
    }
    if (SetOption(curl, CURLOPT_URL, url)
        && SetOption(curl, CURLOPT_HTTPHEADER, &headers)
        && SetOption(curl, CURLOPT_POST, 1L)
        && SetOption(curl, CURLOPT_POSTFIELDS, body.c_str())
        && SetOption(curl, CURLOPT_TIMEOUT, 10L)
        && SetOption(curl, CURLOPT_CONNECTTIMEOUT, 5L)
        && SetOption(curl, CURLOPT_FOLLOWLOCATION, 0L)
        && SetOption(curl, kNsslVerifyOptions, kVerifyPeerHostnameAndDate)
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
    request = next;
    if (!OSCreateThread(&thread, SendRequest, 0, NULL, stack + sizeof(stack), sizeof(stack), 16, 0))
    {
        workerFailed = true;
        request = Request::None;
        return false;
    }
    OSResumeThread(&thread);
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

Status Finish(Request finished, long long now)
{
    const char lowerHex[] = "0123456789abcdef";
    if (finished == Request::Start && responseStatus == 201)
    {
        deviceCode = StringField("device_code", lowerHex, 64, 64);
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
            const std::string token = StringField("token", lowerHex, 64, 64);
            const std::string name = StringField("username", "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_", 3, 20);
            if (!token.empty() && !name.empty())
            {
                sessionToken = token;
                username = name;
                presenceAfter = 0;
                presenceRetryAt = 0;
                return Status::SignedIn;
            }
        }
    }
    return Status::Failed;
}
}

void Start()
{
    Update();
    if (!sessionToken.empty() || status == Status::Starting || status == Status::Waiting)
        return;
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
    if (status == Status::Starting || status == Status::Waiting)
        status = Status::Idle;
}

Status Update()
{
    const long long now = System::processTimeInMilliSecs();
    if (request != Request::None && OSIsThreadTerminated(&thread))
    {
        OSJoinThread(&thread, NULL);
        const Request finished = request;
        request = Request::None;
        if (finished == Request::Presence)
        {
            if (responseStatus == 401)
            {
                sessionToken.clear();
                username.clear();
                status = Status::Idle;
            }
            else if (responseStatus == 200)
            {
                reportedPlaying = requestedPlaying;
                presenceAfter = now + 45000;
            }
            else
            {
                presenceRetryAt = now + 10000;
            }
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

void TickPresence(bool playing)
{
    if (Update() != Status::SignedIn || request != Request::None)
        return;
    const long long now = System::processTimeInMilliSecs();
    if (now < presenceRetryAt || (playing == reportedPlaying && now < presenceAfter))
        return;
    requestedPlaying = playing;
    if (!Send(Request::Presence))
        presenceRetryAt = now + 10000;
}

const std::string& UserCode() { return userCode; }
const std::string& Username() { return username; }

std::wstring ErrorDetail()
{
    if (workerFailed)
        return L"Could not start the network worker.";
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
