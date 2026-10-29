import QtQuick
import "../package/contents/ui"
import Desktocean.Core

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
        // `--sounds` plays the sounds (with the quiet water loop)
        sounds: Qt.application.arguments.indexOf("--sounds") >= 0
        ambientSound: Qt.application.arguments.indexOf("--sounds") >= 0
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
        // `--weather rain` forces rain|drizzle|heavy-rain|snow|storm|fog|cloudy|wind|clear;
        // `--city Paris` tries the real weather; without either it is simulated. `--no-weather` turns it off
        weatherEnabled: Qt.application.arguments.indexOf("--no-weather") < 0
        forcedWeather: {
            const i = Qt.application.arguments.indexOf("--weather");
            return i >= 0 ? Qt.application.arguments[i + 1] : "";
        }
        weatherCity: {
            const i = Qt.application.arguments.indexOf("--city");
            return i >= 0 ? Qt.application.arguments[i + 1] : "";
        }

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

    // `--export-sprites DIR` saves the sprites drawn in code as PNGs
    Component.onCompleted: {
        const i = Qt.application.arguments.indexOf("--export-sprites");
        if (i >= 0)
            console.warn("exported:", aquarium.tank.exportSprites(Qt.application.arguments[i + 1]));
    }

    // `--bubble` blows a bubble that drifts slowly round the middle of the
    // tank, as the bubble wand would; fish next to it come in
    Timer {
        readonly property bool on: Qt.application.arguments.indexOf("--bubble") >= 0
        property real t: 0
        interval: 33
        repeat: true
        running: on
        onTriggered: {
            t += interval / 1000;
            // Waits in the middle first, so someone can come in, then goes round
            const go = Math.max(0, t - 8);
            TankHub.carryAt(aquarium, aquarium.width / 2 + Math.sin(go * 0.25) * aquarium.width * 0.25,
                            aquarium.height * 0.45 + Math.sin(go * 0.5) * aquarium.height * 0.15);
        }
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
