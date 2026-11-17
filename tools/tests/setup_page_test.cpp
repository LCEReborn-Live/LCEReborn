#include "../../launcher/SetupPage.hpp"
#include "../../launcher/Theme.hpp"
#include "../../launcher/LauncherPaths.hpp"
#include "../../launcher/Widgets.hpp"
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <cstdio>

static QString testData;
namespace LauncherPaths
{
QString InstallDir() { return QCoreApplication::applicationDirPath(); }
QString ResourcesDir() { return InstallDir() + "/resources"; }
QString DataDir() { return testData; }
QString SettingsFile() { return testData + "/launcher.ini"; }
QString AssetsDir() { return testData + "/assets"; }
QString ContentCheckFile() { return AssetsDir() + "/Common/Media/MediaWiiU.arc"; }
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
#if defined(Q_OS_WIN)
    // The offscreen plugin has no Windows font database of its own.
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/bahnschrift.ttf");
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf");
#endif
    if (argc != 2) return 2;
    testData = QDir(QString::fromLocal8Bit(argv[1])).canonicalPath();
    if (!testData.endsWith("/build/game-download-check")) return 2;
    ApplyTheme(app);
    SetupPage page;
    page.resize(1280, 720);
    page.show();
    QTimer::singleShot(100, &app, [&] {
        if (page.IsBusy() || page.findChildren<PlayButton*>().size() != 2 || page.findChildren<QLineEdit*>().size() != 2)
        { app.exit(1); return; }
        for (const QSize size : {QSize(1280, 720), QSize(960, 600)})
        {
            page.resize(size);
            app.processEvents();
            const QString name = QString("/setup-%1x%2.png").arg(size.width()).arg(size.height());
            if (!page.grab().save(testData + name)) { app.exit(1); return; }
            std::printf("Setup requested %dx%d; actual %dx%d; minimum %dx%d.\n", size.width(), size.height(),
                        page.width(), page.height(), page.minimumSizeHint().width(), page.minimumSizeHint().height());
            if (page.size() != size) { app.exit(1); return; }
        }
        PlayButton* install = nullptr;
        QPushButton* cancel = nullptr;
        for (PlayButton* button : page.findChildren<PlayButton*>())
            if (button->text() == "Download & install") install = button;
        for (QPushButton* button : page.findChildren<QPushButton*>())
            if (button->text() == "Cancel") cancel = button;
        if (!install || !cancel) { app.exit(1); return; }
        install->click();
        app.processEvents();
        if (!page.IsBusy() || !cancel->isVisible()) { app.exit(1); return; }
        for (QLineEdit* field : page.findChildren<QLineEdit*>())
            if (field->isVisible()) { app.exit(1); return; }
        const QRect cancelBounds(cancel->mapTo(&page, QPoint()), cancel->size());
        if (!page.rect().contains(cancelBounds)) { app.exit(1); return; }
        cancel->click();
        QTimer::singleShot(300, &app, [&] {
            if (page.IsBusy()) { app.exit(1); return; }
            std::printf("First-run layout, automatic/manual controls and cancellation: PASS.\n");
            app.exit(0);
        });
    });
    return app.exec();
}
