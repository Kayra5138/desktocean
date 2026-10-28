import QtQuick
import Desktocean.Core

// The whole aquarium scene. Plain Qt Quick (no Plasma imports) so it also runs
// in dev/preview.qml. FishTank (C++/Rust) ticks the simulation and draws all fish
// and food in one node; the water shader only follows its clock. Redraws are
// capped at `fps` and drop to zero while paused.
Item {
    id: aquarium

    property alias running: tank.running
    property alias fps: tank.fps
    readonly property alias fishCount: tank.fishCount
    property bool showDebug: true
    property string pauseReason: ""
    readonly property alias foodCount: tank.foodCount

    // Must match SAND_TOP in sim/src/sim.rs
    readonly property real sandTopRatio: 0.86

    // Drops food from the surface above `atX`, or somewhere random if omitted
    function feed(atX) {
        tank.feed(atX === undefined ? -1 : atX);
    }

    // Drops food above the cursor when it is over the aquarium, otherwise somewhere random
    function feedAtCursor() {
        tank.feedAtCursor();
    }

    function clearFood() {
        tank.clearFood();
    }

    function openEditor() {
        return tank.openEditor();
    }

    ShaderEffect {
        id: water
        anchors.fill: parent
        property real time: tank.time
        property real sandTop: aquarium.sandTopRatio
        property size resolution: Qt.size(width, height)
        fragmentShader: Qt.resolvedUrl("../shaders/water.frag.qsb")
    }

    FishTank {
        id: tank
        anchors.fill: parent
        fogColor: "#0b3048"
        // Seeds an empty fish library on first run; after that the library is the source of truth
        starterFish: ["fish-orange.svg", "fish-blue.svg", "fish-pink.svg", "fish-lime.svg"]
            .map(name => Qt.resolvedUrl("../images/" + name))
    }

    // Diagnostics, top-right so desktop icons (top-left) stay readable
    Rectangle {
        id: debugBox
        visible: aquarium.showDebug
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 24
        width: debugText.implicitWidth + 24
        height: debugText.implicitHeight + 16
        radius: 8
        color: "#99000000"

        property int frames: 0
        property int measuredFps: 0
        property string snapshot: ""

        function refreshSnapshot() {
            const p = tank.pointer;
            snapshot = "Desktocean · Phase 2\n"
                + "Core: rust " + tank.coreVersion + (tank.healthy ? " (ok)" : " (STOPPED)") + "\n"
                + "FPS: " + measuredFps + " (target " + tank.fps + ")\n"
                + "State: " + (tank.running ? "running" : "paused" + (aquarium.pauseReason ? " (" + aquarium.pauseReason + ")" : "")) + "\n"
                + "Fish: " + tank.fishCount + "  Food: " + tank.foodCount + "\n"
                + "Following cursor: " + (tank.lureActive ? "yes" : "no") + "\n"
                + "Cursor: " + (p.x >= 0 ? Math.round(p.x) + "," + Math.round(p.y) : "none");
        }

        Text {
            id: debugText
            anchors.centerIn: parent
            color: "white"
            font.family: "monospace"
            font.pixelSize: 13
            text: debugBox.snapshot
        }

        Connections {
            target: aquarium.Window.window
            enabled: aquarium.showDebug
            function onFrameSwapped() { debugBox.frames++; }
        }

        Timer {
            interval: 1000
            repeat: true
            running: aquarium.showDebug
            triggeredOnStart: true
            onTriggered: {
                debugBox.measuredFps = debugBox.frames;
                debugBox.frames = 0;
                debugBox.refreshSnapshot();
            }
        }
    }
}
