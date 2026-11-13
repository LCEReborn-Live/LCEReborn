#include "../../launcher/LiveJoin.hpp"
#include <QCoreApplication>
#include <cstdio>
#include <utility>
#include <vector>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const std::vector<std::pair<QString, qint64>> cases = {
        {"lcereborn://join/1", 1}, {"lcereborn://join/123/", 123},
        {"LCEREBORN://JOIN/42", 42}, {"lcereborn://join/9007199254740991", 9007199254740991LL},
        {"", 0}, {"https://lcereborn.xyz/1", 0}, {"lcereborn://join/0", 0},
        {"lcereborn://join/-1", 0}, {"lcereborn://join/+1", 0}, {"lcereborn://join/01", 0},
        {"lcereborn://join/9007199254740992", 0}, {"lcereborn://join/999999999999999999999", 0},
        {"lcereborn://join/1?token=secret", 0}, {"lcereborn://join/1#token", 0},
        {"lcereborn://evil/1", 0}, {"lcereborn://join:8080/1", 0},
        {"lcereborn://user@join/1", 0}, {"lcereborn://join/%31", 0},
        {"lcereborn://join/1/extra", 0}, {"lcereborn://join/1\n", 0},
        {"lcereborn://join/1\r\n", 0}, {" lcereborn://join/1", 0},
        {"lcereborn://join/1\" --token secret", 0}
    };
    for (const auto& value : cases) {
        if (LiveJoin::OwnerIdFromUrl(value.first) != value.second) {
            std::fprintf(stderr, "Invalid join URL acceptance: %s\n", value.first.toUtf8().constData());
            return 1;
        }
    }
    std::printf("Live join URL validation passed (%zu cases).\n", cases.size());
    return 0;
}
