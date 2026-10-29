#include "tankhub.h"
#include "fishtank.h"

#include <KGlobalAccel>

#include <QAction>
#include <algorithm>
#include <QCursor>
#include <QQuickItem>
#include <QQuickWindow>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QHash>
#include <QList>
#include <QMenu>
#include <QQmlFile>
#include <QQuickWindow>
#include <QScreen>

#include <algorithm>

namespace {
// All tanks in this process. Plain pointers guarded by QPointer, touched only
// from the GUI thread.
QList<QPointer<FishTank>> &tanks()
{
    static QList<QPointer<FishTank>> list;
    return list;
}

// Which tank each fish lives in
QHash<quint64, QPointer<FishTank>> &owners()
{
    static QHash<quint64, QPointer<FishTank>> map;
    return map;
}

// The screen a tank covers, in global coordinates (works on Wayland too,
// where windows don't know their own position)
QRect screenOf(const FishTank *tank)
{
    const QQuickWindow *w = tank ? tank->window() : nullptr;
    return w && w->screen() ? w->screen()->geometry() : QRect();
}

int load(const FishTank *tank)
{
    int n = 0;
    for (const QPointer<FishTank> &t : std::as_const(owners()))
        n += t == tank;
    return n;
}

void reapplyAll()
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            t->applyAssignment(false);
    }
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
    setUpShortcut();
    tanks().removeAll(nullptr);
    if (tanks().contains(tank))
        return;
    tanks().append(tank);
    // A new screen: hand it a fair share of the fish already swimming
    const int live = int(tanks().size());
    if (live > 1 && !owners().isEmpty()) {
        const int share = int(owners().size()) / live;
        int moved = 0;
        for (auto it = owners().begin(); it != owners().end() && moved < share; ++it) {
            if (it.value() && load(it.value()) > share) {
                it.value() = tank;
                ++moved;
            }
        }
        reapplyAll();
    }
    updateNeighbours();
}

void TankHub::unregisterTank(FishTank *tank)
{
    tanks().removeAll(tank);
    tanks().removeAll(nullptr);
    // Its fish move to the other screens
    bool orphans = false;
    for (auto it = owners().begin(); it != owners().end();) {
        if (!it.value() || it.value() == tank) {
            it = owners().erase(it);
            orphans = true;
        } else {
            ++it;
        }
    }
    if (orphans)
        reapplyAll();
    updateNeighbours();
}

bool TankHub::owns(FishTank *tank, quint64 id)
{
    auto it = owners().find(id);
    if (it != owners().end() && it.value())
        return it.value() == tank;
    // Nobody has it: the tank with the fewest fish takes it
    FishTank *best = tank;
    int bestLoad = load(tank);
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t && load(t) < bestLoad) {
            best = t;
            bestLoad = load(t);
        }
    }
    owners().insert(id, best);
    return best == tank;
}

void TankHub::assign(quint64 id, FishTank *tank)
{
    owners().insert(id, tank);
}

void TankHub::handOver(FishTank *from, quint64 id, int side, qreal y)
{
    const QRect src = screenOf(from);
    const qreal globalY = src.top() + y * (from->height() > 0 ? src.height() / from->height() : 1.0);
    FishTank *target = nullptr;
    qreal targetY = y;
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (!t || t == from)
            continue;
        const QRect r = screenOf(t);
        const bool touches = side > 0 ? r.left() == src.left() + src.width() : r.left() + r.width() == src.left();
        if (!touches || r.bottom() < src.top() || r.top() > src.bottom())
            continue;
        target = t;
        const qreal local = std::clamp(globalY, qreal(r.top()), qreal(r.bottom())) - r.top();
        targetY = local * (r.height() > 0 ? t->height() / r.height() : 1.0);
        if (globalY >= r.top() && globalY <= r.bottom())
            break;   // a screen right beside it at that height: perfect
    }
    if (!target) {
        // The neighbour went away: it turns round and comes back in
        from->adopt(id, side, y, {});
        return;
    }
    owners().insert(id, target);
    from->applyAssignment(false);
    target->adopt(id, -side, targetY, from->relationsOf(id));
}

void TankHub::updateNeighbours()
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (!t)
            continue;
        const QRect a = screenOf(t);
        bool left = false, right = false;
        for (const QPointer<FishTank> &o : std::as_const(tanks())) {
            if (!o || o == t)
                continue;
            const QRect b = screenOf(o);
            if (a.isNull() || b.isNull() || b.bottom() < a.top() || b.top() > a.bottom())
                continue;
            left |= b.left() + b.width() == a.left();
            right |= b.left() == a.left() + a.width();
        }
        t->setExits(left, right);
    }
}

