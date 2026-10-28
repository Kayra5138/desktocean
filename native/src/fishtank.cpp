#include "fishtank.h"
#include "fishlibrary.h"

#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QQmlFile>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QStandardPaths>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>

namespace {

// Fish mesh resolution: columns along the spine, rows across the body.
constexpr int kSegments = 20;
constexpr int kRows = 2;
constexpr int kVertsPerFish = (kSegments + 1) * (kRows + 1);
constexpr int kIndicesPerFish = kSegments * kRows * 6;

// Atlas layout
constexpr int kPad = 8;          // transparent border so mipmaps don't bleed between sprites
constexpr int kAtlasWidth = 2048;
constexpr int kFoodCell = 32;
// How many copies of each starter fish go into a fresh library
constexpr int kStarterCopies[] = {2, 2, 2, 1};
constexpr float kFoodSize = 10.f;

struct Vertex {
    float x, y;
    float u, v;
    float fog;
    float alpha;
};

const QSGGeometry::AttributeSet &vertexAttributes()
{
    static const QSGGeometry::Attribute attrs[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 1, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        QSGGeometry::Attribute::createWithAttributeType(3, 1, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {4, sizeof(Vertex), attrs};
    return set;
}

class SpriteMaterial : public QSGMaterial
{
public:
    SpriteMaterial() { setFlag(Blending); }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType t;
        return &t;
    }

    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    int compare(const QSGMaterial *other) const override
    {
        const auto *o = static_cast<const SpriteMaterial *>(other);
        if (texture != o->texture)
            return texture < o->texture ? -1 : 1;
        if (fogColor != o->fogColor)
            return fogColor.rgba() < o->fogColor.rgba() ? -1 : 1;
        return 0;
    }

    QSGTexture *texture = nullptr;
    QColor fogColor;
};

class SpriteShader : public QSGMaterialShader
{
public:
    SpriteShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/desktocean/shaders/fishbatch.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/desktocean/shaders/fishbatch.frag.qsb"));
    }

    // Uniform block layout (std140): mat4 @0, float opacity @64, vec4 fogColor @80
    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override
    {
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= 96);
        bool changed = false;
        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data(), m.constData(), 64);
            changed = true;
        }
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(buf->data() + 64, &opacity, 4);
            changed = true;
        }
        const auto *mat = static_cast<SpriteMaterial *>(newMaterial);
        const auto *old = static_cast<SpriteMaterial *>(oldMaterial);
        if (!old || old->fogColor != mat->fogColor) {
            const float c[4] = {float(mat->fogColor.redF()), float(mat->fogColor.greenF()),
                                float(mat->fogColor.blueF()), float(mat->fogColor.alphaF())};
            std::memcpy(buf->data() + 80, c, 16);
            changed = true;
        }
        return changed;
    }

    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture, QSGMaterial *newMaterial,
                            QSGMaterial *) override
    {
        if (binding != 1)
            return;
        QSGTexture *t = static_cast<SpriteMaterial *>(newMaterial)->texture;
        if (t)
            t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = t;
    }
};

QSGMaterialShader *SpriteMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new SpriteShader;
}

class TankNode : public QSGGeometryNode
{
public:
    TankNode()
        : geometry(vertexAttributes(), 0, 0, QSGGeometry::UnsignedIntType)
    {
        geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        setGeometry(&geometry);
        setMaterial(&material);
    }

    QSGGeometry geometry;
    SpriteMaterial material;
    std::unique_ptr<QSGTexture> texture;
};

QImage makeFoodSprite()
{
    QImage img(kFoodCell, kFoodCell, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x7a, 0x3f, 0x19), 4));
    p.setBrush(QColor(0xc8, 0x74, 0x3a));
    p.drawEllipse(QRectF(3, 3, kFoodCell - 6, kFoodCell - 6));
    return img;
}

