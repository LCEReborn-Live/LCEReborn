#include "Widgets.hpp"

#include <QEnterEvent>
#include <QFontDatabase>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTimer>
#include <QtMath>

#include "GameArt.hpp"

namespace GameColours
{
const QColor kText(239, 239, 239);
const QColor kHighlight(239, 239, 0);
const QColor kDark(64, 64, 64);
const QColor kMuted(96, 96, 96);
const QColor kReady(85, 255, 85);
const QColor kWarning(255, 170, 0);
}

namespace
{
const int kPanelOutline = 3;
const int kPanelBevel = 6;
const int kTabTop = 8;
const int kPanelTop = 55;
const int kRowHeight = 60;
const qint64 kPanoramaPixelsPerSecondAt720 = 14;

struct FaceColours
{
    QColor face;
    QColor top;
    QColor bottom;
    int topWidth;
    int bottomWidth;
};

FaceColours ColoursFor(FaceState state)
{
    switch (state)
    {
    case FaceState::Highlighted:
        return {QColor(145, 154, 202), QColor(199, 207, 255), QColor(113, 122, 173), 2, 4};
    case FaceState::Pressed:
        return {QColor(130, 139, 187), QColor(98, 106, 156), QColor(170, 179, 228), 4, 2};
    case FaceState::Disabled:
        return {QColor(92, 92, 92), QColor(120, 120, 120), QColor(72, 72, 72), 2, 4};
    default:
        return {QColor(131, 131, 131), QColor(182, 182, 182), QColor(107, 107, 107), 2, 4};
    }
}

const QPixmap& Grain(const QColor& face)
{
    static QHash<QRgb, QPixmap> cache;
    auto found = cache.find(face.rgb());
    if (found != cache.end())
        return *found;
    QImage image(64, 64, QImage::Format_RGB32);
    QRandomGenerator random(0x4c4345);
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const int shift = static_cast<int>(random.bounded(7)) - 3;
            image.setPixel(x, y, qRgb(qBound(0, face.red() + shift, 255), qBound(0, face.green() + shift, 255),
                                      qBound(0, face.blue() + shift, 255)));
        }
    }
    return *cache.insert(face.rgb(), QPixmap::fromImage(image));
}

QColor ShadowOf(const QColor& colour)
{
    return QColor(colour.red() / 4, colour.green() / 4, colour.blue() / 4, colour.alpha());
}

QString FallbackFamily()
{
    static const QString family = [] {
        const QStringList families = QFontDatabase::families();
        for (const char* candidate : {"Bahnschrift", "DIN Alternate", "Roboto Condensed", "Arial Narrow"})
        {
            if (families.contains(candidate))
                return QString(candidate);
        }
        return QFont().family();
    }();
    return family;
}

void PaintPanel(QPainter& painter, const QRect& rect)
{
    painter.fillRect(rect, Qt::black);
    const QRect inner = rect.adjusted(kPanelOutline, kPanelOutline, -kPanelOutline, -kPanelOutline);
    painter.fillRect(inner, QColor(208, 208, 208));
    painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), kPanelBevel), Qt::white);
    painter.fillRect(QRect(inner.left(), inner.top(), kPanelBevel, inner.height()), Qt::white);
    painter.fillRect(QRect(inner.left(), inner.bottom() - kPanelBevel + 1, inner.width(), kPanelBevel), QColor(106, 106, 106));
    painter.fillRect(QRect(inner.right() - kPanelBevel + 1, inner.top(), kPanelBevel, inner.height()), QColor(106, 106, 106));
}
}

QFont GameFont(int pixelSize)
{
    const QString family = GameArt::FontFamily();
    QFont font(family.isEmpty() ? FallbackFamily() : family);
    font.setPixelSize(pixelSize);
    if (!family.isEmpty())
    {
        font.setStyleStrategy(QFont::NoAntialias);
        font.setWeight(QFont::Normal);
    }
    else
    {
        font.setStyleStrategy(QFont::PreferDefault);
        font.setWeight(QFont::DemiBold);
    }
    return font;
}

void DrawGameText(QPainter& painter, const QRect& rect, int flags, const QString& text, const QColor& colour,
                  bool shadow)
{
    if (shadow)
    {
        const int offset = qMax(1, (painter.font().pixelSize() + 5) / 10);
        painter.setPen(ShadowOf(colour));
        painter.drawText(rect.translated(offset, offset), flags, text);
    }
    painter.setPen(colour);
    painter.drawText(rect, flags, text);
}

