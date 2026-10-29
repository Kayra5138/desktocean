#include "fishmesh.h"

#include <algorithm>
#include <cmath>

namespace FishMesh {

const std::vector<Style> &styles()
{
    // id, label, waveLength, floor, power, ampMul, freqMul, bob, squash, glide
    static const std::vector<Style> list = {
        {QStringLiteral("classic"), QStringLiteral("Classic"), 4.2f, 0.0f, 1.7f, 1.0f, 1.0f, 0.0f, 0.0f, false},
        {QStringLiteral("eel"), QStringLiteral("Eel – whole body waves"), 10.0f, 0.45f, 1.0f, 0.9f, 0.8f, 0.0f, 0.0f, false},
        {QStringLiteral("stiff"), QStringLiteral("Stiff – tail only"), 3.0f, 0.0f, 4.0f, 1.5f, 1.4f, 0.0f, 0.0f, false},
        {QStringLiteral("bouncy"), QStringLiteral("Bouncy – bobs up and down"), 3.0f, 0.0f, 2.0f, 0.6f, 0.8f, 0.1f, 0.05f, false},
        {QStringLiteral("glider"), QStringLiteral("Glider – beat and coast"), 4.0f, 0.0f, 1.7f, 1.1f, 1.0f, 0.02f, 0.0f, true},
        {QStringLiteral("turtle"), QStringLiteral("Turtle – slow paddling, comes up for air"), 2.0f, 0.0f, 6.0f, 0.4f, 0.5f,
         0.04f, 0.0f, true, Body::Turtle},
        {QStringLiteral("ray"), QStringLiteral("Ray – rippling wings, near the bottom"), 2.8f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f,
         0.0f, false, Body::Ray},
        {QStringLiteral("seahorse"), QStringLiteral("Seahorse – upright, fins flutter (draw it standing)"), 3.0f, 0.0f,
         2.0f, 1.0f, 1.0f, 0.03f, 0.0f, false, Body::Seahorse},
        {QStringLiteral("jelly"), QStringLiteral("Jellyfish – pulses up, drifts down (bell on top)"), 3.0f, 0.0f, 2.0f,
         1.0f, 1.0f, 0.0f, 0.0f, false, Body::Jelly},
    };
    return list;
}

const Style &style(const QString &id)
{
    const auto &list = styles();
    const auto it = std::find_if(list.begin(), list.end(), [&](const Style &s) { return s.id == id; });
    return it != list.end() ? *it : list.front();
}

std::vector<float> weightsFromMask(const QImage &mask)
{
    if (mask.isNull())
        return {};
    const QImage grey = mask.convertToFormat(QImage::Format_Grayscale8);
    std::vector<float> weights;
    weights.reserve(kVertsPerFish);
    // Average a small box around each vertex so a thin stroke still counts
    const int rx = std::max(1, grey.width() / (kSegments * 2));
    const int ry = std::max(1, grey.height() / (kRows * 2));
    for (int r = 0; r <= kRows; ++r) {
        const int cy = std::clamp(int(std::lround(double(r) / kRows * (grey.height() - 1))), 0, grey.height() - 1);
        for (int c = 0; c <= kSegments; ++c) {
            const int cx = std::clamp(int(std::lround(double(c) / kSegments * (grey.width() - 1))), 0, grey.width() - 1);
            int sum = 0, n = 0;
            for (int y = std::max(0, cy - ry); y <= std::min(grey.height() - 1, cy + ry); ++y) {
                const uchar *row = grey.constScanLine(y);
                for (int x = std::max(0, cx - rx); x <= std::min(grey.width() - 1, cx + rx); ++x) {
                    sum += row[x];
                    ++n;
                }
            }
            weights.push_back(n ? float(sum) / n / 128.f : 1.f);
        }
    }
    return weights;
}

void writeFish(const Pose &f, const QRectF &uv, const Style &st, const std::vector<float> &weights, Vertex *v,
               quint32 *idx, quint32 base)
{
    const float w = f.w, h = f.h;
    // Keep a sliver of width mid-turn so the sprite never collapses to a line
    const float flip = std::abs(f.facing) < 0.08f ? (f.facing < 0 ? -0.08f : 0.08f) : f.facing;
    const float rad = f.tilt * float(M_PI) / 180.f;
    const float cs = std::cos(rad), sn = std::sin(rad);

    const float ph = f.phase * st.freqMul;
    float amp = f.amplitude * st.ampMul;
    if (st.glide) {
        // A few strong beats, then a long coast with the tail almost still
        const float e = 0.5f + 0.5f * std::sin(ph * 0.18f);
        amp *= 0.15f + 1.25f * e * e;
    }
    const float bob = std::sin(ph * 0.35f) * st.bob * h;
    const float squash = 1.f + st.squash * std::sin(ph * 0.7f);
    const float headSway = std::sin(ph) * amp * 0.12f * h * (1.f - st.floor);
    const bool masked = weights.size() == size_t(kVertsPerFish);

    // The bend and the wave only depend on the column: work them out once per
    // column instead of once per vertex
    float columnBend[kSegments + 1];
    float columnWave[kSegments + 1];
    for (int c = 0; c <= kSegments; ++c) {
        const float u = float(c) / kSegments;
        columnBend[c] = st.floor + (1.f - st.floor) * std::pow(1.f - u, st.power);
        columnWave[c] = std::sin(ph - u * st.waveLength);
    }

    // Jellyfish: the bell squeezes on each pulse (in step with the simulation,
    // which pushes it up then), the tentacles trail and sway
    const float pulse = std::max(0.f, std::sin(f.phase * 0.5f));

    for (int r = 0; r <= kRows; ++r) {
        const float vv = float(r) / kRows;
        for (int c = 0; c <= kSegments; ++c) {
            const float u = float(c) / kSegments;
            float bend = columnBend[c];
            const float paint = masked ? weights[size_t(r) * (kSegments + 1) + c] : 1.f;
            bend *= paint;
            const float wave = columnWave[c];

            float px = u * w + std::abs(wave) * bend * w * 0.025f;
            float py = (vv - 0.5f) * h * squash + 0.5f * h + wave * amp * h * bend - headSway * u + bob;
            float mirror = flip;
            switch (st.body) {
            case Body::Fish:
            case Body::Turtle:
                break;
            case Body::Jelly: {
                // Never mirrored: it has no front
                mirror = 1.f;
                const float tentacle = std::max(0.f, (vv - 0.45f) / 0.55f);
                px = w * 0.5f + (u - 0.5f) * w * (1.f - 0.14f * pulse * (1.f - vv))
                   + std::sin(f.phase * 0.9f - vv * 5.f + u * 4.f) * 0.06f * w * tentacle * paint;
                py = vv * h * (1.f + 0.06f * pulse) + pulse * 0.1f * h * tentacle;
                break;
            }
            case Body::Seahorse:
                // Stiff and upright; the fins flutter and it bobs gently
                px = u * w + std::sin(ph * 2.5f + vv * 6.f) * 0.012f * w * paint;
                py = vv * h + std::sin(ph * 0.4f) * 0.03f * h;
                break;
            case Body::Ray: {
                // Both wing edges ripple together from the front back; the tail
                // (left end) trails a little
                const float wing = std::pow(std::abs(vv - 0.5f) * 2.f, 1.4f);
                const float tail = std::max(0.f, 0.25f - u) * 4.f;
                px = u * w;
                py = vv * h + std::sin(ph * 0.5f - u * st.waveLength) * 0.16f * h * wing * paint
                   + std::sin(ph - u * 8.f) * 0.05f * h * tail;
                break;
            }
            }

            // Around the centre: mirror for facing, then pitch, then place
            const float cx = (px - w * 0.5f) * mirror;
            const float cy = py - h * 0.5f;
            Vertex &out = *v++;
            out.x = f.x + cx * cs - cy * sn;
            out.y = f.y + cx * sn + cy * cs;
            out.u = float(uv.x() + u * uv.width());
            out.v = float(uv.y() + vv * uv.height());
            out.fog = f.depth * 0.55f;
            out.alpha = 1.f;
        }
    }

    const quint32 stride = kSegments + 1;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kSegments; ++c) {
            const quint32 i0 = base + r * stride + c;
            const quint32 i1 = i0 + 1;
            const quint32 i2 = i0 + stride;
            const quint32 i3 = i2 + 1;
            *idx++ = i0; *idx++ = i2; *idx++ = i1;
            *idx++ = i1; *idx++ = i2; *idx++ = i3;
        }
    }
}

