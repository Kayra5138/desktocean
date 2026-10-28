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

        Keys.onSpacePressed: running = !running
        Keys.onPressed: event => {
            if (event.key === Qt.Key_S)
                aquarium.grabToImage(r => r.saveToFile(Qt.resolvedUrl("preview.png").toString().replace("file://", "")));
        }

        TapHandler { onTapped: eventPoint => aquarium.feed(eventPoint.position.x) }
    }

    // Automated snapshot then quit: add `-- --snapshot`
    Timer {
        interval: 2500
        running: Qt.application.arguments.indexOf("--snapshot") >= 0
        onTriggered: {
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
