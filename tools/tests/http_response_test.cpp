#include "net/HttpResponse.hpp"
#include <cassert>
#include <iostream>

int main()
{
    using namespace lcenet;
    long status = 0;
    std::string body;
    const std::string response = "HTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3;test=x\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
    for (size_t size = 0; size < response.size(); ++size)
        assert(ParseHttpResponse(response.substr(0, size), 16, status, body) == HttpParse::Incomplete);
    assert(ParseHttpResponse(response, 16, status, body) == HttpParse::Complete);
    assert(status == 201 && body == "abcde");
    assert(ParseHttpResponse("HTTP/1.1 401 Unauthorized\r\nContent-Length: 4\r\n\r\n" + std::string("a\0bc", 4),
                             16, status, body) == HttpParse::Complete);
    assert(status == 401 && body == std::string("a\0bc", 4));
    assert(ParseHttpResponse("HTTP/1.1 204 No Content\r\n\r\n", 16, status, body) == HttpParse::Complete);
    assert(status == 204 && body.empty());
    const char* invalid[] = {
        "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999999\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nffff\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na!!",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n-1\r\n",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\n\r\nclose-delimited",
    };
    for (const char* wire : invalid)
        assert(ParseHttpResponse(wire, 16, status, body) == HttpParse::Invalid);
    assert(ParseHttpResponse(response, 4, status, body) == HttpParse::Invalid);
    assert(ParseHttpResponse(std::string(16385, 'x'), 16, status, body) == HttpParse::Invalid);
    assert(ParseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Info: yes\r\n\r\n",
                             16, status, body) == HttpParse::Complete);
    std::cout << "PASS: fragmented HTTP, chunked JSON, binary bodies, bounds and truncation\n";
}
