import QtQuick
import Desktocean.Core

// The whole aquarium scene. Plain Qt Quick (no Plasma imports) so it also runs
// in dev/preview.qml. Nothing animates on its own: a single Timer advances the
// Rust simulation and the scene graph only redraws when that tick changes
// something, which caps redraws at `fps` and drops them to zero while paused.
Item {
    id: aquarium

    property bool running: true
    property int fps: 30
    property int fishCount: 7
    property bool showDebug: true
    property string pauseReason: ""

    // Cursor in item coordinates, or (-1, -1) when it is not over the aquarium
    property point pointer: Qt.point(-1, -1)
    readonly property bool pointerValid: pointer.x >= 0 && pointer.y >= 0

    readonly property var fishImages: ["fish-orange.svg", "fish-blue.svg", "fish-pink.svg", "fish-lime.svg"]
    // Must match SAND_TOP in sim/src/sim.rs
    readonly property real sandTopRatio: 0.86
    readonly property color fogColor: "#0b3048"
    // Food sprites are pooled; the pool grows with demand and shrinks once the food is gone
    readonly property int foodPoolMin: 32
    property int foodPool: foodPoolMin

    property real time: 0
    property int foodCount: 0

    // Drops food from the surface above `atX`, or somewhere random if omitted
    function feed(atX) {
        sim.feed(atX === undefined ? -1 : atX);
        applyState();
    }

    // Drops food above the cursor when it is known, otherwise somewhere random
    function feedAtCursor() {
        const p = cursorInScene();
        feed(p.x >= 0 ? p.x : -1);
    }

    function clearFood() {
        sim.clearFood();
        applyState();
    }

    function cursorInScene() {
        const g = Cursor.position();
        const p = aquarium.mapFromGlobal(g.x, g.y);
        const inside = p.x >= 0 && p.y >= 0 && p.x < width && p.y < height;
        return inside ? p : Qt.point(-1, -1);
    }

    function step(dt) {
        time += dt;
        pointer = cursorInScene();
        sim.step(dt, pointer);
        applyState();
    }

    function applyState() {
        const fish = sim.fish();
        const S = sim.fishStride;
        for (let i = 0; i < fishRepeater.count; i++) {
            const item = fishRepeater.itemAt(i);
            const b = i * S;
            if (!item || b + S > fish.length)
                continue;
            const w = fish[b + 2], h = fish[b + 3], depth = fish[b + 8];
            if (item.width !== w) {
                // Size and depth are fixed per fish; only touch them when they change
                item.width = w;
                item.height = h;
                item.fog = depth * 0.55;
                item.z = 10 + (1 - depth) * 10;
            }
            item.x = fish[b] - w / 2;
            item.y = fish[b + 1] - h / 2;
            item.facing = fish[b + 4];
            item.phase = fish[b + 5];
            item.amplitude = fish[b + 6];
            item.tilt = fish[b + 7];
        }

        const food = sim.food();
        const F = sim.foodStride;
        const n = food.length / F;
        if (n === 0 && foodCount === 0)
            return;
        if (n > foodPool)
            foodPool = Math.ceil(n / foodPoolMin) * foodPoolMin;   // Repeater adds items synchronously
        else if (n === 0 && foodPool > foodPoolMin)
            foodPool = foodPoolMin;
        for (let i = 0; i < foodRepeater.count; i++) {
            const item = foodRepeater.itemAt(i);
            item.visible = i < n;
            if (i < n) {
                item.x = food[i * F] - item.width / 2;
                item.y = food[i * F + 1] - item.height / 2;
                item.opacity = food[i * F + 2];
            }
        }
        foodCount = n;
    }

    function syncSize() {
        sim.resize(width, height);
        applyState();
    }

    onWidthChanged: syncSize()
    onHeightChanged: syncSize()
    Component.onCompleted: syncSize()

    Simulation {
        id: sim
        fishCount: aquarium.fishCount
        onFishCountChanged: Qt.callLater(aquarium.applyState)
    }

    Timer {
        id: tick
        interval: Math.round(1000 / Math.max(1, aquarium.fps))
        repeat: true
        running: aquarium.running && aquarium.width > 0 && sim.healthy
        property double last: 0
        onRunningChanged: last = 0
        onTriggered: {
            const now = Date.now();
            const dt = last > 0 ? Math.min((now - last) / 1000, 0.1) : interval / 1000;
            last = now;
            aquarium.step(dt);
        }
    }

    ShaderEffect {
        id: water
        anchors.fill: parent
        property real time: aquarium.time
        property real sandTop: aquarium.sandTopRatio
        property size resolution: Qt.size(width, height)
        fragmentShader: Qt.resolvedUrl("../shaders/water.frag.qsb")
    }

    Repeater {
        id: foodRepeater
        model: aquarium.foodPool
        Rectangle {
            visible: false
            width: 9
            height: 9
            radius: 4.5
            z: 20
            color: "#c8743a"
            border.color: "#7a3f19"
            border.width: 1.5
        }
    }

    Repeater {
        id: fishRepeater
        model: sim.fishCount
        Fish {
            required property int index
            imageSource: Qt.resolvedUrl("../images/" + aquarium.fishImages[index % aquarium.fishImages.length])
            fogColor: aquarium.fogColor
        }
        onItemAdded: Qt.callLater(aquarium.applyState)
    }

    // Diagnostics, top-right so desktop icons (top-left) stay readable
    Rectangle {
        id: debugBox
        visible: aquarium.showDebug
        z: 100
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
            const g = Cursor.position();
            snapshot = "Desktocean · Phase 0b\n"
                + "Core: rust " + sim.coreVersion + (sim.healthy ? " (ok)" : " (STOPPED)") + "\n"
                + "FPS: " + measuredFps + " (target " + aquarium.fps + ")\n"
                + "State: " + (aquarium.running ? "running" : "paused" + (aquarium.pauseReason ? " (" + aquarium.pauseReason + ")" : "")) + "\n"
                + "Fish: " + sim.fishCount + "  Food: " + aquarium.foodCount + "\n"
                + "Following cursor: " + (sim.lureActive() ? "yes" : "no") + "\n"
                + "Cursor: global " + Math.round(g.x) + "," + Math.round(g.y)
                + " / scene " + (aquarium.pointerValid ? Math.round(aquarium.pointer.x) + "," + Math.round(aquarium.pointer.y) : "none");
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
