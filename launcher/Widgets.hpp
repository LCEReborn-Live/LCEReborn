#ifndef LAUNCHER_WIDGETS_HPP
#define LAUNCHER_WIDGETS_HPP

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QVector>
#include <QWidget>

class QTimer;

namespace GameColours
{
extern const QColor kText;
extern const QColor kHighlight;
extern const QColor kDark;
extern const QColor kMuted;
extern const QColor kReady;
extern const QColor kWarning;
}

constexpr int kSlotIconSize = 36;

QFont GameFont(int pixelSize);
void DrawGameText(QPainter& painter, const QRect& rect, int flags, const QString& text, const QColor& colour,
                  bool shadow = true);

enum class FaceState
{
    Normal,
    Highlighted,
    Pressed,
    Disabled
};
void PaintButtonFace(QPainter& painter, const QRect& rect, FaceState state);
void PaintSlot(QPainter& painter, const QRect& rect);

class Panorama : public QWidget
{
    Q_OBJECT

public:
    explicit Panorama(QWidget* parent = nullptr);

    void Reload();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void Rescale();
    int Offset() const;
    void Tick();

    QImage mSource;
    QPixmap mScaled;
    QPixmap mFallback;
    QTimer* mTimer;
    QElapsedTimer mClock;
    int mPaintedOffset = -1;
};

class GamePanel : public QWidget
{
    Q_OBJECT

public:
    explicit GamePanel(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class GameWell : public QWidget
{
    Q_OBJECT

public:
    explicit GameWell(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class TabbedPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TabbedPanel(QWidget* parent = nullptr);

    void AddTab(const QString& text);
    void SetCurrent(int index);
    int Current() const { return mCurrent; }
    void SetMarked(int index, bool marked);
    void SetContent(QWidget* content);
    void SetCorner(QWidget* corner);
    QRect PanelRect() const;

signals:
    void CurrentChanged(int index);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect TabRect(int index) const;
    int TabAt(const QPoint& point) const;

    struct Tab
    {
        QString text;
        bool marked;
    };
    QVector<Tab> mTabs;
    int mCurrent = 0;
    int mHover = -1;
    QWidget* mContent = nullptr;
    QWidget* mCorner = nullptr;
};

class ShadowLabel : public QWidget
{
    Q_OBJECT

public:
    ShadowLabel(const QString& text, int pixelSize, QWidget* parent = nullptr);

    void SetText(const QString& text);
    void SetColour(const QColor& colour);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString mText;
    int mPixelSize;
    QColor mColour;
};

class GameButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit GameButton(const QString& text, QWidget* parent = nullptr);

    void SetPrimary(bool primary);
    void SetPixelSize(int pixelSize);
    void SetReadableFont(bool readable);
    void SetIcon(const QImage& icon, bool inSlot);
    void SetSubtitle(const QString& subtitle);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool mPrimary = false;
    bool mHover = false;
    bool mInSlot = false;
    bool mReadableFont = false;
    int mPixelSize = 18;
    QImage mIcon;
    QString mSubtitle;
};

class PlayButton : public GameButton
{
    Q_OBJECT

public:
    explicit PlayButton(QWidget* parent = nullptr);

    void SetLabels(const QString& title, const QString& detail);
};

class GameRowDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static const int kSubtitleRole = Qt::UserRole + 40;
    static const int kSlotRole = Qt::UserRole + 41;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

#endif
