#version 440
// Full-screen water: depth gradient, god rays, caustics, sand, rising bubbles.
// At night (daylight -> 0) it darkens, rays turn to faint moonlight and
// bioluminescent specks twinkle in the water.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float sandTop;      // 0..1, where the sand starts (from top)
    vec2 resolution;
    float daylight;     // 0 = night, 1 = day
    vec4 shallowColor;  // theme: water near the surface
    vec4 deepColor;     // theme: water near the bottom
    vec4 sandLight;     // theme: sand at its edge
    vec4 sandDark;      // theme: sand at the bottom of the screen
};

float hash(float n) { return fract(sin(n) * 43758.5453); }

// Cheap caustic pattern: a few domain-warped sine iterations.
float caustic(vec2 p, float t) {
    p -= 250.0;   // the formula is only well-behaved far from the origin
    vec2 i = p;
    float c = 1.0;
    float inten = 0.005;
    for (int n = 0; n < 4; n++) {
        float tt = t * (1.0 - (3.5 / float(n + 1)));
        i = p + vec2(cos(tt - i.x) + sin(tt + i.y), sin(tt - i.y) + cos(tt + i.x));
        c += 1.0 / length(vec2(p.x / (sin(i.x + tt) / inten), p.y / (cos(i.y + tt) / inten)));
    }
    c /= 4.0;
    c = 1.17 - pow(c, 1.4);
    return clamp(pow(abs(c), 8.0), 0.0, 1.0);
}

void main() {
    vec2 uv = qt_TexCoord0;              // (0,0) top-left
    vec2 px = uv * resolution;
    float aspect = resolution.x / resolution.y;

    float night = 1.0 - daylight;

    // Depth gradient
    vec3 shallow = mix(shallowColor.rgb, vec3(0.03, 0.09, 0.19), night);
    vec3 deep    = mix(deepColor.rgb, vec3(0.004, 0.02, 0.06), night);
    vec3 col = mix(shallow, deep, smoothstep(0.0, 1.0, uv.y));

    // God rays from the surface, slowly drifting
    float rx = uv.x * aspect + uv.y * 0.35;
    float rays = sin(rx * 7.0 + time * 0.15) * 0.5 + 0.5;
    rays *= sin(rx * 13.0 - time * 0.23 + 1.7) * 0.5 + 0.5;
    rays *= sin(rx * 3.0 + time * 0.07) * 0.5 + 0.5;
    vec3 rayColor = mix(vec3(0.55, 0.85, 0.9), vec3(0.35, 0.45, 0.7), night);
    col += rayColor * rays * mix(0.22, 0.06, night) * pow(1.0 - uv.y, 2.2);

    // Caustic shimmer near the surface
    vec2 cp = vec2(uv.x * aspect, uv.y) * 6.0;
    float c = caustic(mod(cp, 6.2831853), time * 0.35);
    col += vec3(0.6, 0.9, 1.0) * c * 0.10 * pow(1.0 - uv.y, 4.0) * (0.25 + 0.75 * daylight);

    // Surface line
    float surf = 0.012 + 0.004 * sin(uv.x * 40.0 + time * 1.3);
    col += vec3(0.7, 0.95, 1.0) * smoothstep(surf, 0.0, uv.y) * mix(0.35, 0.12, night);

    // Sand with a wavy edge
    float edge = sandTop
        + 0.012 * sin(uv.x * aspect * 5.0 + 1.3)
        + 0.006 * sin(uv.x * aspect * 17.0);
    if (uv.y > edge) {
        float d = (uv.y - edge) / max(1.0 - edge, 0.001);
        vec3 sand = mix(sandLight.rgb, sandDark.rgb, d) * (0.3 + 0.7 * daylight);
        float grain = hash(floor(px.x * 0.5) + floor(px.y * 0.5) * 1931.0);
        sand *= 0.93 + 0.07 * grain;
        float sc = caustic(mod(vec2(uv.x * aspect, uv.y) * 9.0, 6.2831853), time * 0.4);
        sand += vec3(0.5, 0.6, 0.55) * sc * 0.18 * (1.0 - d) * daylight;
        // Blend sand toward water color for depth
        sand = mix(sand, deep, 0.35);
        float soft = smoothstep(edge, edge + 0.006, uv.y);
        col = mix(col, sand, soft);
    }

    // Bubble columns
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        float bx = 0.08 + 0.84 * hash(fi * 7.13);
        float speed = 0.05 + 0.04 * hash(fi * 3.7);
        for (int j = 0; j < 4; j++) {
            float fj = float(j);
            float life = fract(time * speed + fj * 0.25 + hash(fi + fj) * 0.2);
            float y = edge - life * (edge - 0.01);
            float x = bx + 0.006 * sin(life * 25.0 + fi * 2.0 + fj);
            float r = (3.0 + 4.0 * hash(fi * 11.0 + fj)) * (0.6 + life * 0.6);
            float d = length(px - vec2(x, y) * resolution);
            float ring = smoothstep(r, r - 1.5, d) - smoothstep(r - 1.5, r - 3.0, d) * 0.7;
            float hl = smoothstep(r * 0.35, 0.0, length(px - (vec2(x, y) * resolution + vec2(-r * 0.35, -r * 0.35))));
            col += vec3(0.75, 0.95, 1.0) * (ring * 0.45 + hl * 0.5) * (1.0 - life * 0.3) * (0.35 + 0.65 * daylight);
        }
    }

    // Bioluminescent specks: one candidate per 48 px cell, slowly drifting and twinkling
    if (night > 0.01) {
        vec2 cell = floor(px / 48.0);
        float h = hash(cell.x * 157.0 + cell.y * 311.0);
        if (h > 0.82) {
            vec2 center = (cell + vec2(hash(h * 13.0), hash(h * 29.0))) * 48.0
                        + vec2(sin(time * 0.3 + h * 40.0), cos(time * 0.25 + h * 30.0)) * 10.0;
            float d = length(px - center);
            float twinkle = 0.5 + 0.5 * sin(time * (0.8 + h * 1.5) + h * 60.0);
            float glow = smoothstep(6.0, 0.0, d) * twinkle * twinkle;
            col += vec3(0.25, 0.9, 0.85) * glow * 0.7 * night;
        }
    }

    fragColor = vec4(col, 1.0) * qt_Opacity;
}
