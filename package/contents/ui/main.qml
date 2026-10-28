import QtQuick
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

// Plasma entry point. Plasma draws desktop icons and widgets above this item,
// so the aquarium only has to fill the background.
WallpaperItem {
    id: root
    anchors.fill: parent

    property bool userPaused: false
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
        running: !root.userPaused && !root.covered
        pauseReason: root.userPaused ? "by user" : (root.covered ? "window covers screen" : "")
    }

    // Pointer events never reach the wallpaper: the folder view sits on top and
    // consumes them. Aquarium reads the cursor through Desktocean.Core instead.

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
            text: root.userPaused ? "Resume aquarium" : "Pause aquarium"
            icon.name: root.userPaused ? "media-playback-start" : "media-playback-pause"
            onTriggered: root.userPaused = !root.userPaused
        }
    ]
}
