#version 440
// Fish and food sprites, already bent and placed on the CPU (see fishtank.cpp).

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in float fog;
layout(location = 3) in float alpha;

layout(location = 0) out vec2 vUv;
layout(location = 1) out float vFog;
layout(location = 2) out float vAlpha;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 fogColor;
};

out gl_PerVertex { vec4 gl_Position; };

void main() {
    vUv = texcoord;
    vFog = fog;
    vAlpha = alpha;
    gl_Position = qt_Matrix * vec4(position, 0.0, 1.0);
}
