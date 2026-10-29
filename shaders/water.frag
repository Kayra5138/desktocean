#version 440
// Full-screen water: depth gradient, god rays, caustics, sand, rising bubbles.
// At night (daylight -> 0) it darkens, rays turn to faint moonlight and
// bioluminescent specks twinkle in the water.
// Weather (see native/src/weather.h): rain rings and plunging streaks along
// the surface, snowflakes that land and sink while they melt, greyer light
// under clouds and fog, lightning, and a rougher surface in the wind. All of
// it is confined to a band below the surface and skipped when its amount is 0.

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
    float agitation;    // 0 calm .. 1 busy computer: more bubbles, choppier surface
    float rain;         // 0..1 weather amounts
    float snow;
    float gloom;        // overcast: dimmer, greyer
    float haze;         // fog
    float wind;
    float flash;        // lightning, 0..1 for a moment
    float scenery;      // 1 = distant reef, kelp and wreck, floor details, sky window; 0 = plain
};

float hash(float n) { return fract(sin(n) * 43758.5453); }

// Smooth 1D value noise and a few octaves of it, for ridge lines
float noise1(float x) {
    float i = floor(x), f = fract(x);
    float u = f * f * (3.0 - 2.0 * f);
    return mix(hash(i), hash(i + 1.0), u);
}
float fbm1(float x) {
    return 0.5 * noise1(x) + 0.3 * noise1(x * 2.13 + 7.1) + 0.2 * noise1(x * 4.37 + 3.3);
}

