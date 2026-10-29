#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class FishTank;

// Lets other parts of plasmashell (the Desktocean widget) reach the aquarium
// wallpaper. Every FishTank registers itself in a process-wide list; the hub
// forwards commands to all of them (one per screen) and summarises their state.
class TankHub : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // A Desktocean wallpaper is running in this process
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY stateChanged)
    Q_PROPERTY(int fishCount READ fishCount NOTIFY stateChanged)
    Q_PROPERTY(int foodCount READ foodCount NOTIFY stateChanged)
    // 0 = everyone full, 1 = everyone starving
    Q_PROPERTY(qreal hunger READ hunger NOTIFY stateChanged)
    Q_PROPERTY(int friendships READ friendships NOTIFY stateChanged)
    Q_PROPERTY(int rivalries READ rivalries NOTIFY stateChanged)
    Q_PROPERTY(qreal daylight READ daylight NOTIFY stateChanged)
    // This process is the lock screen (kscreenlocker_greet), not plasmashell:
    // the tank only shows the fish there and never writes the library
    Q_PROPERTY(bool lockScreen READ lockScreen CONSTANT)

public:
    explicit TankHub(QObject *parent = nullptr);

    static void registerTank(FishTank *tank);
    static void unregisterTank(FishTank *tank);
    // The first tank does the once-per-library jobs (recording babies), so a
    // multi-monitor setup doesn't do them once per screen
    static bool isPrimary(const FishTank *tank);
    // Running inside the lock screen; DESKTOCEAN_LOCKSCREEN=1/0 overrides for testing
    static bool isLockScreen();

    bool available() const;
    bool paused() const;
    void setPaused(bool paused);
    int fishCount() const;
    int foodCount() const;
    qreal hunger() const;
    int friendships() const;
    int rivalries() const;
    qreal daylight() const;
    bool lockScreen() const { return isLockScreen(); }

    // Drops food somewhere in every tank
    Q_INVOKABLE void feed();
    Q_INVOKABLE void clearFood();
    // page: "" for drawing, "fish" for the fish list, "layout" for the tank layout
    Q_INVOKABLE bool openEditor(const QString &page = {});

    // Turns `action` (a PlasmaCore.Action) into a submenu holding `children`,
    // with the icon at `icon`. QML can't attach a menu to an action itself.
    Q_INVOKABLE void makeSubmenu(QObject *action, const QList<QObject *> &children, const QUrl &icon = {});
    /// Gives a menu action a single-colour icon drawn in the menu's text colour,
    /// like the theme's own symbolic icons (the SVG's shape is used, not its colours)
    Q_INVOKABLE void setMonochromeIcon(QObject *action, const QUrl &svg);

Q_SIGNALS:
    void stateChanged();

private:
    FishTank *first() const;

    QTimer m_refresh;
};