// Writes one fish as a bent grid mesh. Mirrors what the old per-fish vertex
// shader did: the tail (u = 0) swings most, the head (u = 1) barely moves.
void writeFish(const DcFish &f, const QRectF &uv, Vertex *v, quint32 *idx, quint32 base)
{
    const float w = f.w, h = f.h;
    // Keep a sliver of width mid-turn so the sprite never collapses to a line
    const float flip = std::abs(f.facing) < 0.08f ? (f.facing < 0 ? -0.08f : 0.08f) : f.facing;
    const float rad = f.tilt * float(M_PI) / 180.f;
    const float cs = std::cos(rad), sn = std::sin(rad);
    const float sinPhase = std::sin(f.phase);

    for (int r = 0; r <= kRows; ++r) {
        const float vv = float(r) / kRows;
        for (int c = 0; c <= kSegments; ++c) {
            const float u = float(c) / kSegments;
            const float tailWeight = std::pow(1.f - u, 1.7f);
            const float wave = std::sin(f.phase - u * 4.2f);

            float px = u * w + std::abs(wave) * tailWeight * w * 0.025f;
            float py = vv * h + wave * f.amplitude * h * tailWeight - sinPhase * f.amplitude * 0.12f * h * u;

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

void writeFood(const DcFood &p, const QRectF &uv, Vertex *v, quint32 *idx, quint32 base)
{
    const float half = kFoodSize * 0.5f;
    const float xs[2] = {p.x - half, p.x + half};
    const float ys[2] = {p.y - half, p.y + half};
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            Vertex &out = *v++;
            out.x = xs[c];
            out.y = ys[r];
            out.u = float(c ? uv.right() : uv.left());
            out.v = float(r ? uv.bottom() : uv.top());
            out.fog = 0.f;
            out.alpha = p.opacity;
        }
    }
    const quint32 quad[6] = {0, 2, 1, 1, 2, 3};
    for (quint32 q : quad)
        *idx++ = base + q;
}

} // namespace

FishTank::FishTank(QQuickItem *parent)
    : QQuickItem(parent)
    , m_sim(dc_sim_new(QRandomGenerator::global()->generate64()))
{
    setFlag(ItemHasContents);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &FishTank::tick);

    // The editor writes a sprite and then the manifest; wait for both to land.
    m_reloadDebounce.setSingleShot(true);
    m_reloadDebounce.setInterval(250);
    connect(&m_reloadDebounce, &QTimer::timeout, this, [this] { loadLibrary(true); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadDebounce, qOverload<>(&QTimer::start));

    checkHealth();
}

FishTank::~FishTank()
{
    dc_sim_free(m_sim);
}

QString FishTank::coreVersion() const
{
    return QString::fromUtf8(dc_sim_version());
}

QString FishTank::libraryPath() const
{
    return FishLibrary::directory();
}

void FishTank::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    updateTimer();
    Q_EMIT runningChanged();
}

void FishTank::setFps(int fps)
{
    fps = std::clamp(fps, 1, 120);
    if (fps == m_fps)
        return;
    m_fps = fps;
    m_timer.setInterval(1000 / m_fps);
    Q_EMIT fpsChanged();
}

void FishTank::setStarterFish(const QList<QUrl> &urls)
{
    if (urls == m_starterFish)
        return;
    m_starterFish = urls;
    if (isComponentComplete() && !FishLibrary::exists())
        loadLibrary(false);
    Q_EMIT starterFishChanged();
}

void FishTank::setFogColor(const QColor &color)
{
    if (color == m_fogColor)
        return;
    m_fogColor = color;
    update();
    Q_EMIT fogColorChanged();
}

void FishTank::feed(qreal x)
{
    dc_sim_feed(m_sim, float(x));
    pullState();
    update();
}

void FishTank::feedAtCursor()
{
    feed(cursorInItem().x());
}

void FishTank::clearFood()
{
    dc_sim_clear_food(m_sim);
    pullState();
    update();
}

bool FishTank::openEditor()
{
    // plasmashell's PATH often lacks ~/.local/bin, where install.sh puts the editor
    const QString name = QStringLiteral("desktocean-editor");
    QString exe = QStandardPaths::findExecutable(name);
    if (exe.isEmpty())
        exe = QStandardPaths::findExecutable(name, {QDir::homePath() + QStringLiteral("/.local/bin")});
    if (exe.isEmpty()) {
        qWarning("Desktocean: %s is not installed", qPrintable(name));
        return false;
    }
    return QProcess::startDetached(exe, {});
}

void FishTank::componentComplete()
{
    QQuickItem::componentComplete();
    loadLibrary(false);
    updateTimer();
}

void FishTank::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    dc_sim_resize(m_sim, float(newGeometry.width()), float(newGeometry.height()));
    pullState();
    update();
    updateTimer();
}

