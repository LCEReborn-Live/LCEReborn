#include "ProfilesTab.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ProfileDialog.hpp"
#include "Profiles.hpp"
#include "Skins.hpp"
#include "Widgets.hpp"

ProfilesTab::ProfilesTab(ProfileStore& store, QWidget* parent)
    : QWidget(parent), mStore(store), mList(new QListWidget(this)), mDelete(new QPushButton("Delete", this))
{
    mList->setObjectName("GameRows");
    mList->setItemDelegate(new GameRowDelegate(mList));
    mList->setFrameShape(QFrame::NoFrame);
    mList->setSelectionMode(QAbstractItemView::SingleSelection);
    mList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    mList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mList->setMouseTracking(true);

    auto* newButton = new QPushButton("New profile", this);
    auto* editButton = new QPushButton("Edit", this);
    auto* openButton = new QPushButton("Open game folder", this);
    auto* reimportButton = new QPushButton("Re-import game files", this);
    auto* browserJoins = new QPushButton("Enable browser joins", this);
    browserJoins->setToolTip("Allow lcereborn.xyz to open this launcher for game invitations and world joins.");
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(newButton);
    buttons->addWidget(editButton);
    buttons->addWidget(mDelete);
    buttons->addStretch(1);
    buttons->addWidget(openButton);
    buttons->addWidget(reimportButton);
    buttons->addWidget(browserJoins);

    auto* well = new GameWell(this);
    auto* wellLayout = new QVBoxLayout(well);
    wellLayout->setContentsMargins(10, 10, 10, 10);
    wellLayout->addWidget(mList);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addLayout(buttons);
    layout->addWidget(well, 1);

    connect(newButton, &QPushButton::clicked, this, &ProfilesTab::NewProfile);
    connect(editButton, &QPushButton::clicked, this, &ProfilesTab::EditProfile);
    connect(mDelete, &QPushButton::clicked, this, &ProfilesTab::DeleteProfile);
    connect(openButton, &QPushButton::clicked, this, &ProfilesTab::OpenGameFolder);
    connect(reimportButton, &QPushButton::clicked, this, &ProfilesTab::ReimportRequested);
    connect(browserJoins, &QPushButton::clicked, this, &ProfilesTab::BrowserJoinsRequested);
    connect(mList, &QListWidget::itemDoubleClicked, this, &ProfilesTab::EditProfile);
    connect(mList, &QListWidget::itemSelectionChanged, this, [this] {
        if (!mReloading && !CurrentId().isEmpty())
            mStore.Select(CurrentId());
    });
    connect(&mStore, &ProfileStore::Changed, this, &ProfilesTab::Reload);
    Reload();
}

void ProfilesTab::Reload()
{
    mReloading = true;
    mList->clear();
    for (const Profile& profile : mStore.Profiles())
    {
        auto* item = new QListWidgetItem(profile.name, mList);
        item->setData(Qt::UserRole, profile.id);
        item->setData(Qt::DecorationRole, Skins::FaceFor(profile, kSlotIconSize));
        item->setData(GameRowDelegate::kSlotRole, true);
        const QString folder = QDir::toNativeSeparators(ProfileStore::GameDirFor(profile));
        item->setData(GameRowDelegate::kSubtitleRole, QString("%1 - %2").arg(profile.username, folder));
        item->setToolTip(folder);
        if (profile.id == mStore.SelectedId())
            mList->setCurrentItem(item);
    }
    mDelete->setEnabled(mStore.Profiles().size() > 1);
    mReloading = false;
}

QString ProfilesTab::CurrentId() const
{
    const QList<QListWidgetItem*> selected = mList->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->data(Qt::UserRole).toString();
}

void ProfilesTab::NewProfile()
{
    ProfileDialog::Run(this, mStore, mStore.MakeNew(ProfileStore::DefaultUsername()), true);
}

void ProfilesTab::EditProfile()
{
    const Profile* profile = mStore.Find(CurrentId());
    if (profile != nullptr)
        ProfileDialog::Run(this, mStore, *profile, false);
}

void ProfilesTab::DeleteProfile()
{
    const Profile* profile = mStore.Find(CurrentId());
    if (profile == nullptr || mStore.Profiles().size() <= 1)
        return;
    const QString question = QString("Delete the profile %1? Its game folder and saves are kept.").arg(profile->name);
    if (QMessageBox::question(this, "Delete Profile", question) != QMessageBox::Yes)
        return;
    mStore.Remove(profile->id);
    QString error;
    if (!mStore.Save(&error))
        QMessageBox::warning(this, "Profiles", error);
}

void ProfilesTab::OpenGameFolder()
{
    const Profile* profile = mStore.Find(CurrentId());
    if (profile == nullptr)
        return;
    const QString folder = ProfileStore::GameDirFor(*profile);
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}
