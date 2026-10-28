import QtQuick

// One fish: a texture bent along its spine by fish.vert.
// Position, size, facing and phase are driven by Aquarium's simulation tick.
ShaderEffect {
    id: fish

    property url imageSource
    property real facing: 1          // -1 = left, 1 = right; values between squash the turn
    property real tilt: 0            // degrees
    property real phase: 0
    property real amplitude: 0.07
    property real fog: 0
    property color fogColor: "#0a2a40"
    readonly property size itemSize: Qt.size(width, height)
    readonly property var source: texture

    vertexShader: Qt.resolvedUrl("../shaders/fish.vert.qsb")
    fragmentShader: Qt.resolvedUrl("../shaders/fish.frag.qsb")
    mesh: GridMesh { resolution: Qt.size(20, 2) }
    blending: true

    transform: [
        Scale {
            origin.x: fish.width / 2
            origin.y: fish.height / 2
            xScale: Math.abs(fish.facing) < 0.08 ? (fish.facing < 0 ? -0.08 : 0.08) : fish.facing
        },
        Rotation {
            origin.x: fish.width / 2
            origin.y: fish.height / 2
            angle: fish.tilt
        }
    ]

    Image {
        id: texture
        source: fish.imageSource
        sourceSize: Qt.size(400, 240)
        smooth: true
        mipmap: true
        visible: false
    }
}
