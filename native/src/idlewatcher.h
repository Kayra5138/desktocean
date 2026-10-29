#pragma once

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace KScreen
{
class Dpms;
}

// Tells the aquarium when nobody is looking: no input for a while (`idle`),
// this screen switched off (`screenOff`) or the session locked (`locked`).
// Idle comes from KIdleTime, so it works on Wayland; any input clears it at
// once. An item rather than a plain object so it knows which screen it is on.
class IdleWatcher : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // Minutes without input before `idle`; 0 = never
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)
    Q_PROPERTY(bool screenOff READ screenOff NOTIFY screenOffChanged)
    // Always false on the lock screen itself, which is what shows while locked
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)

public:
    explicit IdleWatcher(QQuickItem *parent = nullptr);
    ~IdleWatcher() override;

    int timeout() const { return m_timeout; }
    void setTimeout(int minutes);
    bool idle() const { return m_idle; }
    bool screenOff() const { return m_screenOff; }
    bool locked() const { return m_locked; }

Q_SIGNALS:
    void timeoutChanged();
    void idleChanged();
    void screenOffChanged();
    void lockedChanged();

private Q_SLOTS:
    void setLocked(bool locked);

private:
    void setIdle(bool idle);

    int m_timeout = 0;
    int m_timeoutId = -1;
    bool m_idle = false;
    bool m_screenOff = false;
    bool m_locked = false;
    KScreen::Dpms *m_dpms = nullptr;
};
