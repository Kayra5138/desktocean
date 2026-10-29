#include "idlewatcher.h"
#include "tankhub.h"

#include <KIdleTime>
#include <kscreendpms/dpms.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QQuickWindow>

namespace {
const QString kScreenSaver = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kScreenSaverPath = QStringLiteral("/ScreenSaver");
} // namespace

IdleWatcher::IdleWatcher(QQuickItem *parent)
    : QQuickItem(parent)
{
    KIdleTime *idleTime = KIdleTime::instance();
    connect(idleTime, &KIdleTime::timeoutReached, this, [this](int id) {
        if (id != m_timeoutId)
            return;
        setIdle(true);
        KIdleTime::instance()->catchNextResumeEvent();
    });
    // Any key or mouse movement, whoever asked to be told about it
    connect(idleTime, &KIdleTime::resumingFromIdle, this, [this] { setIdle(false); });

    m_dpms = new KScreen::Dpms(this);
    connect(m_dpms, &KScreen::Dpms::modeChanged, this, [this](KScreen::Dpms::Mode mode, QScreen *screen) {
        const QScreen *mine = window() ? window()->screen() : nullptr;
        if (screen && mine && screen != mine)
            return;
        const bool off = mode != KScreen::Dpms::On;
        if (off == m_screenOff)
            return;
        m_screenOff = off;
        Q_EMIT screenOffChanged();
    });

    // The lock screen is what shows while locked; only the desktop pauses
    if (TankHub::isLockScreen())
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kScreenSaver, kScreenSaverPath, kScreenSaver, QStringLiteral("ActiveChanged"), this, SLOT(setLocked(bool)));
    const QDBusMessage call = QDBusMessage::createMethodCall(kScreenSaver, kScreenSaverPath, kScreenSaver, QStringLiteral("GetActive"));
    auto *pending = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<bool> reply = *w;
        if (!reply.isError())
            setLocked(reply.value());
        w->deleteLater();
    });
}

IdleWatcher::~IdleWatcher()
{
    if (m_timeoutId >= 0)
        KIdleTime::instance()->removeIdleTimeout(m_timeoutId);
}

void IdleWatcher::setTimeout(int minutes)
{
    minutes = std::max(0, minutes);
    if (minutes == m_timeout)
        return;
    m_timeout = minutes;
    if (m_timeoutId >= 0)
        KIdleTime::instance()->removeIdleTimeout(m_timeoutId);
    m_timeoutId = minutes > 0 ? KIdleTime::instance()->addIdleTimeout(minutes * 60 * 1000) : -1;
    if (minutes == 0)
        setIdle(false);
    Q_EMIT timeoutChanged();
}

void IdleWatcher::setIdle(bool idle)
{
    if (idle == m_idle)
        return;
    m_idle = idle;
    Q_EMIT idleChanged();
}

void IdleWatcher::setLocked(bool locked)
{
    if (locked == m_locked)
        return;
    m_locked = locked;
    Q_EMIT lockedChanged();
}
