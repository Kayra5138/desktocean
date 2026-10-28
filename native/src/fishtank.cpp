#include "fishtank.h"
#include "fishlibrary.h"
#include "spritenode.h"

#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTime>
#include <QImageReader>
#include <QPainter>
#include <QProcess>
#include <QQmlFile>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// Atlas layout
constexpr int kPad = 8;          // transparent border so mipmaps don't bleed between sprites
constexpr int kAtlasWidth = 2048;
constexpr int kFoodCell = 32;
constexpr float kFoodSize = 10.f;
// How many copies of each starter fish go into a fresh library
constexpr int kStarterCopies[] = {2, 2, 2, 1};
// Decorations are sized by height: this fraction of the tank height at scale 1,
// unless that would make them wider than kDecorMaxWidth of the tank
constexpr float kDecorHeight = 0.3f;
constexpr float kDecorMaxWidth = 0.35f;

struct StarterDecor {
    const char *name;
    FishEntry::Kind kind;
    QList<Placement> placements;
};

// Where the starter plants and decorations go in a fresh tank
const QList<StarterDecor> &starterLayout()
{
    using K = FishEntry::Kind;
    static const QList<StarterDecor> layout = {
        {"seaweed-tall", K::Plant, {{0.06, 0.93, 1.0, false, false}, {0.71, 0.95, 0.85, false, true}}},
        {"seaweed-bushy", K::Plant, {{0.33, 0.94, 0.9, false, false}, {0.93, 0.92, 0.7, false, true}}},
        {"rock", K::Decor, {{0.2, 0.98, 0.55, true, false}, {0.82, 0.93, 0.4, false, true}}},
        {"coral", K::Decor, {{0.52, 0.93, 0.7, false, false}}},
    };
    return layout;
}

// Smooth 0..1 ramp between a and b
double smoothstep(double a, double b, double x)
{
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

QString statePath()
{
    // Outside the fish directory on purpose: the tank reloads when that changes
    return QFileInfo(FishLibrary::directory()).dir().filePath(QStringLiteral("state.json"));
}

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

    // Friendships take hours to grow; keep them across restarts
    m_saveTimer.setInterval(60 * 1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &FishTank::saveState);
    m_saveTimer.start();

    checkHealth();
}

FishTank::~FishTank()
{
    saveState();
    dc_sim_free(m_sim);
}

void FishTank::setFollowTimeOfDay(bool follow)
{
    if (follow == m_followTimeOfDay)
        return;
    m_followTimeOfDay = follow;
    updateDaylight();
    Q_EMIT followTimeOfDayChanged();
}

void FishTank::setHourOverride(qreal hour)
{
    if (qFuzzyCompare(hour + 1, m_hourOverride + 1))
        return;
    m_hourOverride = hour;
    updateDaylight();
    Q_EMIT hourOverrideChanged();
}

void FishTank::updateDaylight()
{
    double d = 1;
    if (m_followTimeOfDay) {
        const QTime now = QTime::currentTime();
        const double hour = m_hourOverride >= 0 ? std::fmod(m_hourOverride, 24.0)
                                                : now.hour() + now.minute() / 60.0 + now.second() / 3600.0;
        // Dawn 5:30-7:30, dusk 18:30-20:30
        d = smoothstep(5.5, 7.5, hour) * (1 - smoothstep(18.5, 20.5, hour));
    }
    dc_sim_set_daylight(m_sim, float(d));
    if (std::abs(d - m_daylight) < 0.0005)
        return;
    m_daylight = d;
    update();
    Q_EMIT daylightChanged();
}

QString FishTank::stats() const
{
    double hunger = 0;
    for (const DcFish &f : m_fish)
        hunger += f.hunger;
    int friends = 0, rivals = 0;
    const uint32_t n = dc_sim_relations(m_sim, nullptr, 0);
    std::vector<DcRelation> rel(n);
    dc_sim_relations(m_sim, rel.data(), n);
    for (const DcRelation &r : rel) {
        friends += r.affinity > DC_FRIEND;
        rivals += r.affinity < DC_RIVAL;
    }
    return QStringLiteral("Hunger: %1%  Daylight: %2%\nFriends: %3  Rivals: %4")
        .arg(m_fish.empty() ? 0 : qRound(hunger / m_fish.size() * 100))
        .arg(qRound(m_daylight * 100))
        .arg(friends)
        .arg(rivals);
}

void FishTank::loadState()
{
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    std::vector<DcRelation> list;
    const QJsonArray rel = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("relations")).toArray();
    for (const QJsonValue &v : rel) {
        const QJsonObject o = v.toObject();
        list.push_back({o.value(QStringLiteral("a")).toString().toULongLong(),
                        o.value(QStringLiteral("b")).toString().toULongLong(),
                        float(o.value(QStringLiteral("affinity")).toDouble())});
    }
    dc_sim_set_relations(m_sim, list.data(), uint32_t(list.size()));
}

