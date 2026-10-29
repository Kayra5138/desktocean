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
    property alias matchRefresh: tank.matchRefresh
    readonly property alias fishCount: tank.fishCount
    property bool showDebug: true
    property alias followTimeOfDay: tank.followTimeOfDay
    property alias reactToCpu: tank.reactToCpu
    property alias babies: tank.babies
    property alias maxFish: tank.maxFish
    property alias lifeSpeed: tank.lifeSpeed
    // Name of the fish under the cursor
    property alias showNames: tank.showNames
    // The tank itself, for development aids in dev/preview.qml
    readonly property alias tank: tank
    // Swimming a fish yourself (see main.qml)
    readonly property alias controlledName: tank.controlledName
    function controlFishAt(x, y) { return tank.controlFishAt(Qt.point(x, y)); }
    function controlInput(dx, dy, dash) { tank.controlInput(dx, dy, dash); }
    function controlEnd() { tank.controlEnd(); }
    property alias autoFeed: tank.autoFeed
    // Distant reef, kelp and wreck, sand details and the sky window
    property bool scenery: true
    // Optional sounds
    property alias sounds: tank.sounds
    property alias soundVolume: tank.soundVolume
    property alias ambientSound: tank.ambientSound
    property alias soundsMuted: tank.soundsMuted
    // Theme id, see native/src/themes.cpp
    property alias theme: tank.theme
    // The editor asked for another theme
    signal themeRequested(string theme)
    readonly property var themeColors: tank.themeColors
    property alias hourOverride: tank.hourOverride
    property string pauseReason: ""
    // Fish rest as at night (water light unchanged); sleepReason says why, for the diagnostics
    property alias asleep: tank.asleep
    property string sleepReason: ""
    // Weather on the surface and in the light (see native/src/weather.h)
    property alias weatherEnabled: weather.enabled
    // City for real weather from Open-Meteo; empty = simulated seasonal weather
    property alias weatherCity: weather.city
    // Development aid: rain|snow|storm|fog|wind|clear|...
    property alias forcedWeather: weather.forced
    readonly property alias foodCount: tank.foodCount

    // Must match SAND_TOP in sim/src/sim.rs
    readonly property real sandTopRatio: 0.86

    // Drops food from the surface above `atX`, or somewhere random if omitted
    function feed(atX) {
        tank.feed(atX === undefined ? -1 : atX);
    }

    // Drops food above the cursor when it is over the aquarium, otherwise somewhere random
    // Right at (x, y), sinking from there
    function feedAt(x, y) {
        tank.feedAt(Qt.point(x, y));
    }

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

    Weather {
        id: weather
        onLightning: lightning.restart()
    }

    // 0..1 while lightning lights up the tank: a bright flicker, then a slow fade
    property real flash: 0
    SequentialAnimation {
        id: lightning
        NumberAnimation { target: aquarium; property: "flash"; to: 1; duration: 40 }
        NumberAnimation { target: aquarium; property: "flash"; to: 0.25; duration: 90 }
        NumberAnimation { target: aquarium; property: "flash"; to: 0.85; duration: 50 }
        NumberAnimation { target: aquarium; property: "flash"; to: 0; duration: 700; easing.type: Easing.OutQuad }
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
        property real rain: weather.rain
        property real snow: weather.snow
        property real gloom: weather.gloom
        property real haze: weather.haze
        property real wind: weather.windiness
        property real flash: aquarium.flash
        property real scenery: aquarium.scenery ? 1 : 0
        fragmentShader: Qt.resolvedUrl("../shaders/water.frag.qsb")
    }

    FishTank {
        id: tank
        anchors.fill: parent
        onThemeRequested: theme => aquarium.themeRequested(theme)
        soundDirectory: Qt.resolvedUrl("../sounds/")
        // Wind and storms stir the water like a busy computer does
        weatherStir: weather.stir
        // Overcast skies dim the fish a little; lightning lights them up
        weatherLight: 1 - 0.18 * weather.gloom + 0.5 * aquarium.flash
        // Distant fish fade into the water colour, which darkens at night and greys in fog
        readonly property color themeFog: Qt.tint(aquarium.themeColors.deep, Qt.rgba(aquarium.themeColors.shallow.r, aquarium.themeColors.shallow.g, aquarium.themeColors.shallow.b, 0.35))
        readonly property real fogGrey: (themeFog.r + themeFog.g + themeFog.b) / 3 + 0.12 * weather.haze
        readonly property color weatherFog: Qt.rgba(themeFog.r + (fogGrey - themeFog.r) * (0.3 * weather.gloom + 0.6 * weather.haze),
                                                    themeFog.g + (fogGrey - themeFog.g) * (0.3 * weather.gloom + 0.6 * weather.haze),
                                                    themeFog.b + (fogGrey - themeFog.b) * (0.3 * weather.gloom + 0.6 * weather.haze), 1)
        fogColor: Qt.rgba(weatherFog.r * (0.2 + 0.8 * daylight), weatherFog.g * (0.2 + 0.8 * daylight), weatherFog.b * (0.3 + 0.7 * daylight), 1)
        // Seeds an empty fish library on first run; after that the library is the source of truth
        starterFish: ["fish-orange.svg", "fish-blue.svg", "fish-pink.svg", "fish-lime.svg"]
            .map(name => Qt.resolvedUrl("../images/" + name))
        starterDecor: ["decor-seaweed-tall.svg", "decor-seaweed-bushy.svg", "decor-rock.svg", "decor-coral.svg", "decor-cave.svg",
                       "crawler-crab.svg", "crawler-snail.svg"]
            .map(name => Qt.resolvedUrl("../images/" + name))
    }

    // The hovered fish's name, just above it; follows the fish as it swims
    Rectangle {
        id: nameTag
        readonly property bool shown: tank.hoveredName !== ""
        // Keeps the last name while fading out
        property string name: ""
        visible: opacity > 0
        opacity: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        onShownChanged: if (shown) name = tank.hoveredName
        Connections {
            target: tank
            function onHoverChanged() { if (tank.hoveredName !== "") nameTag.name = tank.hoveredName; }
        }
        x: Math.round(Math.max(4, Math.min(aquarium.width - width - 4, tank.hoveredPoint.x - width / 2)))
        y: Math.round(Math.max(4, tank.hoveredPoint.y - height - 6))
        width: nameText.implicitWidth + 16
        height: nameText.implicitHeight + 8
        radius: height / 2
        color: "#a0000000"
        Text {
            id: nameText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 14
            text: nameTag.name
        }
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
                + "FPS: " + measuredFps + " (target " + (tank.matchRefresh ? "screen refresh" : tank.fps) + ")\n"
                + "State: " + (tank.running && !tank.paused ? (tank.asleep ? "asleep (" + (aquarium.sleepReason || "idle") + ")" : "running")
                               : "paused (" + (tank.paused ? "by user" : aquarium.pauseReason) + ")") + "\n"
                + "Fish: " + tank.fishCount + "  Food: " + tank.foodCount + "\n"
                + "Following cursor: " + (tank.lureActive ? "yes" : "no") + "\n"
                + tank.stats() + "\n"
                + "Stir: " + Math.round(tank.agitation * 100) + "% (weather " + Math.round(tank.weatherStir * 100) + "%)\n"
                + "Weather: " + weather.summary() + "\n"
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