void FishTank::updateTimer()
{
    const bool shouldRun = m_running && m_healthy && isComponentComplete() && width() > 0 && height() > 0;
    if (shouldRun && !m_timer.isActive()) {
        m_timer.start(1000 / m_fps);
        m_clock.start();
    } else if (!shouldRun && m_timer.isActive()) {
        m_timer.stop();
    }
}

QPointF FishTank::cursorInItem() const
{
    const QPointF p = mapFromGlobal(QCursor::pos());
    const bool inside = p.x() >= 0 && p.y() >= 0 && p.x() < width() && p.y() < height();
    return inside ? p : QPointF(-1, -1);
}

void FishTank::tick()
{
    const qreal dt = std::clamp(m_clock.nsecsElapsed() / 1e9, 0.0, 0.1);
    m_clock.start();
    m_time += dt;
    m_pointer = cursorInItem();
    dc_sim_step(m_sim, float(dt), float(m_pointer.x()), float(m_pointer.y()));
    pullState();
    checkHealth();
    update();
    Q_EMIT timeChanged();
}

void FishTank::pullState()
{
    const size_t oldFish = m_fish.size();
    const size_t oldFood = m_food.size();
    const bool oldLure = m_lureActive;

    uint32_t n = dc_sim_fish(m_sim, nullptr, 0);
    m_fish.resize(n);
    m_fish.resize(std::min(n, dc_sim_fish(m_sim, m_fish.data(), n)));

    n = dc_sim_food(m_sim, nullptr, 0);
    m_food.resize(n);
    m_food.resize(std::min(n, dc_sim_food(m_sim, m_food.data(), n)));

    m_lureActive = dc_sim_lure_active(m_sim);

    if (m_fish.size() != oldFish)
        Q_EMIT fishCountChanged();
    if (m_food.size() != oldFood)
        Q_EMIT foodCountChanged();
    if (m_lureActive != oldLure)
        Q_EMIT lureActiveChanged();
}

void FishTank::checkHealth()
{
    const bool ok = dc_sim_ok(m_sim);
    if (ok == m_healthy)
        return;
    m_healthy = ok;
    if (!ok)
        qWarning("Desktocean: simulation core stopped after an internal error; the aquarium is frozen");
    updateTimer();
    Q_EMIT healthyChanged();
}

void FishTank::seedLibrary()
{
    for (int i = 0; i < m_starterFish.size(); ++i) {
        const QUrl &url = m_starterFish[i];
        QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
        reader.setScaledSize(QSize(400, 240));
        const QImage img = reader.read();
        if (img.isNull()) {
            qWarning("Desktocean: cannot load starter fish %s: %s", qPrintable(url.toString()),
                     qPrintable(reader.errorString()));
            continue;
        }
        // "fish-orange.svg" -> "Orange"
        QString name = QFileInfo(url.path()).baseName().section(QLatin1Char('-'), -1);
        if (!name.isEmpty())
            name[0] = name[0].toUpper();
        const int copies = kStarterCopies[i % std::size(kStarterCopies)];
        QString error;
        if (!FishLibrary::add(img, name, 1.0, copies, &error))
            qWarning("Desktocean: cannot seed fish library: %s", qPrintable(error));
    }
}

void FishTank::watchLibrary()
{
    const QString dir = FishLibrary::directory();
    QDir().mkpath(dir);
    if (!m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
}

void FishTank::loadLibrary(bool animateNew)
{
    if (!FishLibrary::exists() && !m_starterFish.isEmpty())
        seedLibrary();
    watchLibrary();

    QString error;
    const QList<FishEntry> entries = FishLibrary::load(&error);
    if (!error.isEmpty())
        qWarning("Desktocean: cannot read fish library: %s", qPrintable(error));

    // Keep the atlas manageable when the collection grows
    const int maxSide = entries.size() > 32 ? 256 : 400;

    QList<QImage> sprites;
    std::vector<DcFishSpec> specs;
    for (const FishEntry &e : entries) {
        if (e.count <= 0)
            continue;
        QImage img(e.path());
        if (img.isNull()) {
            qWarning("Desktocean: cannot load %s", qPrintable(e.path()));
            continue;
        }
        if (std::max(img.width(), img.height()) > maxSide)
            img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const auto sprite = uint32_t(sprites.size());
        const float aspect = float(img.height()) / float(img.width());
        sprites << img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int k = 0; k < e.count; ++k)
            specs.push_back({quint64(qHash(e.id, size_t(k))), sprite, aspect, float(e.scale)});
    }

    rebuildAtlas(sprites);
    dc_sim_set_population(m_sim, specs.data(), uint32_t(specs.size()), animateNew && m_libraryLoaded);
    m_libraryLoaded = true;
    pullState();
    checkHealth();
    update();
}

