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

    Aquarium {
        id: aquarium
        anchors.fill: parent
        focus: true
        // `--theme lagoon` previews a theme
        theme: {
            const i = Qt.application.arguments.indexOf("--theme");
            return i >= 0 ? Qt.application.arguments[i + 1] : "tropical";
        }
        // `--hour 23` previews the night
        hourOverride: {
            const i = Qt.application.arguments.indexOf("--hour");
            return i >= 0 ? Number(Qt.application.arguments[i + 1]) : -1;
        }

        Keys.onSpacePressed: running = !running
        // Enter simulates a notification
        Keys.onReturnPressed: announce()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_S)
                aquarium.grabToImage(r => r.saveToFile(Qt.resolvedUrl("preview.png").toString().replace("file://", "")));
        }

        TapHandler { onTapped: eventPoint => aquarium.feed(eventPoint.position.x) }
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