void writeDecor(const DecorPose &d, const QRectF &uv, float time, Vertex *v, quint32 *idx, quint32 base)
{
    for (int r = 0; r <= kDecorRows; ++r) {
        const float vv = float(r) / kDecorRows;     // 0 = top, 1 = roots
        const float height = 1.f - vv;
        // Roots stay put; the top follows a slow current with a little flutter
        const float bend = std::pow(height, 1.6f);
        const float current = std::sin(time * 0.7f + d.phase + height * 1.2f) * 0.065f
                            + std::sin(time * 1.9f + d.phase * 1.7f + height * 3.0f) * 0.015f;
        const float dx = current * d.sway * d.h * bend;
        for (int c = 0; c <= kDecorCols; ++c) {
            const float u = float(c) / kDecorCols;
            Vertex &out = *v++;
            out.x = d.x + (u - 0.5f) * d.w + dx;
            out.y = d.bottom - height * d.h + std::abs(dx) * 0.08f;
            const float tu = d.flip ? 1.f - u : u;
            out.u = float(uv.x() + tu * uv.width());
            out.v = float(uv.y() + vv * uv.height());
            out.fog = d.fog;
            out.alpha = 1.f;
        }
    }
    const quint32 stride = kDecorCols + 1;
    for (int r = 0; r < kDecorRows; ++r) {
        for (int c = 0; c < kDecorCols; ++c) {
            const quint32 i0 = base + r * stride + c;
            const quint32 i1 = i0 + 1;
            const quint32 i2 = i0 + stride;
            const quint32 i3 = i2 + 1;
            *idx++ = i0; *idx++ = i2; *idx++ = i1;
            *idx++ = i1; *idx++ = i2; *idx++ = i3;
        }
    }
}

