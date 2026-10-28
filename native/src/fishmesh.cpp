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

    for (int r = 0; r <= kRows; ++r) {
        const float vv = float(r) / kRows;
        for (int c = 0; c <= kSegments; ++c) {
            const float u = float(c) / kSegments;
            float bend = st.floor + (1.f - st.floor) * std::pow(1.f - u, st.power);
            if (masked)
                bend *= weights[size_t(r) * (kSegments + 1) + c];
            const float wave = std::sin(ph - u * st.waveLength);

            const float px = u * w + std::abs(wave) * bend * w * 0.025f;
            const float py = (vv - 0.5f) * h * squash + 0.5f * h + wave * amp * h * bend - headSway * u + bob;

            // Around the centre: mirror for facing, then pitch, then place
            const float cx = (px - w * 0.5f) * flip;
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
