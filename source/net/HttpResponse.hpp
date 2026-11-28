#ifndef NET_HTTPRESPONSE_HPP
#define NET_HTTPRESPONSE_HPP

#include <string>
#include <stddef.h>

namespace lcenet
{
enum class HttpParse { Incomplete, Complete, Invalid };

inline std::string HttpLower(std::string value)
{
    for (char& c : value)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}

inline std::string HttpTrim(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return std::string();
    return value.substr(first, value.find_last_not_of(" \t") - first + 1);
}

inline bool HttpNumber(const std::string& text, unsigned int base, size_t maximum, size_t& value)
{
    value = 0;
    if (text.empty()) return false;
    for (char c : text)
    {
        const unsigned int digit = c >= '0' && c <= '9' ? c - '0'
            : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
        if (digit >= base || digit > maximum || value > (maximum - digit) / base) return false;
        value = value * base + digit;
    }
    return true;
}

// Responses must be framed: a TLS disconnect must never turn a truncated body into success.
inline HttpParse ParseHttpResponse(const std::string& wire, size_t limit, long& status, std::string& body)
{
    const size_t end = wire.find("\r\n\r\n");
    if (end == std::string::npos)
        return wire.size() > 16384 ? HttpParse::Invalid : HttpParse::Incomplete;
    if (end > 16384 || wire.size() > limit + 65536) return HttpParse::Invalid;
    const size_t first = wire.find("\r\n");
    if (first < 12 || (wire.compare(0, 9, "HTTP/1.1 ") && wire.compare(0, 9, "HTTP/1.0 "))
        || (first > 12 && wire[12] != ' ')) return HttpParse::Invalid;
    size_t code;
    if (!HttpNumber(wire.substr(9, 3), 10, 599, code) || code < 200) return HttpParse::Invalid;
    bool hasLength = false, chunked = false;
    size_t length = 0;
    for (size_t pos = first + 2; pos < end;)
    {
        const size_t next = wire.find("\r\n", pos);
        const size_t colon = wire.find(':', pos);
        if (next == std::string::npos || colon == std::string::npos || colon >= next) return HttpParse::Invalid;
        const std::string name = HttpLower(wire.substr(pos, colon - pos));
        const std::string value = HttpLower(HttpTrim(wire.substr(colon + 1, next - colon - 1)));
        if (name == "content-length")
        {
            if (hasLength || !HttpNumber(value, 10, limit, length)) return HttpParse::Invalid;
            hasLength = true;
        }
        else if (name == "transfer-encoding")
        {
            if (chunked || value != "chunked") return HttpParse::Invalid;
            chunked = true;
        }
        else if (name == "content-encoding" && value != "identity") return HttpParse::Invalid;
        pos = next + 2;
    }
    if (hasLength && chunked) return HttpParse::Invalid;
    size_t pos = end + 4;
    std::string decoded;
    if (code == 204 || code == 304)
    {
        status = static_cast<long>(code);
        body.clear();
        return HttpParse::Complete;
    }
    if (hasLength)
    {
        if (wire.size() - pos < length) return HttpParse::Incomplete;
        decoded.assign(wire, pos, length);
    }
    else if (chunked)
    {
        for (;;)
        {
            const size_t line = wire.find("\r\n", pos);
            if (line == std::string::npos)
                return wire.size() - pos > 1024 ? HttpParse::Invalid : HttpParse::Incomplete;
            if (line - pos > 1024) return HttpParse::Invalid;
            const size_t extension = wire.find(';', pos);
            const size_t numberEnd = extension < line ? extension : line;
            size_t chunk;
            if (!HttpNumber(wire.substr(pos, numberEnd - pos), 16, limit - decoded.size(), chunk))
                return HttpParse::Invalid;
            pos = line + 2;
            if (!chunk)
            {
                if (wire.size() - pos < 2) return HttpParse::Incomplete;
                if (wire.compare(pos, 2, "\r\n") != 0 && wire.find("\r\n\r\n", pos) == std::string::npos)
                    return wire.size() - pos > 16384 ? HttpParse::Invalid : HttpParse::Incomplete;
                break;
            }
            if (wire.size() - pos < chunk + 2) return HttpParse::Incomplete;
            if (wire.compare(pos + chunk, 2, "\r\n") != 0) return HttpParse::Invalid;
            decoded.append(wire, pos, chunk);
            pos += chunk + 2;
        }
    }
    else return HttpParse::Invalid;
    status = static_cast<long>(code);
    body.swap(decoded);
    return HttpParse::Complete;
}
}
#endif
