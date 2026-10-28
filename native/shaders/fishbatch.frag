#version 440

layout(location = 0) in vec2 vUv;
layout(location = 1) in float vFog;
layout(location = 2) in float vAlpha;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 fogColor;
    float light;       // 1 by day, darker at night
};

layout(binding = 1) uniform sampler2D atlas;

void main() {
    vec4 c = texture(atlas, vUv);   // premultiplied alpha
    vec3 tinted = mix(c.rgb, fogColor.rgb * c.a, vFog) * light;
    fragColor = vec4(tinted, c.a) * vAlpha * qt_Opacity;
}
