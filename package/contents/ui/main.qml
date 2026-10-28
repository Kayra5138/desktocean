import QtQuick
import org.kde.notificationmanager as NotificationManager
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

// Plasma entry point. Plasma draws desktop icons and widgets above this item,
// so the aquarium only has to fill the background.
WallpaperItem {
    id: root
    anchors.fill: parent

    readonly property bool covered: root.configuration.PauseOnMaximized && windows.coveredExists

    WindowWatcher {
        id: windows
        screenGeometry: root.parent?.screenGeometry ?? null
    }

    Aquarium {
        id: aquarium
        anchors.fill: parent
        fps: root.configuration.Fps
        showDebug: root.configuration.ShowDebug
        followTimeOfDay: root.configuration.FollowTimeOfDay
        theme: root.configuration.Theme
        reactToCpu: root.configuration.ReactToCpu
        running: !root.covered
        pauseReason: root.covered ? "window covers screen" : ""
    }

    // Pointer events never reach the wallpaper: the folder view sits on top and
    // consumes them. Aquarium reads the cursor through Desktocean.Core instead.

    // A fish surfaces to blow bubbles whenever a notification arrives (the
    // notification server lives in plasmashell, so this needs no D-Bus snooping).
    // Notifications usually arrive while a window covers the desktop, so one
    // that comes in while the tank is hidden is kept for when it shows again.
    readonly property bool tankVisible: aquarium.running && !aquarium.paused
    property double pendingAnnounce: 0   // ms timestamp, 0 = none

    NotificationManager.Notifications {
        id: notifications
        showNotifications: root.configuration.ReactToNotifications
        showJobs: false
        showExpired: false
        showDismissed: false
        onRowsInserted: {
            // Do Not Disturb is a property of the notification server, not of this model
            const quiet = NotificationManager.Server.inhibited;
            console.debug("Desktocean: notification arrived,", quiet ? "do not disturb" : root.tankVisible ? "announcing" : "tank hidden, keeping it for later");
            if (quiet)
                return;
            if (root.tankVisible)
                aquarium.announce();
            else
                root.pendingAnnounce = Date.now();
        }
    }

    onTankVisibleChanged: {
        if (tankVisible && pendingAnnounce > 0) {
            // Only if it is still news
            if (Date.now() - pendingAnnounce < 2 * 60 * 1000)
                announceLater.start();
            pendingAnnounce = 0;
        }
    }
    // Give the desktop a moment to come into view first
    Timer {
        id: announceLater
        interval: 1200
        onTriggered: aquarium.announce()
    }

    contextualActions: [
        PlasmaCore.Action {
            text: "Draw a fish…"
            icon.name: "draw-brush"
            onTriggered: aquarium.openEditor()
        },
        PlasmaCore.Action {
            text: "Feed fish"
            icon.name: "food"
            onTriggered: aquarium.feedAtCursor()
        },
        PlasmaCore.Action {
            text: "Clear food"
            icon.name: "edit-clear-all"
            visible: aquarium.foodCount > 0
            onTriggered: aquarium.clearFood()
        },
        PlasmaCore.Action {
            text: aquarium.paused ? "Resume aquarium" : "Pause aquarium"
            icon.name: aquarium.paused ? "media-playback-start" : "media-playback-pause"
            onTriggered: aquarium.paused = !aquarium.paused
        }
    ]
}
