import QtQuick

// The whole aquarium scene. Plain Qt Quick (no Plasma imports) so it also runs
// in dev/preview.qml. Nothing animates on its own: a single Timer advances the
// simulation and the scene graph only redraws when that tick changes something,
// which caps redraws at `fps` and drops them to zero while `running` is false.
Item {
    id: aquarium

    property bool running: true
    property int fps: 30
    property int fishCount: 7
    property bool showDebug: true
    property string pauseReason: ""
    // Cursor position in item coordinates, or (-1, -1) when unknown
    property point pointer: Qt.point(-1, -1)

    readonly property var fishImages: ["fish-orange.svg", "fish-blue.svg", "fish-pink.svg", "fish-lime.svg"]
    readonly property real sandTopRatio: 0.86
    readonly property real waterTop: height * 0.05
    readonly property real sandTop: height * sandTopRatio
    readonly property color fogColor: "#0b3048"
    readonly property bool pointerValid: pointer.x >= 0 && pointer.y >= 0

    property real time: 0
    property var fishState: []
    property var foodState: []
    property int maxFood: 24
    property bool _foodVisible: false

    function rand(a, b) {
        return a + Math.random() * (b - a);
    }

    function clamp(v, lo, hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    function pickTarget(f) {
        f.tx = rand(f.w * 0.6, width - f.w * 0.6);
        f.ty = rand(waterTop + f.h, sandTop - f.h * 0.7);
        f.cruise = rand(0.45, 1.0) * f.maxSpeed;
    }

    function resetFish() {
        const list = [];
        for (let i = 0; i < fishCount; i++) {
            const depth = Math.random();
            const w = rand(150, 230) * (1 - 0.45 * depth);
            const f = {
                x: rand(w, width - w),
                y: rand(waterTop + w, sandTop - w),
                vx: 0, vy: 0,
                depth: depth,
                w: w, h: w * 0.6,
                maxSpeed: rand(60, 115) * (1 - 0.3 * depth),
                facing: Math.random() < 0.5 ? -1 : 1,
                phase: rand(0, 6.28),
                speed: 0,
                curious: i === 0
            };
            pickTarget(f);
            list.push(f);
        }
        fishState = list;
        applyState();
    }

    // Drops food from the surface above `atX`, or somewhere random if not given
    function feed(atX) {
        const food = foodState.slice();
        const cx = atX >= 0 ? atX : rand(width * 0.15, width * 0.85);
        for (let i = 0; i < 8 && food.length < maxFood; i++)
            food.push({ x: cx + rand(-90, 90), y: waterTop + rand(-10, 30), seed: rand(0, 6.28), age: 0, landed: false, eaten: false });
        foodState = food;
        applyState();
    }

    function step(dt) {
        time += dt;

        for (const p of foodState) {
            if (!p.landed) {
                p.y += 38 * dt;
                p.x += Math.sin(time * 2.2 + p.seed) * 10 * dt;
                if (p.y >= sandTop + 4) {
                    p.y = sandTop + 4;
                    p.landed = true;
                }
            } else {
                p.age += dt;
            }
        }
        for (const f of fishState) {
            const mx = f.x + f.facing * f.w * 0.42;   // mouth
            const my = f.y + f.h * 0.06;
            let tx = f.tx, ty = f.ty, speed = f.cruise, useMouth = false;

            let food = null, best = 1e9;
            for (const p of foodState) {
                const d = Math.hypot(p.x - mx, p.y - my);
                if (d < best) { best = d; food = p; }
            }

            if (food) {
                tx = food.x; ty = food.y; speed = f.maxSpeed * 1.6; useMouth = true;
            } else if (f.curious && pointerValid && pointer.y > waterTop && pointer.y < sandTop) {
                tx = pointer.x - f.facing * 40; ty = pointer.y; speed = f.maxSpeed * 0.8; useMouth = true;
            }

            const ox = useMouth ? mx : f.x, oy = useMouth ? my : f.y;
            const dx = tx - ox, dy = ty - oy;
            const dist = Math.max(Math.hypot(dx, dy), 0.001);

            if (!useMouth && dist < 30)
                pickTarget(f);
            if (food && dist < Math.max(14, f.w * 0.12))
                food.eaten = true;

            const arrive = Math.min(1, dist / 90);
            let desX = dx / dist * speed * arrive;
            let desY = dy / dist * speed * arrive;

            for (const g of fishState) {
                if (g === f || Math.abs(g.depth - f.depth) > 0.35)
                    continue;
                const sx = f.x - g.x, sy = (f.y - g.y) * 1.6;
                const d = Math.hypot(sx, sy);
                const minD = (f.w + g.w) * 0.38;
                if (d > 0.001 && d < minD) {
                    desX += sx / d * (minD - d) * 1.5;
                    desY += sy / d * (minD - d) * 1.5;
                }
            }

            const k = Math.min(1, 1.6 * dt);
            f.vx += (desX - f.vx) * k;
            f.vy += (desY - f.vy) * k;
            f.vy = clamp(f.vy, -0.55 * f.maxSpeed, 0.55 * f.maxSpeed);

            f.x = clamp(f.x + f.vx * dt, f.w * 0.5, width - f.w * 0.5);
            f.y = clamp(f.y + f.vy * dt, waterTop + f.h * 0.5, sandTop - f.h * 0.3);

            if (Math.abs(f.vx) > 10)
                f.facing += ((f.vx > 0 ? 1 : -1) - f.facing) * Math.min(1, 3.5 * dt);

            f.speed = Math.hypot(f.vx, f.vy);
            f.phase += dt * (3.0 + f.speed * 0.07);
        }

        const alive = foodState.filter(p => !p.eaten && p.age < 10);
        if (alive.length !== foodState.length)
            foodState = alive;

        applyState();
    }

    function applyState() {
        for (let i = 0; i < fishRepeater.count; i++) {
            const item = fishRepeater.itemAt(i);
            const f = fishState[i];
            if (!item || !f)
                continue;
            item.x = f.x - f.w / 2;
            item.y = f.y - f.h / 2;
            item.facing = f.facing;
            item.phase = f.phase;
            item.amplitude = 0.05 + 0.07 * Math.min(1, f.speed / f.maxSpeed);
            const tilt = Math.atan2(f.vy, Math.abs(f.vx) + 20) * 180 / Math.PI * 0.7;
            item.tilt = clamp(tilt, -28, 28) * (f.facing < 0 ? -1 : 1);
        }
        if (foodState.length === 0 && !_foodVisible)
            return;
        _foodVisible = foodState.length > 0;
        for (let i = 0; i < foodRepeater.count; i++) {
            const item = foodRepeater.itemAt(i);
            const p = foodState[i];
            item.visible = !!p;
            if (p) {
                item.x = p.x - item.width / 2;
                item.y = p.y - item.height / 2;
                item.opacity = p.landed ? Math.max(0, 1 - p.age / 10) : 1;
            }
        }
    }

    function ensureFish() {
        if (width > 0 && height > 0 && fishState.length !== fishCount)
            resetFish();
    }

    onWidthChanged: ensureFish()
    onHeightChanged: ensureFish()
    onFishCountChanged: resetFish()
    Component.onCompleted: ensureFish()

    Timer {
        id: tick
        interval: Math.round(1000 / Math.max(1, aquarium.fps))
        repeat: true
        running: aquarium.running && aquarium.width > 0
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
        model: aquarium.maxFood
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
        model: aquarium.fishState.length
        Fish {
            required property int index
            readonly property var data_: aquarium.fishState[index]
            imageSource: Qt.resolvedUrl("../images/" + aquarium.fishImages[index % aquarium.fishImages.length])
            width: data_ ? data_.w : 0
            height: data_ ? data_.h : 0
            fog: data_ ? data_.depth * 0.55 : 0
            fogColor: aquarium.fogColor
            z: data_ ? 10 + (1 - data_.depth) * 10 : 10
        }
        onItemAdded: Qt.callLater(aquarium.applyState)
    }

    // Spike diagnostics, top-right so desktop icons (top-left) stay readable
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

        Text {
            id: debugText
            anchors.centerIn: parent
            color: "white"
            font.family: "monospace"
            font.pixelSize: 13
            text: debugBox.snapshot
        }

        property string snapshot: ""
        function refreshSnapshot() {
            snapshot = "Desktocean · Faz 0\n"
                + "FPS: " + debugBox.measuredFps + " (hedef " + aquarium.fps + ")\n"
                + "Durum: " + (aquarium.running ? "çalışıyor" : "duraklatıldı" + (aquarium.pauseReason ? " (" + aquarium.pauseReason + ")" : "")) + "\n"
                + "Balık: " + aquarium.fishState.length + "  Yem: " + aquarium.foodState.length + "\n"
                + "İmleç: " + (aquarium.pointerValid ? Math.round(aquarium.pointer.x) + "," + Math.round(aquarium.pointer.y) : "yok");
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
            onTriggered: {
                debugBox.measuredFps = debugBox.frames;
                debugBox.frames = 0;
                debugBox.refreshSnapshot();
            }
        }
    }
}