void PaintButtonFace(QPainter& painter, const QRect& rect, FaceState state)
{
    const FaceColours colours = ColoursFor(state);
    painter.fillRect(rect, Qt::black);
    const QRect inner = rect.adjusted(2, 2, -2, -2);
    painter.drawTiledPixmap(inner, Grain(colours.face));
    painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), colours.topWidth), colours.top);
    painter.fillRect(QRect(inner.left(), inner.bottom() - colours.bottomWidth + 1, inner.width(), colours.bottomWidth),
                     colours.bottom);
}

void PaintSlot(QPainter& painter, const QRect& rect)
{
    painter.fillRect(rect, QColor(139, 139, 139));
    painter.fillRect(QRect(rect.left(), rect.top(), rect.width(), 2), QColor(55, 55, 55));
    painter.fillRect(QRect(rect.left(), rect.top(), 2, rect.height()), QColor(55, 55, 55));
    painter.fillRect(QRect(rect.left() + 2, rect.bottom() - 1, rect.width() - 2, 2), Qt::white);
    painter.fillRect(QRect(rect.right() - 1, rect.top() + 2, 2, rect.height() - 2), Qt::white);
}

Panorama::Panorama(QWidget* parent) : QWidget(parent), mTimer(new QTimer(this))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    QImage fallback(64, 64, QImage::Format_RGB32);
    QRandomGenerator random(0x504e4f);
    for (int y = 0; y < fallback.height(); ++y)
    {
        for (int x = 0; x < fallback.width(); ++x)
        {
            const int shade = 34 + static_cast<int>(random.bounded(6));
            fallback.setPixel(x, y, qRgb(shade, shade, shade));
        }
    }
    mFallback = QPixmap::fromImage(fallback);
    mTimer->setInterval(33);
    connect(mTimer, &QTimer::timeout, this, &Panorama::Tick);
    mClock.start();
    Reload();
}

void Panorama::Reload()
{
    mSource = GameArt::Panorama();
    Rescale();
    update();
}

void Panorama::Rescale()
{
    if (mSource.isNull() || height() <= 0)
    {
        mScaled = QPixmap();
        mTimer->stop();
        return;
    }
    const int scaledWidth = mSource.width() * height() / mSource.height();
    mScaled = QPixmap::fromImage(mSource.scaled(scaledWidth, height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    if (isVisible())
        mTimer->start();
}

int Panorama::Offset() const
{
    const qint64 travelled = mClock.elapsed() * kPanoramaPixelsPerSecondAt720 * height() / 720 / 1000;
    return static_cast<int>(travelled % mScaled.width());
}

void Panorama::Tick()
{
    if (mScaled.isNull() || Offset() == mPaintedOffset)
        return;
    QRegion exposed(rect());
    for (QWidget* child : findChildren<QWidget*>())
    {
        if (!child->isVisible())
            continue;
        QRect cover;
        if (const auto* tabs = qobject_cast<TabbedPanel*>(child))
            cover = tabs->PanelRect();
        else if (qobject_cast<GamePanel*>(child) != nullptr || qobject_cast<GameButton*>(child) != nullptr)
            cover = child->rect();
        else
            continue;
        exposed -= QRect(child->mapTo(this, cover.topLeft()), cover.size());
    }
    update(exposed);
}

void Panorama::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (mScaled.isNull())
    {
        painter.drawTiledPixmap(rect(), mFallback);
        return;
    }
    mPaintedOffset = Offset();
    painter.drawTiledPixmap(rect(), mScaled, QPoint(mPaintedOffset, 0));
}

void Panorama::resizeEvent(QResizeEvent*)
{
    Rescale();
}

void Panorama::showEvent(QShowEvent*)
{
    if (!mScaled.isNull())
        mTimer->start();
}

void Panorama::hideEvent(QHideEvent*)
{
    mTimer->stop();
}

GamePanel::GamePanel(QWidget* parent) : QWidget(parent)
{
    setContentsMargins(kPanelOutline + kPanelBevel + 6, kPanelOutline + kPanelBevel + 6, kPanelOutline + kPanelBevel + 6,
                       kPanelOutline + kPanelBevel + 6);
}

void GamePanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    PaintPanel(painter, rect());
}

GameWell::GameWell(QWidget* parent) : QWidget(parent)
{
    setContentsMargins(8, 8, 8, 8);
}

void GameWell::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRect area = rect();
    painter.fillRect(area, QColor(157, 157, 157));
    painter.fillRect(QRect(area.left(), area.top(), area.width(), 4), QColor(75, 75, 75));
    painter.fillRect(QRect(area.left(), area.top(), 4, area.height()), QColor(75, 75, 75));
    painter.fillRect(QRect(area.left() + 4, area.bottom() - 3, area.width() - 4, 4), Qt::white);
    painter.fillRect(QRect(area.right() - 3, area.top() + 4, 4, area.height() - 4), Qt::white);
}

