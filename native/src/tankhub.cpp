#include "tankhub.h"
#include "fishtank.h"

#include <QAction>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QList>
#include <QMenu>
#include <QQmlFile>

namespace {
// All tanks in this process. Plain pointers guarded by QPointer, touched only
// from the GUI thread.
QList<QPointer<FishTank>> &tanks()
{
    static QList<QPointer<FishTank>> list;
    return list;
}
} // namespace

TankHub::TankHub(QObject *parent)
    : QObject(parent)
{
    // Tanks come and go with wallpaper changes; numbers change every tick.
    // A slow poll is plenty for a widget and costs nothing measurable.
    m_refresh.setInterval(2000);
    connect(&m_refresh, &QTimer::timeout, this, &TankHub::stateChanged);
    m_refresh.start();
}

void TankHub::registerTank(FishTank *tank)
{
    tanks().removeAll(nullptr);
    if (!tanks().contains(tank))
        tanks().append(tank);
}

void TankHub::unregisterTank(FishTank *tank)
{
    tanks().removeAll(tank);
    tanks().removeAll(nullptr);
}

bool TankHub::isLockScreen()
{
    static const bool lockScreen = [] {
        if (qEnvironmentVariableIsSet("DESKTOCEAN_LOCKSCREEN"))
            return qEnvironmentVariableIntValue("DESKTOCEAN_LOCKSCREEN") == 1;
        return QFileInfo(QCoreApplication::applicationFilePath()).fileName().startsWith(QLatin1String("kscreenlocker"))
            || QCoreApplication::applicationName().startsWith(QLatin1String("kscreenlocker"));
    }();
    return lockScreen;
}

bool TankHub::isPrimary(const FishTank *tank)
{
    // The lock screen only looks: plasmashell's tank records the babies
    if (isLockScreen())
        return false;
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            return t == tank;
    }
    return false;
}

FishTank *TankHub::first() const
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            return t;
    }
    return nullptr;
}

bool TankHub::available() const
{
    return first() != nullptr;
}

bool TankHub::paused() const
{
    const FishTank *t = first();
    return t && t->paused();
}

void TankHub::setPaused(bool paused)
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            t->setPaused(paused);
    }
    Q_EMIT stateChanged();
}

int TankHub::fishCount() const
{
    const FishTank *t = first();
    return t ? t->fishCount() : 0;
}

int TankHub::foodCount() const
{
    const FishTank *t = first();
    return t ? t->foodCount() : 0;
}

qreal TankHub::hunger() const
{
    const FishTank *t = first();
    return t ? t->averageHunger() : 0;
}

int TankHub::friendships() const
{
    const FishTank *t = first();
    return t ? t->relationCounts().first : 0;
}

int TankHub::rivalries() const
{
    const FishTank *t = first();
    return t ? t->relationCounts().second : 0;
}

qreal TankHub::daylight() const
{
    const FishTank *t = first();
    return t ? t->daylight() : 1;
}

void TankHub::feed()
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            t->feed(-1);
    }
    Q_EMIT stateChanged();
}

void TankHub::clearFood()
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            t->clearFood();
    }
    Q_EMIT stateChanged();
}

void TankHub::makeSubmenu(QObject *action, const QList<QObject *> &children, const QUrl &icon)
{
    auto *parent = qobject_cast<QAction *>(action);
    if (!parent)
        return;
    auto *menu = new QMenu;
    for (QObject *child : children) {
        if (auto *a = qobject_cast<QAction *>(child))
            menu->addAction(a);
    }
    parent->setMenu(menu);
    // The menu has no QObject parent (QAction doesn't own it); tie their lives together
    connect(parent, &QObject::destroyed, menu, &QObject::deleteLater);
    if (!icon.isEmpty())
        setMonochromeIcon(parent, icon);
}

namespace {
// Paints an SVG's shape in the palette's text colour at draw time, so the icon
// matches the theme's symbolic icons and follows light/dark switches
class MonochromeIconEngine : public QIconEngine
{
public:
    explicit MonochromeIconEngine(const QString &file)
        : m_file(file)
        , m_shape(file)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        QPixmap pm = scaledPixmap(rect.size(), mode, state, dpr);
        painter->drawPixmap(rect, pm);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm = m_shape.pixmap(size, scale, QIcon::Normal, state);
        if (pm.isNull())
            return pm;
        const QPalette pal = QGuiApplication::palette();
        QColor colour = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
                                  mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::WindowText);
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), colour);
        return pm;
    }

    QIconEngine *clone() const override { return new MonochromeIconEngine(m_file); }
    QString key() const override { return QStringLiteral("desktocean-monochrome"); }

private:
    QString m_file;
    QIcon m_shape;
};
} // namespace

void TankHub::setMonochromeIcon(QObject *action, const QUrl &svg)
{
    if (auto *a = qobject_cast<QAction *>(action))
        a->setIcon(QIcon(new MonochromeIconEngine(QQmlFile::urlToLocalFileOrQrc(svg))));
}

bool TankHub::openEditor(const QString &page)
{
    return FishTank::launchEditor(page);
}
