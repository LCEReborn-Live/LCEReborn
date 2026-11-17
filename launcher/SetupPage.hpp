#ifndef LAUNCHER_SETUPPAGE_HPP
#define LAUNCHER_SETUPPAGE_HPP

#include <QWidget>

#include "GameLog.hpp"

class AssetPreparer;
class GameDownload;
class PlayButton;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QThread;
class QTimer;

class SetupPage : public QWidget
{
    Q_OBJECT

public:
    explicit SetupPage(QWidget* parent = nullptr);
    ~SetupPage() override;

    bool IsBusy() const { return mThread != nullptr || mDownloader != nullptr; }
    void SetCanGoBack(bool canGoBack);

signals:
    void Message(LogLevel level, const QString& message);
    void Imported();
    void BackRequested();

private:
    struct Step
    {
        QLineEdit* field;
        QPushButton* browse;
        QLabel* check;
        bool valid;
    };

    QWidget* MakeStep(int number, const QString& title, const QString& hint, Step* step, bool isUpdate);
    void Validate();
    void Start();
    void Download();
    void OnProgress(qint64 filesDone, qint64 filesTotal, qint64 bytesDone, qint64 bytesTotal);
    void OnFinished(bool succeeded, const QString& error);
    void SetBusy(bool busy);

    Step mBase;
    Step mUpdate;
    PlayButton* mImport;
    PlayButton* mDownload;
    QPushButton* mCancel;
    QPushButton* mBack;
    QProgressBar* mProgress;
    QLabel* mProgressText;
    QWidget* mLocalFields;
    QScrollArea* mScroll;
    QTimer* mValidateTimer;
    QThread* mThread = nullptr;
    AssetPreparer* mPreparer = nullptr;
    GameDownload* mDownloader = nullptr;
    bool mCanGoBack = false;
};

#endif
