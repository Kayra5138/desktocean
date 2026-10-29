import QtQuick
import Desktocean.Core
import org.kde.notificationmanager as NotificationManager
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

// Plasma entry point. Plasma draws desktop icons and widgets above this item,
// so the aquarium only has to fill the background.
//
// The same package also runs as the lock screen wallpaper (kscreenlocker_greet).
// There the tank only shows the fish: no menu, no window or notification
// watching, and the library and friendships are left to plasmashell's tank.
WallpaperItem {
    id: root
    anchors.fill: parent

    readonly property bool lockScreen: TankHub.lockScreen
    readonly property bool covered: root.configuration.PauseOnMaximized && (windows.item?.coveredExists ?? false)
    // A window covers the desktop (whether or not that pauses the aquarium)
    readonly property bool desktopHidden: windows.item?.coveredExists ?? false

    Loader {
        id: windows
        active: !root.lockScreen
        sourceComponent: WindowWatcher {
            screenGeometry: root.parent?.screenGeometry ?? null
        }
    }

    // Nobody at the computer: fish fall asleep and the tank ticks slowly; with
    // the screen off or the session locked it stops. Any input wakes it.
    IdleWatcher {
        id: idle
        timeout: root.configuration.SleepWhenIdle ? root.configuration.IdleMinutes : 0
    }
    readonly property int sleepFps: 10

    Aquarium {
        id: aquarium
        anchors.fill: parent
        fps: idle.idle ? Math.min(root.sleepFps, root.configuration.Fps) : root.configuration.Fps
        matchRefresh: root.configuration.MatchRefresh && !idle.idle
        asleep: idle.idle
        sleepReason: "no input for " + idle.timeout + " min"
        showDebug: root.configuration.ShowDebug
        showNames: root.configuration.ShowNames
        autoFeed: root.configuration.AutoFeed
        scenery: root.configuration.Scenery
        sounds: root.configuration.Sounds
        soundVolume: root.configuration.SoundVolume / 100
        ambientSound: root.configuration.AmbientSound
        soundsMuted: root.configuration.SoundsOnlyWhenVisible && root.desktopHidden
        followTimeOfDay: root.configuration.FollowTimeOfDay
        theme: root.configuration.Theme
        reactToCpu: root.configuration.ReactToCpu
        babies: root.configuration.Babies && !root.lockScreen
        weatherEnabled: root.configuration.WeatherEffects
        weatherCity: root.configuration.WeatherCity
        // Chosen in the editor's tank layout: store it like a choice made in the settings
        // (the desktop's settings, not the lock screen's)
        onThemeRequested: theme => {
            if (root.lockScreen)
                return;
            root.configuration.Theme = theme;
            root.configuration.writeConfig();
        }
        maxFish: root.configuration.MaxFish
        running: !root.covered && !idle.screenOff && !idle.locked
        pauseReason: idle.locked ? "session locked" : idle.screenOff ? "screen off" : root.covered ? "window covers screen" : ""
    }

    // Pointer events never reach the wallpaper: the folder view sits on top and
    // consumes them. Aquarium reads the cursor through Desktocean.Core instead.

    // A fish surfaces to blow bubbles whenever a notification arrives (the
    // notification server lives in plasmashell, so this needs no D-Bus snooping).
    // Notifications usually arrive while a window covers the desktop, so one
    // that comes in while the tank is hidden is kept for when it shows again.
    readonly property bool tankVisible: aquarium.running && !aquarium.paused
    property double pendingAnnounce: 0   // ms timestamp, 0 = none

    Loader {
        active: !root.lockScreen
        sourceComponent: NotificationManager.Notifications {
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

    // Right-clicking the desktop shows one "Desktocean" entry; its submenu
    // holds everything else, so the desktop menu stays tidy
    contextualActions: root.lockScreen ? [] : [desktoceanMenu]

    PlasmaCore.Action {
        id: desktoceanMenu
        text: "Desktocean"
    }

    readonly property list<QtObject> desktoceanActions: [
        PlasmaCore.Action {
            id: feedAction
            text: "Feed fish"
            onTriggered: aquarium.feedAtCursor()
        },
        PlasmaCore.Action {
            text: "Clear food"
            icon.name: "edit-clear-all"
            visible: aquarium.foodCount > 0
            onTriggered: aquarium.clearFood()
        },
        PlasmaCore.Action {
            text: "Draw a fish…"
            icon.name: "draw-brush"
            onTriggered: aquarium.openEditor()
        },
        PlasmaCore.Action {
            text: "My fish…"
            icon.name: "view-list-icons"
            onTriggered: TankHub.openEditor("fish")
        },
        PlasmaCore.Action {
            text: "Tank layout…"
            icon.name: "view-grid"
            onTriggered: TankHub.openEditor("layout")
        },
        PlasmaCore.Action {
            text: aquarium.controlledName ? "Stop swimming " + aquarium.controlledName : "Swim a fish…"
            icon.name: "input-gaming"
            onTriggered: aquarium.controlledName ? root.stopSwimming() : root.startSwimming()
        },
        PlasmaCore.Action {
            text: aquarium.paused ? "Resume aquarium" : "Pause aquarium"
            icon.name: aquarium.paused ? "media-playback-start" : "media-playback-pause"
            onTriggered: aquarium.paused = !aquarium.paused
        }
    ]

    // Swimming a fish yourself. Clicks and keys never reach the wallpaper, so a
    // layer goes over the whole desktop (in its window) for a while: click a
    // fish to take it, steer with WASD or the arrow keys, Space to dash; Esc,
    // a right click or a click on empty water lets go.
    property Item swimLayer: null

    function startSwimming() {
        if (swimLayer)
            return;
        const top = root.Window.window ? root.Window.window.contentItem : null;
        if (top)
            swimLayer = swimLayerComponent.createObject(top);
    }

    function stopSwimming() {
        aquarium.controlEnd();
        if (swimLayer) {
            swimLayer.destroy();
            swimLayer = null;
        }
    }

    Component.onDestruction: stopSwimming()

    Component {
        id: swimLayerComponent
        FocusScope {
            id: layer
            anchors.fill: parent
            z: 1e6
            focus: true

            property bool steering: false
            property bool up: false
            property bool down: false
            property bool left: false
            property bool right: false

            function send(dash) {
                aquarium.controlInput((right ? 1 : 0) - (left ? 1 : 0), (down ? 1 : 0) - (up ? 1 : 0), dash);
            }

            function setKey(key, pressed) {
                switch (key) {
                case Qt.Key_W: case Qt.Key_Up: up = pressed; return true;
                case Qt.Key_S: case Qt.Key_Down: down = pressed; return true;
                case Qt.Key_A: case Qt.Key_Left: left = pressed; return true;
                case Qt.Key_D: case Qt.Key_Right: right = pressed; return true;
                }
                return false;
            }

            Component.onCompleted: forceActiveFocus()

            Keys.onPressed: event => {
                event.accepted = true;
                if (event.key === Qt.Key_Escape) {
                    root.stopSwimming();
                    return;
                }
                if (event.isAutoRepeat)
                    return;
                if (event.key === Qt.Key_Space)
                    send(true);
                else if (setKey(event.key, true))
                    send(false);
            }
            Keys.onReleased: event => {
                event.accepted = true;
                if (!event.isAutoRepeat && setKey(event.key, false))
                    send(false);
            }

            // The fish swam off to another screen or left the library
            Connections {
                target: aquarium
                function onControlledNameChanged() {
                    if (layer.steering && !aquarium.controlledName)
                        root.stopSwimming();
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: layer.steering ? Qt.ArrowCursor : Qt.CrossCursor
                onClicked: mouse => {
                    layer.forceActiveFocus();
                    if (mouse.button === Qt.RightButton) {
                        root.stopSwimming();
                        return;
                    }
                    const p = mapToItem(aquarium, mouse.x, mouse.y);
                    if (aquarium.controlFishAt(p.x, p.y)) {
                        layer.steering = true;
                        layer.send(false);
                    } else if (layer.steering) {
                        root.stopSwimming();
                    }
                }
            }

            // What to do, at the top of the screen
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.08
                width: hint.implicitWidth + 32
                height: hint.implicitHeight + 16
                radius: height / 2
                color: "#b0000000"
                Text {
                    id: hint
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 15
                    text: aquarium.controlledName
                        ? "Swimming as " + aquarium.controlledName + "  ·  WASD or arrows to swim  ·  Space to dash  ·  Esc to stop"
                        : "Click a fish to swim with it  ·  Esc or right-click to cancel"
                }
            }
        }
    }

    Component.onCompleted: {
        if (root.lockScreen)
            return;
        TankHub.makeSubmenu(desktoceanMenu, desktoceanActions, Qt.resolvedUrl("../icons/menu-desktocean.svg"));
        TankHub.setMonochromeIcon(feedAction, Qt.resolvedUrl("../icons/menu-feed.svg"));
    }
}
