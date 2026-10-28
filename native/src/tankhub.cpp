#include "tankhub.h"
#include "fishtank.h"

#include <QList>

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

bool TankHub::openEditor(const QString &page)
{
    return FishTank::launchEditor(page);
}