int TankHub::totalFish()
{
    int n = 0;
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t)
            n += t->fishCount();
    }
    return n;
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
    return totalFish();
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

namespace {
// Drops food at global point p if it lies over the tank (vertically: anywhere
// within the tank's height when y < 0)
bool feedAtGlobal(FishTank *tank, QPointF p, bool anyHeight)
{
    if (!tank || !tank->window() || tank->width() <= 0)
        return false;
    const QPointF top = tank->mapToGlobal(QPointF(0, 0));
    if (anyHeight)
        p.setY(top.y() + tank->height() / 2);
    const QPointF local = tank->mapFromGlobal(p);
    if (local.x() < 0 || local.x() >= tank->width() || local.y() < 0 || local.y() >= tank->height())
        return false;
    tank->feed(local.x());
    return true;
}

bool feedUnderCursor()
{
    const QPointF cursor = QCursor::pos();
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (feedAtGlobal(t, cursor, false))
            return true;
    }
    return false;
}
} // namespace

namespace {
QPointer<FishTank> &carryTank()
{
    static QPointer<FishTank> tank;
    return tank;
}
} // namespace

bool TankHub::carryAt(QQuickItem *item, qreal x, qreal y)
{
    if (!item || !item->window())
        return false;
    FishTank *found = nullptr;
    QPointF local;
    // A widget on the desktop shares the wallpaper's window
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t && t->window() == item->window() && t->width() > 0) {
            found = t;
            local = item->mapToItem(t, QPointF(x, y));
            break;
        }
    }
    if (!found) {
        const QPointF global = item->mapToGlobal(QPointF(x, y));
        for (const QPointer<FishTank> &t : std::as_const(tanks())) {
            if (!t || t->width() <= 0)
                continue;
            const QPointF p = t->mapFromGlobal(global);
            if (p.x() >= 0 && p.y() >= 0 && p.x() < t->width() && p.y() < t->height()) {
                found = t;
                local = p;
                break;
            }
        }
    }
    // Moved over to another screen: the old bubble pops
    if (carryTank() && carryTank() != found)
        carryTank()->carryEnd();
    carryTank() = found;
    if (found)
        found->carryTo(local);
    return found;
}

void TankHub::carryEnd()
{
    if (carryTank())
        carryTank()->carryEnd();
    carryTank() = nullptr;
}

bool TankHub::feedBelow(QQuickItem *item)
{
    if (!item || !item->window())
        return false;
    const QPointF centre(item->width() / 2, item->height() / 2);
    // A widget on the desktop shares the wallpaper's window: no global maths needed
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (t && t->window() == item->window() && t->width() > 0) {
            const qreal x = item->mapToItem(t, centre).x();
            t->feed(std::clamp(x, 0.0, t->width() - 1));
            Q_EMIT stateChanged();
            return true;
        }
    }
    // Elsewhere (a panel): the tank on the same screen
    const QPointF global = item->mapToGlobal(centre);
    return feedAtGlobalX(global.x(), global.y());
}

bool TankHub::feedAtGlobalX(qreal x, qreal y)
{
    for (const QPointer<FishTank> &t : std::as_const(tanks())) {
        if (feedAtGlobal(t, QPointF(x, y), y < 0)) {
            Q_EMIT stateChanged();
            return true;
        }
    }
    // A panel lies outside the tank's rectangle: try the x alone
    if (y >= 0)
        return feedAtGlobalX(x, -1);
    return false;
}

bool TankHub::feedAtCursor()
{
    if (!feedUnderCursor())
        return false;
    Q_EMIT stateChanged();
    return true;
}

void TankHub::setUpShortcut()
{
    // Global shortcuts belong to plasmashell's tanks, never the lock screen or a
    // preview window (registering would add an entry to the user's shortcuts)
    static bool done = false;
    if (done || QCoreApplication::applicationName() != QLatin1String("plasmashell"))
        return;
    done = true;
    auto *action = new QAction(QStringLiteral("Feed fish at cursor"), qApp);
    action->setObjectName(QStringLiteral("feed-at-cursor"));
    action->setProperty("componentName", QStringLiteral("desktocean"));
    action->setProperty("componentDisplayName", QStringLiteral("Desktocean"));
    const QList<QKeySequence> shortcut{QKeySequence(Qt::META | Qt::ALT | Qt::Key_F)};
    KGlobalAccel::self()->setDefaultShortcut(action, shortcut);
    KGlobalAccel::self()->setShortcut(action, shortcut);   // keeps a shortcut the user changed
    connect(action, &QAction::triggered, action, [] { feedUnderCursor(); });
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