void FishTank::saveState() const
{
    if (!m_stateLoaded)
        return;   // never overwrite saved friendships before they were read
    const uint32_t n = dc_sim_relations(m_sim, nullptr, 0);
    std::vector<DcRelation> rel(n);
    rel.resize(std::min(n, dc_sim_relations(m_sim, rel.data(), n)));

    // Raw relations for the tank, plus friend/rival pairs by library fish for the editor
    QJsonArray relations, friends, rivals;
    for (const DcRelation &r : rel) {
        relations.append(QJsonObject{{QStringLiteral("a"), QString::number(r.a)},
                                     {QStringLiteral("b"), QString::number(r.b)},
                                     {QStringLiteral("affinity"), double(r.affinity)}});
        const QString a = m_specOwner.value(r.a), b = m_specOwner.value(r.b);
        if (a.isEmpty() || b.isEmpty() || a == b)
            continue;
        if (r.affinity > DC_FRIEND)
            friends.append(QJsonArray{a, b});
        else if (r.affinity < DC_RIVAL)
            rivals.append(QJsonArray{a, b});
    }
    const QJsonObject root{{QStringLiteral("relations"), relations},
                           {QStringLiteral("friends"), friends},
                           {QStringLiteral("rivals"), rivals}};
    QSaveFile file(statePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
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

void FishTank::setStarterDecor(const QList<QUrl> &urls)
{
    if (urls == m_starterDecor)
        return;
    m_starterDecor = urls;
    if (isComponentComplete() && !FishLibrary::decorSeeded())
        loadLibrary(false);
    Q_EMIT starterDecorChanged();
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
    updateDaylight();
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
    // The clock moves slowly; once a second is plenty
    if (int(m_time) != int(m_time + m_clock.nsecsElapsed() / 1e9))
        updateDaylight();
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
        FishEntry meta;
        meta.name = name;
        meta.count = kStarterCopies[i % std::size(kStarterCopies)];
        QString error;
        if (!FishLibrary::add({img, {}}, meta, &error))
            qWarning("Desktocean: cannot seed fish library: %s", qPrintable(error));
    }
}

void FishTank::seedDecor()
{
    if (m_starterDecor.isEmpty())
        return;
    for (const QUrl &url : std::as_const(m_starterDecor)) {
        // "decor-seaweed-tall.svg" -> "seaweed-tall"
        const QString base = QFileInfo(url.path()).baseName().section(QLatin1Char('-'), 1);
        const auto &layout = starterLayout();
        const auto it = std::find_if(layout.begin(), layout.end(), [&](const StarterDecor &d) { return base == QLatin1String(d.name); });
        if (it == layout.end())
            continue;
        QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
        reader.setScaledSize(reader.size().scaled(400, 400, Qt::KeepAspectRatio));
        const QImage img = reader.read();
        if (img.isNull()) {
            qWarning("Desktocean: cannot load starter decoration %s: %s", qPrintable(url.toString()),
                     qPrintable(reader.errorString()));
            continue;
        }
        FishEntry meta;
        meta.kind = it->kind;
        meta.name = QString(base).replace(QLatin1Char('-'), QLatin1Char(' '));
        meta.name[0] = meta.name[0].toUpper();
        meta.placements = it->placements;
        QString error;
        if (!FishLibrary::add({img, {}}, meta, &error))
            qWarning("Desktocean: cannot seed decorations: %s", qPrintable(error));
    }
    FishLibrary::markDecorSeeded();
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
    if (!FishLibrary::decorSeeded())
        seedDecor();
    watchLibrary();

    QString error;
    const QList<FishEntry> entries = FishLibrary::load(&error);
    if (!error.isEmpty())
        qWarning("Desktocean: cannot read fish library: %s", qPrintable(error));

    // Keep the atlas manageable when the collection grows
    const int maxSide = entries.size() > 32 ? 256 : 400;

    QList<QImage> images;
    std::vector<SpriteInfo> sprites;
    std::vector<DcFishSpec> specs;
    std::vector<DecorItem> decor;
    QHash<quint64, QString> owners;
    for (const FishEntry &e : entries) {
        const bool isFish = e.kind == FishEntry::Kind::Fish;
        if (isFish ? e.count <= 0 : e.placements.isEmpty())
            continue;
        QImage img(e.path());
        if (img.isNull()) {
            qWarning("Desktocean: cannot load %s", qPrintable(e.path()));
            continue;
        }
        if (std::max(img.width(), img.height()) > maxSide)
            img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        SpriteInfo info;
        info.aspect = float(img.height()) / float(img.width());
        info.style = &FishMesh::style(e.style);
        if (!e.wiggle.isEmpty())
            info.weights = FishMesh::weightsFromMask(QImage(e.wigglePath()));
        const auto sprite = uint32_t(sprites.size());
        sprites.push_back(std::move(info));

        const float aspect = float(img.height()) / float(img.width());
        images << img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (!isFish) {
            const float sway = e.kind == FishEntry::Kind::Plant ? float(e.sway) : 0.f;
            for (const Placement &p : e.placements)
                decor.push_back({sprite, sway, float(p.x * 17.0 + p.y * 5.0), p});
            continue;
        }
        for (int k = 0; k < e.count; ++k) {
            const auto id = quint64(qHash(e.id, size_t(k)));
            specs.push_back({id, sprite, aspect, float(e.scale)});
            owners.insert(id, e.id);
        }
    }

    m_sprites = std::move(sprites);
    // Back to front by where they stand: lower on the screen is closer
    std::sort(decor.begin(), decor.end(), [](const DecorItem &a, const DecorItem &b) { return a.placement.y < b.placement.y; });
    m_decor = std::move(decor);
    rebuildAtlas(images);
    dc_sim_set_population(m_sim, specs.data(), uint32_t(specs.size()), animateNew && m_libraryLoaded);
    m_specOwner = owners;
    m_libraryLoaded = true;
    if (!m_stateLoaded) {
        // After the population, so relations of fish that left are dropped
        loadState();
        m_stateLoaded = true;
    }
    pullState();
    checkHealth();
    update();
}

void FishTank::rebuildAtlas(const QList<QImage> &images)
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
    for (const QImage &img : images)
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
    for (int i = 0; i < images.size(); ++i) {
        painter.drawImage(placed[i].topLeft(), images[i]);
        m_sprites[i].uv = normalised(placed[i]);
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
    auto *node = static_cast<SpriteNode *>(oldNode);
    if (!node)
        node = new SpriteNode;

    if (m_atlasDirty && !m_atlas.isNull()) {
        m_atlasDirty = false;
        node->setAtlas(window(), m_atlas);
    }
    node->setFogColor(m_fogColor);
    node->setLight(float(0.35 + 0.65 * m_daylight));

    // Fish whose sprite is not in the atlas (yet) are skipped
    std::vector<int> order;
    if (node->hasAtlas()) {
        for (int i = 0; i < int(m_fish.size()); ++i) {
            if (m_fish[i].sprite < m_sprites.size())
                order.push_back(i);
        }
    }
    const int nFish = int(order.size());
    const int nFood = node->hasAtlas() ? int(m_food.size()) : 0;
    const int nDecor = node->hasAtlas() ? int(std::count_if(m_decor.begin(), m_decor.end(), [&](const DecorItem &d) {
                                               return d.sprite < m_sprites.size();
                                           }))
                                         : 0;

    quint32 *idx = nullptr;
    FishMesh::Vertex *v = node->reserve(
        nFish * FishMesh::kVertsPerFish + nFood * 4 + nDecor * FishMesh::kVertsPerDecor,
        nFish * FishMesh::kIndicesPerFish + nFood * 6 + nDecor * FishMesh::kIndicesPerDecor, &idx);
    quint32 base = 0;

    // Draw order: decorations behind the fish, fish back to front, food,
    // then decorations in front of the fish
    auto writeDecorLayer = [&](bool front) {
        if (nDecor == 0)
            return;
        for (const DecorItem &d : m_decor) {
            if (d.placement.front != front || d.sprite >= m_sprites.size())
                continue;
            const float aspect = m_sprites[d.sprite].aspect;
            const float scale = float(d.placement.scale);
            const float h = std::min(float(height()) * kDecorHeight, float(width()) * kDecorMaxWidth * aspect) * scale;
            const FishMesh::DecorPose pose{float(d.placement.x * width()), float(d.placement.y * height()), h / aspect, h,
                                           d.sway, d.phase, d.placement.flip, front ? 0.f : 0.15f};
            FishMesh::writeDecor(pose, m_sprites[d.sprite].uv, float(m_time), v, idx, base);
            v += FishMesh::kVertsPerDecor;
            idx += FishMesh::kIndicesPerDecor;
            base += FishMesh::kVertsPerDecor;
        }
    };
    writeDecorLayer(false);

    // Back to front: one draw call, blending order comes from vertex order
    std::sort(order.begin(), order.end(), [&](int a, int b) { return m_fish[a].depth > m_fish[b].depth; });
    for (int i : order) {
        const DcFish &f = m_fish[i];
        const SpriteInfo &sprite = m_sprites[f.sprite];
        const FishMesh::Pose pose{f.x, f.y, f.w, f.h, f.facing, f.phase, f.amplitude, f.tilt, f.depth};
        FishMesh::writeFish(pose, sprite.uv, *sprite.style, sprite.weights, v, idx, base);
        v += FishMesh::kVertsPerFish;
        idx += FishMesh::kIndicesPerFish;
        base += FishMesh::kVertsPerFish;
    }
    const float half = kFoodSize * 0.5f;
    for (int i = 0; i < nFood; ++i) {
        const DcFood &p = m_food[i];
        FishMesh::writeQuad(QRectF(p.x - half, p.y - half, kFoodSize, kFoodSize), m_foodUv, p.opacity, v, idx, base);
        v += 4;
        idx += 6;
        base += 4;
    }
    writeDecorLayer(true);

    node->commit();
    return node;
}