TabbedPanel::TabbedPanel(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
}

void TabbedPanel::AddTab(const QString& text)
{
    mTabs.append({text, false});
    update();
}

void TabbedPanel::SetCurrent(int index)
{
    if (index < 0 || index >= mTabs.size() || index == mCurrent)
        return;
    mCurrent = index;
    update();
    emit CurrentChanged(index);
}

void TabbedPanel::SetMarked(int index, bool marked)
{
    if (index < 0 || index >= mTabs.size() || mTabs[index].marked == marked)
        return;
    mTabs[index].marked = marked;
    update();
}

void TabbedPanel::SetContent(QWidget* content)
{
    mContent = content;
    content->setParent(this);
    resizeEvent(nullptr);
}

void TabbedPanel::SetCorner(QWidget* corner)
{
    mCorner = corner;
    corner->setParent(this);
    resizeEvent(nullptr);
}

QRect TabbedPanel::PanelRect() const
{
    return QRect(0, kPanelTop, width(), height() - kPanelTop);
}

QRect TabbedPanel::TabRect(int index) const
{
    const QFontMetrics metrics(GameFont(20));
    int x = 0;
    for (int tab = 0; tab < mTabs.size(); ++tab)
    {
        const int width = qMax(190, metrics.horizontalAdvance(mTabs[tab].text) + 64);
        if (tab == index)
            return tab == mCurrent ? QRect(x, 0, width, kPanelTop + kPanelOutline + kPanelBevel)
                                   : QRect(x, kTabTop, width, kPanelTop - kTabTop);
        x += width - kPanelOutline;
    }
    return QRect();
}

int TabbedPanel::TabAt(const QPoint& point) const
{
    for (int index = 0; index < mTabs.size(); ++index)
    {
        if (TabRect(index).contains(point))
            return index;
    }
    return -1;
}

void TabbedPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    PaintPanel(painter, PanelRect());
    painter.setFont(GameFont(20));

    for (int pass = 0; pass < 2; ++pass)
    {
        for (int index = 0; index < mTabs.size(); ++index)
        {
            const bool selected = index == mCurrent;
            if (selected != (pass == 1))
                continue;
            const QRect tab = TabRect(index);
            const QRect inner = tab.adjusted(kPanelOutline, kPanelOutline, -kPanelOutline, 0);
            painter.fillRect(QRect(tab.left(), tab.top(), tab.width(), kPanelOutline), Qt::black);
            painter.fillRect(QRect(tab.left(), tab.top(), kPanelOutline, tab.height()), Qt::black);
            painter.fillRect(QRect(tab.right() - kPanelOutline + 1, tab.top(), kPanelOutline, tab.height()), Qt::black);
            const bool hovered = !selected && index == mHover;
            painter.fillRect(inner, selected ? QColor(208, 208, 208) : hovered ? QColor(188, 188, 188) : QColor(171, 171, 171));
            const QColor light = selected ? QColor(255, 255, 255) : QColor(217, 217, 217);
            const QColor dark = selected ? QColor(106, 106, 106) : QColor(105, 105, 105);
            painter.fillRect(QRect(inner.left(), inner.top(), inner.width(), kPanelBevel), light);
            painter.fillRect(QRect(inner.left(), inner.top(), kPanelBevel, inner.height()), light);
            const int darkBottom = selected ? kPanelTop - tab.top() : inner.height();
            painter.fillRect(QRect(inner.right() - kPanelBevel + 1, inner.top() + kPanelBevel, kPanelBevel,
                                   darkBottom - kPanelBevel),
                             dark);
            const QRect label(tab.left(), tab.top(), tab.width(), kPanelTop - tab.top());
            DrawGameText(painter, label, Qt::AlignCenter, mTabs[index].text, GameColours::kDark, false);
            if (mTabs[index].marked)
            {
                const QRect mark(tab.right() - 22, label.center().y() - 5, 10, 10);
                painter.fillRect(mark, Qt::black);
                painter.fillRect(mark.adjusted(2, 2, -2, -2), GameColours::kReady);
            }
        }
    }
}

bool TabbedPanel::event(QEvent* event)
{
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::FontChange
        || event->type() == QEvent::Show)
    {
        resizeEvent(nullptr);
        update();
    }
    return QWidget::event(event);
}

