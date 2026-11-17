#include "SetupPage.hpp"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "GameFiles.hpp"
#include "GameDownload.hpp"
#include "LauncherPaths.hpp"
#include "Widgets.hpp"

namespace
{
const char* const kCancelled = "Cancelled.";

void SetCheck(QLabel* label, const QString& text, const char* style)
{
    label->setText(text);
    label->setObjectName(style);
    label->style()->unpolish(label);
    label->style()->polish(label);
}
}

SetupPage::SetupPage(QWidget* parent)
    : QWidget(parent), mImport(new PlayButton(this)), mDownload(new PlayButton(this)), mCancel(new QPushButton("Cancel", this)),
      mBack(new QPushButton("Back to the launcher", this)), mProgress(new QProgressBar(this)),
      mProgressText(new QLabel(this)), mValidateTimer(new QTimer(this))
{
    auto* panel = new GamePanel(this);

    auto* brand = new QHBoxLayout;
    brand->setSpacing(14);
    auto* mark = new QLabel(panel);
    mark->setPixmap(QPixmap(":/launcher/mark.png").scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    mark->setFixedSize(56, 56);
    auto* wordmark = new QLabel("LCE Reborn", panel);
    wordmark->setObjectName("Heading");
    brand->addWidget(mark);
    brand->addWidget(wordmark, 1);

    auto* statement = new QLabel("Get ready to play.", panel);
    statement->setObjectName("SetupStatement");
    statement->setWordWrap(true);
    auto* explanation = new QLabel(
        "Download and install the game files automatically, or choose the extracted base game and Update 688 "
        "folders already on your computer. Downloads can resume if interrupted.",
        panel);
    explanation->setWordWrap(true);
    auto* packs = new QLabel(
        "DLC packs you own and custom .pck packs work too: once the game is set up, copy each pack's folder "
        "into the packs folder, which Packs on the game's main menu opens.",
        panel);
    packs->setWordWrap(true);

    auto* intro = new QVBoxLayout;
    intro->setSpacing(10);
    intro->addLayout(brand);
    intro->addSpacing(14);
    intro->addWidget(statement);
    intro->addWidget(explanation);
    intro->addWidget(packs);
    intro->addStretch(1);
    intro->addWidget(mBack, 0, Qt::AlignLeft);
    auto* introWidget = new QWidget(panel);
    introWidget->setLayout(intro);
    introWidget->setFixedWidth(360);

    mImport->SetLabels("Import game files", "Copies the content into the launcher");
    mImport->setFixedSize(340, 62);
    mDownload->SetLabels("Download & install", "Installs the game and Update 688");
    mDownload->setMinimumHeight(62);
    mProgress->setRange(0, 1000);
    mProgress->setTextVisible(false);
    mProgress->hide();
    mProgressText->setObjectName("StepHint");
    mProgressText->setWordWrap(true);
    mCancel->hide();

    auto* scroll = new QScrollArea(panel);
    mScroll = scroll;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* well = new GameWell(scroll);
    scroll->setWidget(well);
    auto* steps = new QVBoxLayout(well);
    steps->setContentsMargins(24, 22, 24, 22);
    steps->setSpacing(14);
    steps->addWidget(mDownload);
    mLocalFields = new QWidget(well);
    auto* localSteps = new QVBoxLayout(mLocalFields);
    localSteps->setContentsMargins(0, 0, 0, 0);
    localSteps->setSpacing(14);
    auto* localHint = new QLabel("Or import local game folders:", mLocalFields);
    localHint->setObjectName("StepHint");
    localSteps->addWidget(localHint);
    localSteps->addWidget(MakeStep(1, "Base game", "The extracted title folder of Minecraft: Wii U Edition, with code, "
                                              "content and meta inside.",
                              &mBase, false));
    localSteps->addWidget(MakeStep(2, "Update 688", "The extracted folder of the game's update, version 688.", &mUpdate,
                              true));
    steps->addWidget(mLocalFields);
    steps->addStretch(1);
    steps->addWidget(mProgress);
    steps->addWidget(mProgressText);
    auto* importRow = new QHBoxLayout;
    importRow->addWidget(mCancel, 0, Qt::AlignVCenter);
    importRow->addStretch(1);
    importRow->addWidget(mImport);
    steps->addLayout(importRow);

    auto* columns = new QHBoxLayout(panel);
    columns->setSpacing(24);
    columns->addWidget(introWidget);
    columns->addWidget(scroll, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(60, 40, 60, 40);
    layout->addWidget(panel);

    QSettings settings(LauncherPaths::SettingsFile(), QSettings::IniFormat);
    mBase.field->setText(settings.value("gameFiles/baseDump").toString());
    mUpdate.field->setText(settings.value("gameFiles/updateDump").toString());

    mValidateTimer->setSingleShot(true);
    mValidateTimer->setInterval(250);
    connect(mValidateTimer, &QTimer::timeout, this, &SetupPage::Validate);
    connect(mImport, &PlayButton::clicked, this, &SetupPage::Start);
    connect(mDownload, &PlayButton::clicked, this, &SetupPage::Download);
    connect(mCancel, &QPushButton::clicked, this, [this] {
        if (mPreparer != nullptr)
            mPreparer->Cancel();
        if (mDownloader != nullptr)
            mDownloader->Cancel();
        mCancel->setEnabled(false);
    });
    connect(mBack, &QPushButton::clicked, this, &SetupPage::BackRequested);
    SetCanGoBack(false);
    Validate();
}

SetupPage::~SetupPage()
{
    if (mDownloader != nullptr)
    {
        disconnect(mDownloader, nullptr, this, nullptr);
        delete mDownloader;
        mDownloader = nullptr;
    }
    if (mThread != nullptr)
    {
        mPreparer->Cancel();
        mThread->quit();
        mThread->wait();
    }
}

QWidget* SetupPage::MakeStep(int number, const QString& title, const QString& hint, Step* step, bool isUpdate)
{
    auto* widget = new QWidget(this);
    auto* column = new QVBoxLayout(widget);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    auto* titleLabel = new QLabel(QString("%1. %2").arg(number).arg(title), widget);
    titleLabel->setObjectName("StepTitle");
    auto* hintLabel = new QLabel(hint, widget);
    hintLabel->setObjectName("StepHint");
    hintLabel->setWordWrap(true);
    step->field = new QLineEdit(widget);
    step->field->setPlaceholderText(QDir::toNativeSeparators(isUpdate ? ".../Minecraft Wii U Edition [Update]"
                                                                      : ".../Minecraft Wii U Edition [Game]"));
    step->check = new QLabel(widget);
    step->check->setWordWrap(true);
    step->valid = false;
    auto* browse = new QPushButton("Browse...", widget);
    step->browse = browse;
    auto* fieldRow = new QHBoxLayout;
    fieldRow->setSpacing(8);
    fieldRow->addWidget(step->field, 1);
    fieldRow->addWidget(browse);
    column->addWidget(titleLabel);
    column->addWidget(hintLabel);
    column->addLayout(fieldRow);
    column->addWidget(step->check);

    connect(step->field, &QLineEdit::textChanged, mValidateTimer, qOverload<>(&QTimer::start));
    connect(browse, &QPushButton::clicked, this, [this, step, title] {
        const QString folder = QFileDialog::getExistingDirectory(this, "Choose the " + title.toLower() + " folder",
                                                                 step->field->text());
        if (!folder.isEmpty())
            step->field->setText(QDir::toNativeSeparators(folder));
    });
    return widget;
}

void SetupPage::Validate()
{
    for (Step* step : {&mBase, &mUpdate})
    {
        const bool isUpdate = step == &mUpdate;
        const QString path = QDir::fromNativeSeparators(step->field->text().trimmed());
        if (path.isEmpty())
        {
            step->valid = false;
            SetCheck(step->check, isUpdate ? "Choose the update's folder." : "Choose the base game's folder.",
                     "StepHint");
            continue;
        }
        int version = 0;
        QString error;
        step->valid = AssetPreparer::CheckDump(path, isUpdate, &version, &error);
        if (step->valid)
            SetCheck(step->check, QString("Found Minecraft: Wii U Edition, version %1").arg(version), "StepGood");
        else
            SetCheck(step->check, error, "StepBad");
    }
    mImport->setEnabled(!IsBusy() && mBase.valid && mUpdate.valid);
}

void SetupPage::SetCanGoBack(bool canGoBack)
{
    mCanGoBack = canGoBack;
    mBack->setVisible(canGoBack && !IsBusy());
}

void SetupPage::Start()
{
    Validate();
    if (!mBase.valid || !mUpdate.valid || IsBusy())
        return;
    QSettings settings(LauncherPaths::SettingsFile(), QSettings::IniFormat);
    settings.setValue("gameFiles/baseDump", mBase.field->text().trimmed());
    settings.setValue("gameFiles/updateDump", mUpdate.field->text().trimmed());

    mThread = new QThread(this);
    mPreparer = new AssetPreparer(QDir::fromNativeSeparators(mBase.field->text().trimmed()),
                                  QDir::fromNativeSeparators(mUpdate.field->text().trimmed()),
                                  LauncherPaths::AssetsDir());
    mPreparer->moveToThread(mThread);
    connect(mThread, &QThread::started, mPreparer, &AssetPreparer::Run);
    connect(mPreparer, &AssetPreparer::Progress, this, &SetupPage::OnProgress);
    connect(mPreparer, &AssetPreparer::Message, this, &SetupPage::Message);
    connect(mPreparer, &AssetPreparer::Finished, this, &SetupPage::OnFinished);
    connect(mThread, &QThread::finished, mPreparer, &QObject::deleteLater);

    SetBusy(true);
    mProgress->setValue(0);
    SetCheck(mProgressText, "Reading your dumps...", "StepHint");
    emit Message(LogLevel::Info, "Importing the game files.");
    mThread->start();
}

void SetupPage::SetBusy(bool busy)
{
    mBase.field->setEnabled(!busy);
    mUpdate.field->setEnabled(!busy);
    mBase.browse->setEnabled(!busy);
    mUpdate.browse->setEnabled(!busy);
    mImport->setVisible(!busy);
    mDownload->setVisible(!busy);
    mLocalFields->setVisible(!busy);
    mProgress->setVisible(busy);
    mCancel->setVisible(busy);
    mCancel->setEnabled(busy);
    mBack->setVisible(mCanGoBack && !busy);
}

void SetupPage::Download()
{
    if (IsBusy()) return;
    mDownloader = new GameDownload(this);
    connect(mDownloader, &GameDownload::Ready, this, [this](const QString& key, const QString& root) {
        (key == "base" ? mBase : mUpdate).field->setText(QDir::toNativeSeparators(root));
    });
    connect(mDownloader, &GameDownload::Progress, this, [this](const QString& text, qint64 done, qint64 total) {
        mProgress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) mProgress->setValue(static_cast<int>(done * 1000 / total));
        mProgressText->setText(text);
    });
    connect(mDownloader, &GameDownload::Finished, this, [this](bool succeeded, const QString& error) {
        mDownloader->deleteLater();
        mDownloader = nullptr;
        SetBusy(false);
        mProgress->setRange(0, 1000);
        Validate();
        if (succeeded) Start();
        else
        {
            SetCheck(mProgressText, error == kCancelled ? "Download cancelled. You can resume it later." : error,
                     error == kCancelled ? "StepHint" : "StepBad");
            emit Message(error == kCancelled ? LogLevel::Info : LogLevel::Error, mProgressText->text());
            QTimer::singleShot(0, this, [this] { mScroll->ensureWidgetVisible(mProgressText); });
        }
    });
    SetBusy(true);
    mDownloader->Start();
}

void SetupPage::OnProgress(qint64 filesDone, qint64 filesTotal, qint64 bytesDone, qint64 bytesTotal)
{
    mProgress->setValue(bytesTotal > 0 ? static_cast<int>(bytesDone * 1000 / bytesTotal) : 0);
    mProgressText->setText(QString("Copied %1 of %2 files, %3 of %4 MB")
                               .arg(filesDone)
                               .arg(filesTotal)
                               .arg(bytesDone >> 20)
                               .arg(bytesTotal >> 20));
}

void SetupPage::OnFinished(bool succeeded, const QString& error)
{
    mThread->quit();
    mThread->wait();
    mThread->deleteLater();
    mThread = nullptr;
    mPreparer = nullptr;
    SetBusy(false);
    Validate();
    if (succeeded)
    {
        mProgressText->setText(QString());
        emit Imported();
        return;
    }
    if (error == kCancelled)
        SetCheck(mProgressText, "Import cancelled.", "StepHint");
    else
        SetCheck(mProgressText, error, "StepBad");
    emit Message(error == kCancelled ? LogLevel::Warn : LogLevel::Error, "Importing the game files failed: " + error);
}
