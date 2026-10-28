import QtQuick
import Desktocean.Core

// The whole aquarium scene. Plain Qt Quick (no Plasma imports) so it also runs
// in dev/preview.qml. FishTank (C++/Rust) ticks the simulation and draws all fish
// and food in one node; the water shader only follows its clock. Redraws are
// capped at `fps` and drop to zero while paused.
Item {
    id: aquarium

    property alias running: tank.running
    property alias paused: tank.paused
    property alias fps: tank.fps
    readonly property alias fishCount: tank.fishCount
    property bool showDebug: true
    property alias followTimeOfDay: tank.followTimeOfDay
    property alias reactToCpu: tank.reactToCpu
    property alias babies: tank.babies
    property alias maxFish: tank.maxFish
    property alias lifeSpeed: tank.lifeSpeed
    // Theme id, see native/src/themes.cpp
    property alias theme: tank.theme
    readonly property var themeColors: tank.themeColors
    property alias hourOverride: tank.hourOverride
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

    // A fish swims up and blows bubbles
    function announce() {
        tank.announce();
    }

    // Development aid: everyone becomes best friends
    function befriendAll() {
        tank.befriendAll();
    }

    function openEditor() {
        return tank.openEditor();
    }

    ShaderEffect {
        id: water
        anchors.fill: parent
        property real time: tank.flow
        property real agitation: tank.agitation
        property real sandTop: aquarium.sandTopRatio
        property size resolution: Qt.size(width, height)
        property real daylight: tank.daylight
        property color shallowColor: aquarium.themeColors.shallow
        property color deepColor: aquarium.themeColors.deep
        property color sandLight: aquarium.themeColors.sandLight
        property color sandDark: aquarium.themeColors.sandDark
        fragmentShader: Qt.resolvedUrl("../shaders/water.frag.qsb")
    }

    FishTank {
        id: tank
        anchors.fill: parent
        // Distant fish fade into the water colour, which darkens at night
        readonly property color themeFog: Qt.tint(aquarium.themeColors.deep, Qt.rgba(aquarium.themeColors.shallow.r, aquarium.themeColors.shallow.g, aquarium.themeColors.shallow.b, 0.35))
        fogColor: Qt.rgba(themeFog.r * (0.2 + 0.8 * daylight), themeFog.g * (0.2 + 0.8 * daylight), themeFog.b * (0.3 + 0.7 * daylight), 1)
        // Seeds an empty fish library on first run; after that the library is the source of truth
        starterFish: ["fish-orange.svg", "fish-blue.svg", "fish-pink.svg", "fish-lime.svg"]
            .map(name => Qt.resolvedUrl("../images/" + name))
        starterDecor: ["decor-seaweed-tall.svg", "decor-seaweed-bushy.svg", "decor-rock.svg", "decor-coral.svg"]
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
            snapshot = "Desktocean · Phase 4\n"
                + "Core: rust " + tank.coreVersion + (tank.healthy ? " (ok)" : " (STOPPED)") + "\n"
                + "FPS: " + measuredFps + " (target " + tank.fps + ")\n"
                + "State: " + (tank.running && !tank.paused ? "running" : "paused (" + (tank.paused ? "by user" : aquarium.pauseReason) + ")") + "\n"
                + "Fish: " + tank.fishCount + "  Food: " + tank.foodCount + "\n"
                + "Following cursor: " + (tank.lureActive ? "yes" : "no") + "\n"
                + tank.stats() + "\n"
                + "CPU stir: " + Math.round(tank.agitation * 100) + "%\n"
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