void TabbedPanel::resizeEvent(QResizeEvent*)
{
    const int inset = kPanelOutline + kPanelBevel + 6;
    if (mContent != nullptr)
        mContent->setGeometry(PanelRect().adjusted(inset, inset, -inset, -inset));
    if (mCorner != nullptr)
    {
        const QSize hint = mCorner->sizeHint();
        const int tabsRight = mTabs.isEmpty() ? 0 : TabRect(mTabs.size() - 1).right() + 16;
        const int left = qMax(tabsRight, width() - hint.width() - 4);
        mCorner->setGeometry(left, (kPanelTop - hint.height()) / 2 + 2, qMax(0, width() - 4 - left), hint.height());
    }
}

void TabbedPanel::mousePressEvent(QMouseEvent* event)
{
    const int index = TabAt(event->position().toPoint());
    if (index >= 0)
        SetCurrent(index);
}

void TabbedPanel::mouseMoveEvent(QMouseEvent* event)
{
    const int index = TabAt(event->position().toPoint());
    setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (index != mHover)
    {
        mHover = index;
        update();
    }
}

void TabbedPanel::leaveEvent(QEvent*)
{
    mHover = -1;
    update();
}

ShadowLabel::ShadowLabel(const QString& text, int pixelSize, QWidget* parent)
    : QWidget(parent), mText(text), mPixelSize(pixelSize), mColour(GameColours::kText)
{
}

void ShadowLabel::SetText(const QString& text)
{
    mText = text;
    updateGeometry();
    update();
}

void ShadowLabel::SetColour(const QColor& colour)
{
    mColour = colour;
    update();
}

QSize ShadowLabel::sizeHint() const
{
    const QFontMetricsF metrics(GameFont(mPixelSize));
    return QSize(qCeil(metrics.horizontalAdvance(mText)) + 4, qCeil(metrics.height()) + 2);
}

QSize ShadowLabel::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

void ShadowLabel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setFont(GameFont(mPixelSize));
    const QString text = painter.fontMetrics().elidedText(mText, Qt::ElideRight, width() - 4);
    DrawGameText(painter, rect().adjusted(0, 0, -2, -2), Qt::AlignLeft | Qt::AlignVCenter, text, mColour);
}

GameButton::GameButton(const QString& text, QWidget* parent) : QAbstractButton(parent)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void GameButton::SetPrimary(bool primary)
{
    mPrimary = primary;
    update();
}

void GameButton::SetPixelSize(int pixelSize)
{
    mPixelSize = pixelSize;
    updateGeometry();
    update();
}

void GameButton::SetReadableFont(bool readable)
{
    mReadableFont = readable;
    updateGeometry();
    update();
}

static QFont ButtonFont(int size, bool readable)
{
    if (!readable) return GameFont(size);
    QFont font("Segoe UI");
    font.setPixelSize(size);
    font.setWeight(QFont::DemiBold);
    return font;
}

void GameButton::SetIcon(const QImage& icon, bool inSlot)
{
    mIcon = icon;
    mInSlot = inSlot;
    updateGeometry();
    update();
}

void GameButton::SetSubtitle(const QString& subtitle)
{
    mSubtitle = subtitle;
    updateGeometry();
    update();
}

QSize GameButton::sizeHint() const
{
    const QFontMetrics metrics(ButtonFont(mPixelSize, mReadableFont));
    const int height = mSubtitle.isEmpty() ? qMax(40, metrics.height() + 18) : 62;
    const int icon = mIcon.isNull() ? 0 : height - 14;
    return QSize(metrics.horizontalAdvance(text()) + icon + 44, height);
}

void GameButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    FaceState state = FaceState::Normal;
    if (!isEnabled())
        state = FaceState::Disabled;
    else if (isDown())
        state = FaceState::Pressed;
    else if (mPrimary || mHover || hasFocus())
        state = FaceState::Highlighted;
    PaintButtonFace(painter, rect(), state);

    const QColor ink = state == FaceState::Disabled ? QColor(160, 160, 160)
                       : state == FaceState::Normal  ? GameColours::kText
                                                     : GameColours::kHighlight;
    const int press = state == FaceState::Pressed ? 1 : 0;
    QRect content = rect().adjusted(12, 2 + press, -12, -4 + press);

    int iconRight = content.left();
    if (!mIcon.isNull())
    {
        if (mInSlot)
        {
            const int slotSize = kSlotIconSize + 8;
            const QRect slot(content.left() - 4, (height() - slotSize) / 2 - 1, slotSize, slotSize);
            PaintSlot(painter, slot);
            painter.drawImage(slot.adjusted(4, 4, -4, -4), mIcon);
            iconRight = slot.right() + 14;
        }
        else
        {
            const int iconSize = height() - 20;
            const QRect target(content.left(), (height() - iconSize) / 2 - 1 + press, iconSize, iconSize);
            painter.drawImage(target, mIcon);
            iconRight = target.right() + 12;
        }
    }

    if (!mSubtitle.isEmpty())
    {
        const QRect lines(iconRight, content.top(), content.right() - iconRight, content.height());
        painter.setFont(ButtonFont(mPixelSize, mReadableFont));
        const QRect title(lines.left(), lines.top() + 6, lines.width(), lines.height() / 2);
        DrawGameText(painter, title, Qt::AlignLeft | Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(text(), Qt::ElideRight, lines.width()), ink,
                     state != FaceState::Disabled);
        painter.setFont(ButtonFont(mPixelSize - 4, mReadableFont));
        const QRect subtitle(lines.left(), title.bottom(), lines.width(), lines.height() / 2 - 6);
        DrawGameText(painter, subtitle, Qt::AlignLeft | Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(mSubtitle, Qt::ElideRight, lines.width()),
                     state == FaceState::Normal ? QColor(214, 214, 214) : ink.darker(110), state != FaceState::Disabled);
        return;
    }

    painter.setFont(ButtonFont(mPixelSize, mReadableFont));
    const QRect label = mIcon.isNull() ? content : QRect(iconRight, content.top(), content.right() - iconRight, content.height());
    DrawGameText(painter, label, (mIcon.isNull() ? Qt::AlignHCenter : Qt::AlignLeft) | Qt::AlignVCenter,
                 painter.fontMetrics().elidedText(text(), Qt::ElideRight, label.width()), ink,
                 state != FaceState::Disabled);
}

void GameButton::enterEvent(QEnterEvent*)
{
    mHover = true;
    update();
}

void GameButton::leaveEvent(QEvent*)
{
    mHover = false;
    update();
}

PlayButton::PlayButton(QWidget* parent) : GameButton("Play", parent)
{
    SetPrimary(true);
    SetPixelSize(24);
}

void PlayButton::SetLabels(const QString& title, const QString& detail)
{
    setText(title);
    setToolTip(detail);
    update();
}

void GameRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = (index.flags() & Qt::ItemIsEnabled) != 0;
    const QRect row = option.rect;
    PaintButtonFace(*painter, row, !enabled ? FaceState::Disabled : selected ? FaceState::Highlighted : FaceState::Normal);
    const QColor ink = !enabled ? QColor(160, 160, 160)
                       : selected || hovered ? GameColours::kHighlight
                                             : GameColours::kText;

    int left = row.left() + 12;
    const QVariant decoration = index.data(Qt::DecorationRole);
    const QImage icon = decoration.canConvert<QImage>() ? decoration.value<QImage>() : decoration.value<QPixmap>().toImage();
    if (!icon.isNull())
    {
        if (index.data(kSlotRole).toBool())
        {
            const int slotSize = kSlotIconSize + 8;
            const QRect slot(left, row.top() + (row.height() - slotSize) / 2 - 1, slotSize, slotSize);
            PaintSlot(*painter, slot);
            painter->drawImage(slot.adjusted(4, 4, -4, -4), icon);
            left = slot.right() + 16;
        }
        else
        {
            const QRect target(left, row.top() + (row.height() - 40) / 2 - 1, 40, 40);
            painter->drawImage(target, icon);
            left = target.right() + 16;
        }
    }

    const QRect text(left, row.top() + 2, row.right() - left - 12, row.height() - 6);
    const QString subtitle = index.data(kSubtitleRole).toString();
    painter->setFont(GameFont(18));
    if (subtitle.isEmpty())
    {
        DrawGameText(*painter, text, Qt::AlignLeft | Qt::AlignVCenter,
                     painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width()),
                     ink, enabled);
    }
    else
    {
        const QRect title(text.left(), text.top() + 6, text.width(), text.height() / 2);
        DrawGameText(*painter, title, Qt::AlignLeft | Qt::AlignVCenter,
                     painter->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width()),
                     ink, enabled);
        painter->setFont(GameFont(14));
        const QRect second(text.left(), title.bottom(), text.width(), text.height() / 2 - 6);
        DrawGameText(*painter, second, Qt::AlignLeft | Qt::AlignVCenter,
                     painter->fontMetrics().elidedText(subtitle, Qt::ElideRight, text.width()),
                     selected ? ink.darker(110) : QColor(214, 214, 214), enabled);
    }
    painter->restore();
}

QSize GameRowDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const
{
    return QSize(option.rect.width(), kRowHeight);
}