void FishTank::rebuildAtlas(const QList<QImage> &sprites)
{
    // Shelf packing: left to right, new shelf when the row is full
    std::vector<QRect> placed;
    int x = kPad, y = kPad, shelf = 0;
    auto place = [&](QSize size) {
        if (x + size.width() + kPad > kAtlasWidth) {
            x = kPad;
            y += shelf + 2 * kPad;
            shelf = 0;
        }
        placed.emplace_back(QPoint(x, y), size);
        x += size.width() + 2 * kPad;
        shelf = std::max(shelf, size.height());
    };
    for (const QImage &img : sprites)
        place(img.size());
    place(QSize(kFoodCell, kFoodCell));
    const int atlasH = y + shelf + kPad;

    QImage atlas(kAtlasWidth, atlasH, QImage::Format_ARGB32_Premultiplied);
    atlas.fill(Qt::transparent);
    QPainter painter(&atlas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);

    auto normalised = [&](const QRect &r) {
        return QRectF(double(r.x()) / kAtlasWidth, double(r.y()) / atlasH, double(r.width()) / kAtlasWidth,
                      double(r.height()) / atlasH);
    };
    m_spriteUv.clear();
    for (int i = 0; i < sprites.size(); ++i) {
        painter.drawImage(placed[i].topLeft(), sprites[i]);
        m_spriteUv.push_back(normalised(placed[i]));
    }
    painter.drawImage(placed.back().topLeft(), makeFoodSprite());
    m_foodUv = normalised(placed.back());
    painter.end();

    m_atlas = atlas;
    m_atlasDirty = true;
    update();
}

QSGNode *FishTank::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<TankNode *>(oldNode);
    if (!node)
        node = new TankNode;

    if (m_atlasDirty && !m_atlas.isNull()) {
        m_atlasDirty = false;
        QSGTexture *t = window()->createTextureFromImage(
            m_atlas, QQuickWindow::CreateTextureOptions(QQuickWindow::TextureHasAlphaChannel | QQuickWindow::TextureHasMipmaps));
        t->setFiltering(QSGTexture::Linear);
        t->setMipmapFiltering(QSGTexture::Linear);
        node->material.texture = t;
        node->texture.reset(t);
        node->markDirty(QSGNode::DirtyMaterial);
    }
    if (node->material.fogColor != m_fogColor) {
        node->material.fogColor = m_fogColor;
        node->markDirty(QSGNode::DirtyMaterial);
    }

    // Fish whose sprite is not in the atlas (yet) are skipped
    std::vector<int> order;
    if (node->material.texture) {
        for (int i = 0; i < int(m_fish.size()); ++i) {
            if (m_fish[i].sprite < m_spriteUv.size())
                order.push_back(i);
        }
    }
    const int nFish = int(order.size());
    const int nFood = node->material.texture ? int(m_food.size()) : 0;
    const int vertexCount = nFish * kVertsPerFish + nFood * 4;
    const int indexCount = nFish * kIndicesPerFish + nFood * 6;

    QSGGeometry &g = node->geometry;
    if (g.vertexCount() != vertexCount || g.indexCount() != indexCount)
        g.allocate(vertexCount, indexCount);

    auto *v = static_cast<Vertex *>(g.vertexData());
    quint32 *idx = g.indexDataAsUInt();
    quint32 base = 0;

    // Back to front: one draw call, blending order comes from vertex order
    std::sort(order.begin(), order.end(), [&](int a, int b) { return m_fish[a].depth > m_fish[b].depth; });
    for (int i : order) {
        writeFish(m_fish[i], m_spriteUv[m_fish[i].sprite], v, idx, base);
        v += kVertsPerFish;
        idx += kIndicesPerFish;
        base += kVertsPerFish;
    }
    for (int i = 0; i < nFood; ++i) {
        writeFood(m_food[i], m_foodUv, v, idx, base);
        v += 4;
        idx += 6;
        base += 4;
    }

    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
