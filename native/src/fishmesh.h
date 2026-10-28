#pragma once

#include <QImage>
#include <QRectF>
#include <QString>

#include <vector>

// How a flat fish drawing is bent into a swimming fish. Shared by the
// wallpaper (FishTank) and the editor's live preview so both move the same.
//
// A fish is a grid mesh over its sprite (head on the right, u = 1). Each
// vertex is displaced by a travelling wave whose strength along the body comes
// from the swim style, optionally scaled per vertex by a painted "wiggle"
// mask (grey 128 = normal, white = twice as much, black = rigid).
namespace FishMesh {

constexpr int kSegments = 24;   // columns along the spine
constexpr int kRows = 8;        // rows across the body
constexpr int kVertsPerFish = (kSegments + 1) * (kRows + 1);
constexpr int kIndicesPerFish = kSegments * kRows * 6;

struct Vertex {
    float x, y;
    float u, v;
    float fog;
    float alpha;
};

// Where and how a fish is right now (mirrors DcFish from the simulation)
struct Pose {
    float x, y;        // centre
    float w, h;
    float facing;      // -1 left .. 1 right
    float phase;       // tail-wave phase, radians
    float amplitude;   // tail swing, fraction of h
    float tilt;        // degrees
    float depth;       // 0 front .. 1 back
};

struct Style {
    QString id;
    QString label;
    float waveLength;   // waves along the body (radians over its length)
    float floor;        // minimum bend weight, > 0 makes the whole body undulate
    float power;        // how fast the bend fades toward the head
    float ampMul;
    float freqMul;
    float bob;          // vertical body bob, fraction of h
    float squash;       // vertical breathing, fraction of h
    bool glide;         // beat, then coast
};

const std::vector<Style> &styles();
// Falls back to the classic style for unknown ids
const Style &style(const QString &id);

// Per-vertex weights sampled from a wiggle mask; empty for a null mask (all 1)
std::vector<float> weightsFromMask(const QImage &mask);

void writeFish(const Pose &pose, const QRectF &uv, const Style &style, const std::vector<float> &weights,
               Vertex *v, quint32 *idx, quint32 base);

// Axis-aligned sprite, e.g. a food pellet
void writeQuad(QRectF rect, const QRectF &uv, float alpha, Vertex *v, quint32 *idx, quint32 base);

} // namespace FishMesh
