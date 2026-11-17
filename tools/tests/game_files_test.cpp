#include "../../launcher/GameDownload.hpp"
#include "../../launcher/GameFiles.hpp"
#include "../../launcher/LauncherPaths.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

// Keep integration-test data outside the user's installed launcher and saves.
static QString testData;
namespace LauncherPaths
{
QString InstallDir() { return QCoreApplication::applicationDirPath(); }
QString ResourcesDir() { return InstallDir() + "/resources"; }
QString DataDir() { return testData; }
}

static QByteArray Hash(const QString& file)
{
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&input)) return QByteArray();
    return hash.result();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) { std::fprintf(stderr, "Pass the isolated game-download-check directory.\n"); return 2; }
    testData = QDir(QString::fromLocal8Bit(argv[1])).canonicalPath();
    if (testData.isEmpty() || !testData.endsWith("/build/game-download-check")) return 2;
    QTemporaryDir destination(testData + "/import-check-XXXXXX");
    if (!destination.isValid()) return 2;
    QMap<QString, QString> roots;
    auto* download = new GameDownload(&app);
    QObject::connect(download, &GameDownload::Ready, &app, [&](const QString& key, const QString& root) {
        roots[key] = root;
    });
    QObject::connect(download, &GameDownload::Finished, &app, [&](bool succeeded, const QString& error) {
        if (!succeeded || roots.size() != 2)
        {
            std::fprintf(stderr, "Download adapter failed: %s\n", error.toUtf8().constData());
            app.exit(1);
            return;
        }
        AssetPreparer preparer(roots["base"], roots["update"], destination.path() + "/assets");
        QObject::connect(&preparer, &AssetPreparer::Finished, &app, [&](bool imported, const QString& importError) {
            if (!imported)
            {
                std::fprintf(stderr, "Import failed: %s\n", importError.toUtf8().constData());
                app.exit(1);
                return;
            }
            QMap<QString, QString> expected;
            for (const QString& key : {QString("base"), QString("update")})
            {
                const QDir content(roots[key] + "/content");
                for (const QString& top : {QString("Common"), QString("music"), QString("WiiU")})
                {
                    QDirIterator files(content.filePath(top), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                                       QDirIterator::Subdirectories);
                    while (files.hasNext())
                    {
                        const QString file = files.next();
                        expected[content.relativeFilePath(file)] = file;
                    }
                }
            }
            for (auto entry = expected.cbegin(); entry != expected.cend(); ++entry)
            {
                const QByteArray source = Hash(entry.value());
                if (source.isEmpty() || source != Hash(destination.path() + "/assets/" + entry.key()))
                {
                    std::fprintf(stderr, "Imported file differs: %s\n", entry.key().toUtf8().constData());
                    app.exit(1);
                    return;
                }
            }
            QFile manifest(destination.path() + "/assets/.prepared.json");
            if (!manifest.open(QIODevice::ReadOnly)) { app.exit(1); return; }
            const QJsonObject record = QJsonDocument::fromJson(manifest.readAll()).object();
            if (record.value("base_version").toInt() != 1 || record.value("update_version").toInt() != 688)
            { app.exit(1); return; }
            std::printf("Download process adapter + original importer: PASS; %lld merged files verified by SHA-256.\n",
                        static_cast<long long>(expected.size()));
            app.exit(0);
        });
        preparer.Run();
    });
    QTimer::singleShot(180000, &app, [&] { download->Cancel(); app.exit(3); });
    QTimer::singleShot(0, download, &GameDownload::Start);
    return app.exec();
}
