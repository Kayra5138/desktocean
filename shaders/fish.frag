#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float phase;
    float amplitude;
    float fog;
    vec4 fogColor;
    vec2 itemSize;
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec4 c = texture(source, qt_TexCoord0);   // premultiplied alpha
    vec3 tinted = mix(c.rgb, fogColor.rgb * c.a, fog);
    fragColor = vec4(tinted, c.a) * qt_Opacity;
}