Gait gaitFromId(const QString &id)
{
    if (id == QLatin1String("snail"))
        return Gait::Snail;
    if (id == QLatin1String("star"))
        return Gait::Star;
    if (id == QLatin1String("walker"))
        return Gait::Walker;
    return Gait::Crab;
}

uint32_t gaitIndex(Gait gait)
{
    return uint32_t(gait);
}

void writeCrawler(const CrawlerPose &p, const QRectF &uv, Vertex *v, quint32 *idx, quint32 base)
{
    const float rad = p.tilt * float(M_PI) / 180.f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    const bool legged = p.gait == Gait::Crab || p.gait == Gait::Walker;
    // Dug in: squashed down into the sand; a snail pulls in toward its shell
    const float squash = p.gait == Gait::Snail ? 1.f - 0.35f * p.buried : 1.f - 0.7f * p.buried;
    const float pinch = p.gait == Gait::Snail ? 1.f - 0.25f * p.buried : 1.f;
    const float bob = legged ? std::abs(std::sin(p.phase * 6.f)) * 0.03f * p.h : 0.f;
    const float stretch = p.gait == Gait::Snail ? 1.f + 0.05f * std::sin(p.phase * 3.f) : 1.f;
    for (int r = 0; r <= kRows; ++r) {
        const float vv = float(r) / kRows;   // 0 = top, 1 = feet
        for (int c = 0; c <= kSegments; ++c) {
            const float u = float(c) / kSegments;
            float lx = (u - 0.5f) * p.w * stretch * pinch;
            float ly = -(1.f - vv) * p.h * squash - bob * (1.f - vv);
            if (legged && vv > 0.72f) {
                // Legs: the lowest part of the drawing steps back and forth,
                // the two sides out of step
                const float leg = (vv - 0.72f) / 0.28f;
                const float side = u < 0.5f ? 0.f : float(M_PI);
                lx += std::sin(p.phase * 6.f + side + u * 9.f) * 0.05f * p.w * leg;
                ly -= std::max(0.f, std::sin(p.phase * 6.f + side + u * 9.f)) * 0.04f * p.h * leg;
            }
            lx *= p.facing;
            Vertex &out = *v++;
            out.x = p.x + lx * cs - ly * sn;
            out.y = p.bottom + lx * sn + ly * cs;
            out.u = float(uv.x() + u * uv.width());
            out.v = float(uv.y() + vv * uv.height());
            out.fog = 0.1f;
            out.alpha = 1.f - 0.25f * p.buried;
        }
    }
    const quint32 stride = kSegments + 1;
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kSegments; ++c) {
            const quint32 i0 = base + r * stride + c;
            const quint32 i1 = i0 + 1;
            const quint32 i2 = i0 + stride;
            const quint32 i3 = i2 + 1;
            *idx++ = i0; *idx++ = i2; *idx++ = i1;
            *idx++ = i1; *idx++ = i2; *idx++ = i3;
        }
    }
}

void writeQuad(QRectF rect, const QRectF &uv, float alpha, Vertex *v, quint32 *idx, quint32 base)
{
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            Vertex &out = *v++;
            out.x = float(c ? rect.right() : rect.left());
            out.y = float(r ? rect.bottom() : rect.top());
            out.u = float(c ? uv.right() : uv.left());
            out.v = float(r ? uv.bottom() : uv.top());
            out.fog = 0.f;
            out.alpha = alpha;
        }
    }
    const quint32 quad[6] = {0, 2, 1, 1, 2, 3};
    for (quint32 q : quad)
        *idx++ = base + q;
}

} // namespace FishMesh
