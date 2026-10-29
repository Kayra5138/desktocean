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

// Plants and decorations: a grid standing on (x, bottom), bending sideways in
// the current more and more toward the top. sway = 0 keeps it still.
constexpr int kDecorCols = 4;
constexpr int kDecorRows = 12;
constexpr int kVertsPerDecor = (kDecorCols + 1) * (kDecorRows + 1);
constexpr int kIndicesPerDecor = kDecorCols * kDecorRows * 6;

struct DecorPose {
    float x, bottom;   // middle of the bottom edge
    float w, h;
    float sway;        // 0 = still, 1 = gentle current
    float phase;       // per-placement offset so plants don't move in lockstep
    bool flip;
    float fog;
};

void writeDecor(const DecorPose &pose, const QRectF &uv, float time, Vertex *v, quint32 *idx, quint32 base);

// Bottom creatures: stand on (x, bottom), legs (the lowest quarter of the
// drawing) step with `phase`, the body follows the ground's tilt and sinks
// into the sand / its shell with `buried`. Same vertex layout as a fish.
enum class Gait { Crab, Snail, Star, Walker };
Gait gaitFromId(const QString &id);
uint32_t gaitIndex(Gait gait);   // DC_GAIT_*

struct CrawlerPose {
    float x, bottom;
    float w, h;
    float facing;
    float phase;
    float tilt;     // degrees
    float buried;   // 0 .. 1
    Gait gait;
};

void writeCrawler(const CrawlerPose &pose, const QRectF &uv, Vertex *v, quint32 *idx, quint32 base);

// Axis-aligned sprite, e.g. a food pellet
void writeQuad(QRectF rect, const QRectF &uv, float alpha, Vertex *v, quint32 *idx, quint32 base);

} // namespace FishMesh