// How much of a pixel a silhouette covers, with a soft 1.5 px edge
float cover(float d) { return smoothstep(1.5, -1.5, d); }

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
    col += rayColor * rays * mix(0.22, 0.06, night) * pow(1.0 - uv.y, 2.2) * (1.0 - 0.85 * gloom);

    // Caustic shimmer near the surface
    vec2 cp = vec2(uv.x * aspect, uv.y) * 6.0;
    float c = caustic(mod(cp, 6.2831853), time * 0.35);
    col += vec3(0.6, 0.9, 1.0) * c * 0.10 * pow(1.0 - uv.y, 4.0) * (0.25 + 0.75 * daylight) * (1.0 - 0.8 * gloom);

    // ---- Distant scenery: far reef, kelp and a wreck, fading into the water ----
    if (scenery > 0.5) {
        float x = uv.x * aspect;
        // Far ridge: rolling reef hills well back, a shade darker than the water
        float farTop = sandTop - 0.2 - 0.14 * fbm1(x * 1.8 + 11.0);
        vec3 farColor = col * 0.74 + shallow * 0.05;
        // Nearer ridge: lower rocks with round coral heads along the top, darker
        float nearTop = sandTop - 0.06 - 0.08 * fbm1(x * 3.1 + 2.0);
        float lobes = 0.0;
        for (int k = 0; k < 8; k++) {
            float fk = float(k);
            float cx = (fk + 0.25 + 0.5 * hash(fk * 3.3 + 1.0)) / 8.0 * aspect;
            float r = 0.03 + 0.03 * hash(fk * 7.7);
            vec2 d = vec2(x - cx, (uv.y - (sandTop - 0.06 - 0.08 * fbm1(cx * 3.1 + 2.0))) * 1.2);
            lobes = max(lobes, smoothstep(r, r - 0.003, length(d)));
        }
        vec3 nearColor = col * 0.55;

        // Kelp: tall leafy strands swaying slowly, between the two ridges
        float kelp = 0.0;
        for (int k = 0; k < 10; k++) {
            float fk = float(k);
            float kx = (fk + 0.15 + 0.7 * hash(fk * 5.1)) / 10.0 * aspect;
            float height = 0.3 + 0.25 * hash(fk * 9.3);
            float base = sandTop;
            float up = (base - uv.y) / height;          // 0 at the root .. 1 at the tip
            if (up < 0.0 || up > 1.0)
                continue;
            float sway = sin(time * 0.3 + fk * 1.7 + up * 2.5) * 0.04 * up * up;
            // A stem with leaves bulging out every so often
            float leaf = 0.5 + 0.5 * sin(up * 38.0 + fk * 3.0);
            float width = (0.004 + 0.009 * leaf * (1.0 - 0.6 * up)) * (1.0 - 0.5 * up);
            float dx = abs(x - kx - sway);
            kelp = max(kelp, cover((dx - width) * resolution.y));
        }

        // A wreck resting on the far reef: a tilted hull and a broken mast
        float wx = 0.66 * aspect;
        float wreckBase = sandTop - 0.2 - 0.14 * fbm1(wx * 1.8 + 11.0);
        vec2 w = vec2(x - wx, uv.y - (wreckBase - 0.025));
        float ca = cos(0.1), sa = sin(0.1);
        w = vec2(ca * w.x - sa * w.y, sa * w.x + ca * w.y);
        // Hull: wider at the deck, rounded keel
        float hull = max(abs(w.x) - 0.13 + max(w.y, 0.0) * 1.4, abs(w.y) - 0.035);
        float cabin = max(abs(w.x + 0.04) - 0.03, abs(w.y + 0.05) - 0.018);
        float mast = max(abs(w.x - 0.03 + (w.y + 0.12) * 0.25) - 0.003, abs(w.y + 0.12) - 0.09);
        float spar = max(abs(w.y + 0.17 - (w.x - 0.05) * 0.4) - 0.0025, abs(w.x - 0.05) - 0.05);
        float wreck = max(max(cover(hull * resolution.y), cover(cabin * resolution.y)),
                          max(cover(mast * resolution.y), cover(spar * resolution.y)));

        col = mix(col, farColor, cover((farTop - uv.y) * resolution.y));
        col = mix(col, col * 0.82, wreck);
        col = mix(col, col * 0.7, kelp * 0.85);
        float nearCov = max(cover((nearTop - uv.y) * resolution.y), lobes);
        col = mix(col, nearColor, nearCov);

        // The sky window: the bright patch overhead, sun by day, moon by night
        vec2 sw = vec2((uv.x - 0.5) * aspect * 0.7, uv.y + 0.08);
        float window = smoothstep(0.42, 0.0, length(sw));
        vec3 skyColor = mix(vec3(0.75, 0.95, 1.0), vec3(0.35, 0.45, 0.65), night);
        col += skyColor * window * window * mix(0.18, 0.07, night) * (1.0 - 0.7 * gloom);
        vec2 sun = vec2((uv.x - 0.5 - 0.08 * sin(time * 0.01)) * aspect, uv.y - 0.02);
        col += mix(vec3(1.0, 0.98, 0.9), vec3(0.8, 0.85, 1.0), night)
             * smoothstep(0.06, 0.0, length(sun)) * mix(0.25, 0.12, night) * (1.0 - 0.8 * gloom);
    }

    // Surface line
    // Surface line; wind makes it rougher, snow paler
    float surf = 0.012 + 0.004 * (1.0 + 1.5 * agitation + wind) * sin(uv.x * 40.0 + time * 1.3)
               + 0.002 * wind * sin(uv.x * aspect * 140.0 + time * 4.1);
    surf = max(surf, 0.002);   // at 0 or below the whole column would light up
    vec3 surfColor = mix(vec3(0.7, 0.95, 1.0), vec3(0.95), snow * 0.6);
    col += surfColor * smoothstep(surf, 0.0, uv.y) * mix(0.35, 0.12, night) * (1.0 - 0.4 * gloom);

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
        sand += vec3(0.5, 0.6, 0.55) * sc * mix(0.18, 0.26, scenery) * (1.0 - d) * daylight;
        if (scenery > 0.5) {
            // Ripples in the sand, getting wider toward the front
            float rp = sin((uv.x * aspect * 60.0 + 3.0 * sin(uv.x * aspect * 4.0)) / (0.6 + d) + d * 14.0);
            sand *= 1.0 + 0.05 * rp;
            // Pebbles and the odd shell, larger toward the front
            float cellSize = mix(14.0, 34.0, d);
            vec2 cell = floor(vec2(px.x, px.y) / cellSize);
            float h = hash(cell.x * 91.7 + cell.y * 47.3);
            if (h > 0.86) {
                vec2 c = (cell + vec2(0.3 + 0.4 * hash(h * 3.0), 0.3 + 0.4 * hash(h * 7.0))) * cellSize;
                vec2 q = (px - c) / (cellSize * (0.12 + 0.12 * hash(h * 11.0)));
                if (h > 0.985) {
                    // A little scallop shell: a fan with ribs
                    float r = length(q);
                    float ang = atan(q.x, -q.y);
                    float fan = step(r, 1.4) * step(abs(ang), 1.2) * step(-0.2, -q.y);
                    float ribs = 0.85 + 0.15 * sin(ang * 14.0);
                    sand = mix(sand, vec3(0.93, 0.86, 0.8) * ribs * (0.3 + 0.7 * daylight), fan * 0.85);
                } else {
                    float pebble = smoothstep(1.0, 0.8, length(q * vec2(1.0, 1.6)));
                    vec3 stone = mix(sandDark.rgb * 0.7, sandLight.rgb * 1.1, hash(h * 5.0)) * (0.3 + 0.7 * daylight);
                    sand = mix(sand, stone, pebble * 0.7);
                }
            }
        }
        // Blend sand toward water color for depth
        sand = mix(sand, deep, 0.35);
        float soft = smoothstep(edge, edge + 0.006, uv.y);
        col = mix(col, sand, soft);
    }

    // Bubble columns; extra ones appear as the water gets agitated
    float columns = 6.0 + 6.0 * agitation;
    for (int i = 0; i < 12; i++) {
        float fi = float(i);
        if (fi >= columns)
            break;
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


    // ---- Weather -------------------------------------------------------------
    float surfPx = 0.012 * resolution.y;
    float lit = 0.35 + 0.65 * daylight;

    // Rain: rings where drops hit the surface, and short streaks as they plunge in
    if (rain > 0.0 && px.y < 0.12 * resolution.y) {
        if (px.y < 0.04 * resolution.y) {
            const float cellW = 34.0;
            float cell = floor(px.x / cellW);
            for (int k = -1; k <= 1; k++) {
                float c = cell + float(k);
                float h = hash(c * 12.9898);
                float t = time * (0.9 + 0.7 * h) + h * 7.0;
                float cycle = floor(t);
                float life = fract(t);
                float seed = c * 3.1 + cycle * 17.7;
                if (hash(seed) >= rain)
                    continue;
                vec2 center = vec2((c + 0.15 + 0.7 * hash(seed + 1.3)) * cellW,
                                   surfPx * (0.5 + 0.8 * hash(seed + 2.9)));
                float r = 2.0 + life * (10.0 + 8.0 * hash(seed + 4.1));
                float d = length(vec2(px.x - center.x, (px.y - center.y) * 3.2));
                float fade = (1.0 - life) * (1.0 - life);
                float ring = smoothstep(1.6, 0.0, abs(d - r)) * fade;
                float splash = smoothstep(3.0, 0.0, d) * smoothstep(0.15, 0.0, life);
                col += vec3(0.8, 0.95, 1.0) * (ring * 0.6 + splash * 0.7) * lit;
            }
        }
        // Streaks, slanted by the wind, slowing and fading as they sink
        const float colW = 23.0;
        float slant = 0.35 * wind;
        float qx = px.x - slant * (px.y - surfPx);
        float col1 = floor(qx / colW);
        float h = hash(col1 * 7.77 + 0.5);
        float t = time * (0.6 + 0.5 * h) + h * 11.0;
        float seed = col1 * 5.3 + floor(t) * 13.1;
        if (hash(seed) < rain * 0.75) {
            float life = fract(t);
            float depth = (0.03 + 0.07 * hash(seed + 3.3)) * resolution.y;
            float head = surfPx + depth * (1.0 - (1.0 - life) * (1.0 - life));
            float len = 6.0 + 14.0 * (1.0 - life);
            float cx = (col1 + 0.2 + 0.6 * hash(seed + 8.1)) * colW;
            float across = smoothstep(1.2, 0.0, abs(qx - cx));
            float along = smoothstep(head - len, head, px.y) * step(px.y, head) * step(surfPx * 0.6, px.y);
            col += vec3(0.75, 0.9, 1.0) * across * along * (1.0 - life) * 0.5 * lit;
        }
    }

    // Snow: flakes resting on the surface, and flakes sinking and melting below it
    if (snow > 0.0 && px.y < 0.24 * resolution.y) {
        vec3 flakeColor = vec3(0.95, 0.97, 1.0) * lit;
        if (px.y < surfPx * 2.2) {
            const float cellW = 19.0;
            float drift = time * (6.0 + 30.0 * wind);
            float cell = floor((px.x - drift) / cellW);
            float h = hash(cell * 4.71);
            if (h < snow * 0.8) {
                float melt = fract(time * 0.05 + h * 13.0);
                vec2 center = vec2((cell + 0.5) * cellW + drift + 5.0 * sin(time * 0.4 + h * 30.0),
                                   surfPx * (0.6 + 0.6 * hash(h * 9.0)));
                float r = (1.8 + 1.6 * hash(h * 5.0)) * (1.0 - melt * 0.6);
                float d = length(px - center);
                col += flakeColor * smoothstep(r, r - 1.2, d) * 0.55 * (1.0 - melt);
            }
        }
        const float cellS = 46.0;
        vec2 cell = floor(vec2(px.x - time * 8.0 * wind, px.y) / cellS);
        float h = hash(cell.x * 157.3 + cell.y * 37.9 + 3.0);
        if (h < snow * 0.5) {
            float life = fract(time * (0.04 + 0.04 * h) + h * 17.0);
            vec2 center = vec2((cell.x + 0.5) * cellS + time * 8.0 * wind + 9.0 * sin(time * 0.6 + h * 40.0),
                               (cell.y + life) * cellS);
            float sinkFade = 1.0 - smoothstep(surfPx, 0.24 * resolution.y, center.y);
            float r = (2.2 + 2.2 * hash(h * 3.0)) * (1.0 - 0.6 * life) * (0.5 + 0.5 * sinkFade);
            float d = length(px - center);
            float a = smoothstep(0.0, 0.1, life) * sinkFade * smoothstep(r, r - 1.0, d);
            col += flakeColor * a * 0.5;
        }
    }

    // Clouds and fog: dimmer, greyer light; fog washes out with distance
    if (gloom > 0.0 || haze > 0.0) {
        float lum = dot(col, vec3(0.3, 0.59, 0.11));
        col = mix(col, vec3(lum) * vec3(0.92, 0.96, 1.0), gloom * 0.4) * (1.0 - 0.22 * gloom);
        vec3 mist = mix(shallow, vec3(dot(shallow, vec3(0.3, 0.59, 0.11))), 0.6) * (1.1 + 0.2 * daylight);
        col = mix(col, mist, haze * 0.3 * (1.0 - 0.5 * uv.y));
    }

    // Lightning lights the water from above
    col += vec3(0.75, 0.82, 0.95) * flash * (0.45 - 0.3 * uv.y);

    fragColor = vec4(col, 1.0) * qt_Opacity;
}
