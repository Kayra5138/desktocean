#version 440
// Bends a fish texture along its spine. Texture convention: head on the right
// (u = 1), tail on the left (u = 0). The tail swings most, the head barely.

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 qt_TexCoord0;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float phase;
    float amplitude;    // fraction of item height
    float fog;          // 0 = near, 1 = fully tinted toward water
    vec4 fogColor;
    vec2 itemSize;
};

out gl_PerVertex { vec4 gl_Position; };

void main() {
    vec2 uv = qt_MultiTexCoord0;
    vec4 p = qt_Vertex;

    float tailWeight = pow(1.0 - uv.x, 1.7);
    float wave = sin(phase - uv.x * 4.2);
    p.y += wave * amplitude * itemSize.y * tailWeight;
    // Tiny counter-sway of the head keeps the motion from looking hinged
    p.y -= sin(phase) * amplitude * 0.12 * itemSize.y * uv.x;
    // Body shortens slightly at the peak of each swing
    p.x += abs(wave) * tailWeight * itemSize.x * 0.025;

    qt_TexCoord0 = uv;
    gl_Position = qt_Matrix * p;
}
