import QtQuick
import "../package/contents/ui"

// Runs the aquarium in a normal window, without plasmashell:
//   /usr/lib/qt6/bin/qml -I build/qml dev/preview.qml
// Click = feed, Space = pause, S = save a screenshot to dev/preview.png
Window {
    id: win
    width: 1280
    height: 720
    visible: true
    title: "Desktocean preview"
    // `--fullscreen` measures at the screen's real size
    visibility: Qt.application.arguments.indexOf("--fullscreen") >= 0 ? Window.FullScreen : Window.Windowed

    Aquarium {
        id: aquarium
        anchors.fill: parent
        focus: true
        // `--theme lagoon` previews a theme
        theme: {
            const i = Qt.application.arguments.indexOf("--theme");
            return i >= 0 ? Qt.application.arguments[i + 1] : "tropical";
        }
        // `--vsync` ticks once per screen refresh
        matchRefresh: Qt.application.arguments.indexOf("--vsync") >= 0
        // Name of the fish under the cursor
        showNames: true
        // `--fps 20` tries another frame rate
        fps: {
            const i = Qt.application.arguments.indexOf("--fps");
            return i >= 0 ? Number(Qt.application.arguments[i + 1]) : 30;
        }
        // `--life-speed 300` makes babies happen within a minute or two
        lifeSpeed: {
            const i = Qt.application.arguments.indexOf("--life-speed");
            return i >= 0 ? Number(Qt.application.arguments[i + 1]) : 1;
        }
        // `--hour 23` previews the night
        hourOverride: {
            const i = Qt.application.arguments.indexOf("--hour");
            return i >= 0 ? Number(Qt.application.arguments[i + 1]) : -1;
        }

        // `--asleep` shows the fish asleep (idle) whatever the hour; Z toggles it
        asleep: Qt.application.arguments.indexOf("--asleep") >= 0
        sleepReason: "preview"

        Keys.onSpacePressed: running = !running
        // Enter simulates a notification
        Keys.onReturnPressed: announce()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Z)
                aquarium.asleep = !aquarium.asleep;
            if (event.key === Qt.Key_S)
                aquarium.grabToImage(r => r.saveToFile(Qt.resolvedUrl("preview.png").toString().replace("file://", "")));
        }

        TapHandler { onTapped: eventPoint => aquarium.feed(eventPoint.position.x) }
    }

    // `--matchmake` makes every pair best friends (with --life-speed: quick babies)
    Timer {
        interval: 600
        running: Qt.application.arguments.indexOf("--matchmake") >= 0
        onTriggered: aquarium.befriendAll()
    }

    // `--announce` simulates a notification a second after start
    Timer {
        interval: 1000
        running: Qt.application.arguments.indexOf("--announce") >= 0
        onTriggered: aquarium.announce()
    }

    // Automated snapshot then quit: add `-- --snapshot`. Optional `--snapshot-delay <ms>`
    // (default 2500) and `--no-feed`.
    Timer {
        readonly property var args: Qt.application.arguments
        readonly property int delayIndex: args.indexOf("--snapshot-delay")
        interval: delayIndex >= 0 ? Number(args[delayIndex + 1]) : 2500
        running: args.indexOf("--snapshot") >= 0
        onTriggered: {
            if (args.indexOf("--no-feed") < 0)
                aquarium.feed();
            snapshotLater.start();
        }
    }
    Timer {
        id: snapshotLater
        interval: 1500
        onTriggered: aquarium.grabToImage(r => {
            r.saveToFile(Qt.resolvedUrl("preview.png").toString().replace("file://", ""));
            Qt.quit();
        })
    }
}
