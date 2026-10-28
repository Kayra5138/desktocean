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
        fishCount: root.configuration.FishCount
        showDebug: root.configuration.ShowDebug
        running: !root.userPaused && !root.covered
        pauseReason: root.userPaused ? "kullanıcı" : (root.covered ? "pencere kaplıyor" : "")
        pointer: root.cursor
    }

    // Pointer events never reach the wallpaper itself: the folder view sits on top
    // and consumes them. A passive HoverHandler on the desktop window's root item
    // still sees the cursor (hover propagates up the ancestor chain) without
    // stealing clicks from icons or widgets.
    readonly property var hostWindow: Window.window
    property var hoverProbe: null
    readonly property point cursor: hoverProbe && hoverProbe.hovered
        ? aquarium.mapFromItem(hostWindow.contentItem, hoverProbe.point.position)
        : Qt.point(-1, -1)
    // The context menu takes the pointer while open, so remember where it was
    property point lastCursor: Qt.point(-1, -1)
    onCursorChanged: if (cursor.x >= 0) lastCursor = cursor

    onHostWindowChanged: attachHoverProbe()
    Component.onCompleted: attachHoverProbe()
    Component.onDestruction: if (hoverProbe) hoverProbe.destroy()

    function attachHoverProbe() {
        if (hoverProbe || !hostWindow || !hostWindow.contentItem)
            return;
        hoverProbe = Qt.createQmlObject("import QtQuick; HoverHandler {}", hostWindow.contentItem, "desktoceanHoverProbe");
    }

    contextualActions: [
        PlasmaCore.Action {
            text: "Yem at"
            icon.name: "food"
            onTriggered: aquarium.feed(root.lastCursor.x)
        },
        PlasmaCore.Action {
            text: root.userPaused ? "Akvaryumu devam ettir" : "Akvaryumu duraklat"
            icon.name: root.userPaused ? "media-playback-start" : "media-playback-pause"
            onTriggered: root.userPaused = !root.userPaused
        }
    ]
}
