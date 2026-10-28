#include "swimpreview.h"
#include "drawingcanvas.h"
#include "spritenode.h"

#include <QPainter>
#include <QVariantMap>

#include <cmath>

namespace {
constexpr int kPad = 8;
constexpr int kMaxSide = 400;
}

SwimPreview::SwimPreview(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(150);
    connect(&m_refresh, &QTimer::timeout, this, &SwimPreview::refreshSprite);
    m_timer.setInterval(33);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SwimPreview::tick);
}

void SwimPreview::setCanvas(DrawingCanvas *canvas)
{
    if (canvas == m_canvas)
        return;
    if (m_canvas)
        disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (m_canvas)
        connect(m_canvas, &DrawingCanvas::contentChanged, &m_refresh, qOverload<>(&QTimer::start));
    refreshSprite();
    Q_EMIT canvasChanged();
}

void SwimPreview::setSwimStyle(const QString &style)
{
    if (style == m_style)
        return;
    m_style = style;
    update();
    Q_EMIT swimStyleChanged();
}

QVariantList SwimPreview::styles() const
{
    QVariantList list;
    for (const FishMesh::Style &s : FishMesh::styles())
        list << QVariantMap{{QStringLiteral("id"), s.id}, {QStringLiteral("label"), s.label}};
    return list;
}

void SwimPreview::refreshSprite()
{
    m_atlas = {};
    if (m_canvas) {
        const FishDrawing d = m_canvas->drawing();
        const QRect box = FishLibrary::contentBox(d.sprite, 0);
        if (!box.isNull()) {
            QImage sprite = d.sprite.copy(box);
            if (std::max(sprite.width(), sprite.height()) > kMaxSide)
                sprite = sprite.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_aspect = float(sprite.height()) / float(sprite.width());
            m_weights = d.wiggle.isNull() ? std::vector<float>() : FishMesh::weightsFromMask(d.wiggle.copy(box));

            m_atlas = QImage(sprite.width() + 2 * kPad, sprite.height() + 2 * kPad, QImage::Format_ARGB32_Premultiplied);
            m_atlas.fill(Qt::transparent);
            QPainter p(&m_atlas);
            p.drawImage(kPad, kPad, sprite);
            p.end();
            m_uv = QRectF(double(kPad) / m_atlas.width(), double(kPad) / m_atlas.height(),
                          double(sprite.width()) / m_atlas.width(), double(sprite.height()) / m_atlas.height());
            m_atlasDirty = true;
        }
    }
    updateTimer();
    update();
}

void SwimPreview::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        updateTimer();
}

void SwimPreview::updateTimer()
{
    const bool run = isVisible() && window() && !m_atlas.isNull();
    if (run && !m_timer.isActive()) {
        m_clock.start();
        m_timer.start();
    } else if (!run) {
        m_timer.stop();
    }
}

void SwimPreview::tick()
{
    const float dt = std::min(0.1f, float(m_clock.nsecsElapsed() / 1e9));
    m_clock.start();
    m_time += dt;

    // Cruise slowly back and forth across the preview
    const float range = float(width()) * 0.18f;
    const float x = std::sin(m_time * 0.3f) * range;
    const float vx = (x - m_x) / std::max(dt, 1e-3f);
    m_x = x;
    m_speed = std::abs(vx);
    const float want = vx >= 0 ? 1.f : -1.f;
    const float step = 6.f * dt;
    m_facing = std::abs(want - m_facing) <= step ? want : m_facing + step * (want - m_facing > 0 ? 1.f : -1.f);
    m_phase = std::fmod(m_phase + dt * (2.5f + std::max(m_speed, 40.f) * 0.07f), float(M_PI) * 128.f);
    update();
}

QSGNode *SwimPreview::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<SpriteNode *>(oldNode);
    if (!node)
        node = new SpriteNode;
    if (m_atlasDirty) {
        m_atlasDirty = false;
        node->setAtlas(window(), m_atlas);
    }

    const bool draw = !m_atlas.isNull() && node->hasAtlas() && width() > 0 && height() > 0;
    quint32 *idx = nullptr;
    FishMesh::Vertex *v = node->reserve(draw ? FishMesh::kVertsPerFish : 0, draw ? FishMesh::kIndicesPerFish : 0, &idx);
    if (draw) {
        // As large as fits, leaving room for the tail swing and bobbing
        float w = float(width()) * 0.55f;
        float h = w * m_aspect;
        if (h > height() * 0.6f) {
            h = float(height()) * 0.6f;
            w = h / m_aspect;
        }
        const float amplitude = 0.04f + 0.08f * std::min(1.f, m_speed / 60.f);
        const FishMesh::Pose pose{float(width()) / 2 + m_x, float(height()) / 2, w, h, m_facing, m_phase, amplitude, 0.f, 0.f};
        FishMesh::writeFish(pose, m_uv, FishMesh::style(m_style), m_weights, v, idx, 0);
    }
    node->commit();
    return node;
}
