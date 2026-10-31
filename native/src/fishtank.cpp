#include "fishtank.h"
#include "fishlibrary.h"
#include "spritenode.h"
#include "sounds.h"
#include "tankhub.h"
#include "themes.h"

#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTime>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQmlFile>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QtLogging>

#include <QDateTime>

#include <algorithm>
#include <limits>
#include <numeric>
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
    QRectF opening = {};   // caves: fractions of the drawing, see scripts/make-starter-decor.py
    const char *style = nullptr;   // crawlers: gait
    int count = 1;                 // crawlers: how many
};

// A cave opening's shape inside its box: rounded top, straight sides down to
// the bottom (matches scripts/make-starter-decor.py)
QPainterPath caveArch(const QRectF &r)
{
    const qreal round = r.height() * 0.55;
    QPainterPath path;
    path.moveTo(r.left(), r.bottom());
    path.lineTo(r.left(), r.top() + round);
    path.arcTo(QRectF(r.left(), r.top(), r.width(), round * 2), 180, -180);
    path.lineTo(r.right(), r.bottom());
    path.closeSubpath();
    return path;
}

// Marker files for starter decorations added after the first release
QString seededMarker(const QString &name)
{
    return QDir(FishLibrary::directory()).filePath(QStringLiteral(".seeded-%1").arg(name));
}

// Where the starter plants and decorations go in a fresh tank
const QList<StarterDecor> &starterLayout()
{
    using K = FishEntry::Kind;
    static const QList<StarterDecor> layout = {
        {"seaweed-tall", K::Plant, {{0.06, 0.93, 1.0, false, false}, {0.71, 0.95, 0.85, false, true}}},
        {"seaweed-bushy", K::Plant, {{0.33, 0.94, 0.9, false, false}, {0.93, 0.92, 0.7, false, true}}},
        {"rock", K::Decor, {{0.2, 0.98, 0.55, true, false}, {0.82, 0.93, 0.4, false, true}}},
        {"coral", K::Decor, {{0.52, 0.93, 0.7, false, false}}},
        {"cave", K::Decor, {{0.64, 0.975, 0.75, false, false}}, {90 / 320.0, 85 / 200.0, 140 / 320.0, 100 / 200.0}},
        {"crab", K::Crawler, {}, {}, "crab", 2},
        {"snail", K::Crawler, {}, {}, "snail", 1},
    };
    return layout;
}

// Smooth 0..1 ramp between a and b
double smoothstep(double a, double b, double x)
{
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

QImage makeBubbleSprite()
{
    QImage img(kFoodCell, kFoodCell, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(220, 245, 255, 230), 2.5));
    p.setBrush(QColor(200, 240, 255, 50));
    p.drawEllipse(QRectF(2, 2, kFoodCell - 4, kFoodCell - 4));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 220));
    p.drawEllipse(QRectF(8, 7, 7, 6));
    return img;
}

// A soft ring, drawn squashed flat on the surface
QImage makeRippleSprite()
{
    QImage img(kFoodCell * 2, kFoodCell * 2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(235, 250, 255, 240), 5));
    p.drawEllipse(QRectF(4, 4, img.width() - 8, img.height() - 8));
    return img;
}

// Overall hue of a drawing (circular mean over its coloured pixels), or -1
double dominantHue(const QImage &image)
{
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    double x = 0, y = 0;
    for (int row = 0; row < img.height(); row += 2) {
        const auto *px = reinterpret_cast<const QRgb *>(img.constScanLine(row));
        for (int col = 0; col < img.width(); col += 2) {
            if (qAlpha(px[col]) < 128)
                continue;
            const QColor c = QColor::fromRgb(px[col]);
            if (c.hsvSaturationF() < 0.25 || c.valueF() < 0.2)
                continue;   // outlines, white, grey
            const double angle = c.hsvHueF() * 2 * M_PI;
            x += std::cos(angle) * c.hsvSaturationF();
            y += std::sin(angle) * c.hsvSaturationF();
        }
    }
    if (std::hypot(x, y) < 1e-6)
        return -1;
    double hue = std::atan2(y, x) / (2 * M_PI);
    return hue < 0 ? hue + 1 : hue;
}

// A baby looks like parent A, coloured partway toward parent B. The whole
// drawing is turned around the colour wheel by one shared amount, so it stays
// consistent (shifting each pixel toward B on its own would send nearly
// opposite colours different ways round the wheel).
QImage mixColours(const QImage &shape, const QImage &colourSource)
{
    QImage out = shape.convertToFormat(QImage::Format_ARGB32);
    const double from = dominantHue(shape);
    const double to = dominantHue(colourSource);
    if (from < 0 || to < 0)
        return out;
    double delta = to - from;
    if (delta > 0.5)
        delta -= 1;
    else if (delta < -0.5)
        delta += 1;
    const double turn = delta * 0.6;
    for (int row = 0; row < out.height(); ++row) {
        auto *px = reinterpret_cast<QRgb *>(out.scanLine(row));
        for (int col = 0; col < out.width(); ++col) {
            if (qAlpha(px[col]) == 0)
                continue;
            QColor c = QColor::fromRgba(px[col]);
            if (c.hsvSaturationF() < 0.15)
                continue;   // keep outlines and highlights as they are
            double hue = c.hsvHueF() + turn;
            hue -= std::floor(hue);
            c.setHsvF(float(hue), c.hsvSaturationF(), c.valueF(), c.alphaF());
            px[col] = c.rgba();
        }
    }
    return out;
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

// The bubble wand's bubble, large enough to stay sharp around a big fish:
// a thin rim, a faint rainbow sheen and a window highlight
constexpr int kBigBubble = 384;
QImage makeBigBubbleSprite()
{
    QImage img(kBigBubble, kBigBubble, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(4, 4, kBigBubble - 8, kBigBubble - 8);
    const QPointF c = r.center();
    const qreal R = r.width() / 2;
    // The film: clear in the middle, thicker and tinted toward the edge
    QRadialGradient film(c, R);
    film.setColorAt(0.0, QColor(255, 255, 255, 0));
    film.setColorAt(0.72, QColor(200, 235, 255, 18));
    film.setColorAt(0.9, QColor(210, 190, 255, 60));
    film.setColorAt(0.97, QColor(170, 230, 255, 120));
    film.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(film);
    p.drawEllipse(r);
    // Rim
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(235, 250, 255, 200), kBigBubble * 0.008));
    p.drawEllipse(r.adjusted(2, 2, -2, -2));
    // A soft window reflection, top left, and a small one bottom right
    QPainterPath shine;
    shine.arcMoveTo(r.adjusted(R * 0.18, R * 0.18, -R * 0.18, -R * 0.18), 105);
    shine.arcTo(r.adjusted(R * 0.18, R * 0.18, -R * 0.18, -R * 0.18), 105, 55);
    p.setPen(QPen(QColor(255, 255, 255, 190), kBigBubble * 0.03, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(shine);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawEllipse(QPointF(c.x() - R * 0.45, c.y() - R * 0.5), R * 0.07, R * 0.05);
    p.setBrush(QColor(255, 255, 255, 110));
    p.drawEllipse(QPointF(c.x() + R * 0.55, c.y() + R * 0.5), R * 0.05, R * 0.035);
    return img;
}

// A heart for petted fish
QImage makeHeartSprite()
{
    QImage img(kFoodCell, kFoodCell, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = kFoodCell;
    QPainterPath heart;
    heart.moveTo(s * 0.5, s * 0.88);
    heart.cubicTo(s * 0.05, s * 0.6, s * 0.02, s * 0.22, s * 0.28, s * 0.15);
    heart.cubicTo(s * 0.42, s * 0.11, s * 0.5, s * 0.25, s * 0.5, s * 0.3);
    heart.cubicTo(s * 0.5, s * 0.25, s * 0.58, s * 0.11, s * 0.72, s * 0.15);
    heart.cubicTo(s * 0.98, s * 0.22, s * 0.95, s * 0.6, s * 0.5, s * 0.88);
    p.setPen(QPen(QColor(0xa8, 0x1e, 0x4a), 2.5));
    p.setBrush(QColor(0xff, 0x5c, 0x8a));
    p.drawPath(heart);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 170));
    p.drawEllipse(QRectF(s * 0.24, s * 0.24, s * 0.14, s * 0.1));
    return img;
}

// One egg: a pale, see-through pearl
QImage makeEggSprite()
{
    QImage img(kFoodCell, kFoodCell, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0xd8, 0x9a, 0x5c, 220), 2));
    p.setBrush(QColor(0xff, 0xe4, 0xb8, 200));
    p.drawEllipse(QRectF(3, 3, kFoodCell - 6, kFoodCell - 6));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x5a, 0x3a, 0x22, 200));
    p.drawEllipse(QRectF(kFoodCell * 0.42, kFoodCell * 0.45, 6, 6));
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawEllipse(QRectF(9, 8, 6, 5));
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
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        // The fish directory: drawings changed. Its parent: state or requests.
        if (path == FishLibrary::directory())
            m_reloadDebounce.start();
        else
            checkRequests();
    });
    // Also the request file itself, in case it is rewritten in place
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &FishTank::checkRequests);
    // Requests already waiting when the tank starts are old news
    m_handledRequest = FishLibrary::themeRequest().at;

    // Friendships take hours to grow; keep them across restarts
    m_saveTimer.setInterval(60 * 1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &FishTank::saveState);
    m_saveTimer.start();

    // In refresh-synced mode frames drive the ticks; if a frame ever goes
    // missing (nothing else changed on screen), this keeps the loop alive
    m_vsyncWatchdog.setSingleShot(true);
    m_vsyncWatchdog.setInterval(250);
    connect(&m_vsyncWatchdog, &QTimer::timeout, this, [this] {
        if (m_vsyncActive) {
            tick();
            m_vsyncWatchdog.start();
        }
    });

    m_autoFeedTimer.setInterval(10 * 1000);
    connect(&m_autoFeedTimer, &QTimer::timeout, this, &FishTank::maybeAutoFeed);
    m_autoFeedTimer.start();

    // Babies grow slowly; once a minute is smooth enough
    m_growthTimer.setInterval(60 * 1000);
    connect(&m_growthTimer, &QTimer::timeout, this, &FishTank::refreshGrowth);
    m_growthTimer.start();

    // Two seconds between /proc/stat reads is plenty for a mood of the water
    m_cpuTimer.setInterval(2000);
    connect(&m_cpuTimer, &QTimer::timeout, this, &FishTank::sampleCpu);

    checkHealth();
    TankHub::registerTank(this);

    m_profile.enabled = qEnvironmentVariableIntValue("DESKTOCEAN_PROFILE") == 1;
    m_profile.window.start();
}

void FishTank::reportProfile()
{
    if (m_profile.window.elapsed() < 5000)
        return;
    const auto avg = [](qint64 ns, int n) { return n ? double(ns) / n / 1000.0 : 0.0; };
    qInfo("Desktocean profile: %d ticks, step %.1f us, pull %.1f us | %d paints, geometry %.1f us | fish %zu",
          m_profile.ticks, avg(m_profile.stepNs, m_profile.ticks), avg(m_profile.pullNs, m_profile.ticks),
          m_profile.paints, avg(m_profile.paintNs, m_profile.paints), m_fish.size());
    m_profile = Profile{true, {}, 0, 0, 0, 0, 0};
    m_profile.window.start();
}

FishTank::~FishTank()
{
    TankHub::unregisterTank(this);
    SoundBoard::instance().setActive(this, false);
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

void FishTank::setReactToCpu(bool react)
{
    if (react == m_reactToCpu)
        return;
    m_reactToCpu = react;
    updateTimer();
    Q_EMIT reactToCpuChanged();
    Q_EMIT agitationChanged();
}

void FishTank::setWeatherStir(qreal stir)
{
    stir = std::clamp(stir, 0.0, 1.0);
    if (qFuzzyCompare(stir + 1, m_weatherStir + 1))
        return;
    m_weatherStir = stir;
    Q_EMIT agitationChanged();
}

void FishTank::setWeatherLight(qreal light)
{
    light = std::clamp(light, 0.0, 2.0);
    if (qFuzzyCompare(light, m_weatherLight))
        return;
    m_weatherLight = light;
    update();
    Q_EMIT weatherLightChanged();
}

void FishTank::sampleCpu()
{
    // First line of /proc/stat: cpu user nice system idle iowait irq softirq steal ...
    QFile stat(QStringLiteral("/proc/stat"));
    if (!stat.open(QIODevice::ReadOnly))
        return;
    const QList<QByteArray> fields = stat.readLine().simplified().split(' ');
    if (fields.size() < 5 || fields[0] != "cpu")
        return;
    quint64 total = 0, idle = 0;
    for (int i = 1; i < fields.size(); ++i) {
        const quint64 v = fields[i].toULongLong();
        total += v;
        if (i == 4 || i == 5)   // idle, iowait
            idle += v;
    }
    const quint64 busy = total - idle;
    if (m_cpuTotal > 0 && total > m_cpuTotal) {
        const double load = double(busy - m_cpuBusy) / double(total - m_cpuTotal);
        // Ease toward the new value so a short spike doesn't slosh the tank
        const qreal smoothed = m_cpuLoad + (std::clamp(load, 0.0, 1.0) - m_cpuLoad) * 0.35;
        if (std::abs(smoothed - m_cpuLoad) > 0.005) {
            m_cpuLoad = smoothed;
            Q_EMIT agitationChanged();
        }
    }
    m_cpuBusy = busy;
    m_cpuTotal = total;
}

void FishTank::setBabies(bool babies)
{
    if (babies == m_babies)
        return;
    m_babies = babies;
    applyLifeSpeed();
    Q_EMIT babiesChanged();
}

void FishTank::setMaxFish(int maxFish)
{
    maxFish = std::clamp(maxFish, 1, 200);
    if (maxFish == m_maxFish)
        return;
    m_maxFish = maxFish;
    Q_EMIT babiesChanged();
}

void FishTank::setLifeSpeed(qreal speed)
{
    if (qFuzzyCompare(speed, m_lifeSpeed))
        return;
    m_lifeSpeed = speed;
    applyLifeSpeed();
    Q_EMIT babiesChanged();
}

void FishTank::applyLifeSpeed()
{
    dc_sim_set_life_speed(m_sim, m_babies ? float(m_lifeSpeed) : 0.f);
}

void FishTank::handleBirths()
{
    DcBirth births[8];
    const uint32_t n = dc_sim_take_births(m_sim, births, 8);
    // Every fish lives on one screen only, so each tank records its own
    // babies; the lock screen only looks
    if (!m_babies || TankHub::isLockScreen())
        return;
    if (n > 0)
        SoundBoard::instance().play(SoundBoard::Sound::Hatch);
    for (uint32_t i = 0; i < n; ++i)
        recordBaby(births[i]);
}

void FishTank::recordBaby(const DcBirth &birth)
{
    if (TankHub::totalFish() >= m_maxFish)
        return;
    const QString idA = m_specOwner.value(birth.parent_a), idB = m_specOwner.value(birth.parent_b);
    const QList<FishEntry> entries = FishLibrary::load();
    const auto find = [&](const QString &id) {
        return std::find_if(entries.begin(), entries.end(), [&](const FishEntry &e) { return e.id == id; });
    };
    const auto a = find(idA), b = find(idB);
    if (a == entries.end() || b == entries.end())
        return;

    const QImage shape(a->path());
    const QImage colours(b->path());
    if (shape.isNull())
        return;
    FishDrawing drawing;
    drawing.sprite = mixColours(shape, colours);
    if (!a->wiggle.isEmpty())
        drawing.wiggle = QImage(a->wigglePath());
    // Same shape, same mouth
    const QPointF mouth = a->mouth.x() >= 0 ? a->mouth : FishLibrary::guessMouth(shape);
    drawing.mouth = QPointF(mouth.x() * shape.width(), mouth.y() * shape.height());

    FishEntry meta;
    // No name of its own: it is called after its parents, whatever they are called
    meta.style = a->style;
    meta.scale = (a->scale + b->scale) / 2;
    meta.speed = (a->speed + b->speed) / 2;
    // Small fish have litters: siblings share one drawing, as copies
    meta.count = std::clamp(int(birth.count), 1, std::max(1, m_maxFish - fishCount()));
    meta.bornAt = QDateTime::currentMSecsSinceEpoch();
    meta.parents = {a->id, b->id};
    QString error;
    const auto added = FishLibrary::add(drawing, meta, &error);
    if (!added) {
        qWarning("Desktocean: could not record a baby: %s", qPrintable(error));
        return;
    }
    // Placed between the parents (on this screen) once the library reload brings it in
    for (int k = 0; k < added->count; ++k)
        TankHub::assign(quint64(qHash(added->id, size_t(k))), this);
    m_newborns.insert(added->id, QPointF(birth.x, birth.y));
}

void FishTank::applyAssignment(bool animateNew)
{
    m_specs.clear();
    m_specEntry.clear();
    for (size_t i = 0; i < m_allSpecs.size(); ++i) {
        if (TankHub::owns(this, m_allSpecs[i].id)) {
            m_specs.push_back(m_allSpecs[i]);
            m_specEntry.push_back(m_allSpecEntry[i]);
        }
    }
    std::vector<DcCrawlerSpec> crawlers;
    for (const DcCrawlerSpec &c : m_allCrawlers) {
        if (TankHub::owns(this, c.id))
            crawlers.push_back(c);
    }
    dc_sim_set_population(m_sim, m_specs.data(), uint32_t(m_specs.size()), animateNew);
    dc_sim_set_crawlers(m_sim, crawlers.data(), uint32_t(crawlers.size()));
    pullState();
    update();
}

std::vector<DcRelation> FishTank::relationsOf(quint64 id) const
{
    const uint32_t n = dc_sim_relations(m_sim, nullptr, 0);
    std::vector<DcRelation> all(n);
    all.resize(std::min(n, dc_sim_relations(m_sim, all.data(), n)));
    std::vector<DcRelation> mine;
    std::copy_if(all.begin(), all.end(), std::back_inserter(mine),
                 [&](const DcRelation &r) { return r.a == id || r.b == id; });
    return mine;
}

void FishTank::adopt(quint64 id, int side, qreal y, const std::vector<DcRelation> &relations)
{
    applyAssignment(false);
    if (!relations.empty()) {
        const uint32_t n = dc_sim_relations(m_sim, nullptr, 0);
        std::vector<DcRelation> all(n);
        all.resize(std::min(n, dc_sim_relations(m_sim, all.data(), n)));
        all.insert(all.end(), relations.begin(), relations.end());
        dc_sim_set_relations(m_sim, all.data(), uint32_t(all.size()));
    }
    dc_sim_arrive(m_sim, id, side, float(y));
    pullState();
}

void FishTank::setExits(bool left, bool right)
{
    dc_sim_set_exits(m_sim, left, right);
}

uint32_t FishTank::carryNew(QPointF at)
{
    const uint32_t id = dc_sim_carry_new(m_sim, float(at.x()), float(at.y()));
    pullState();
    update();
    return id;
}

bool FishTank::carryTo(uint32_t id, QPointF at)
{
    const bool ok = dc_sim_carry_to(m_sim, id, float(at.x()), float(at.y()));
    pullState();
    update();
    return ok;
}

void FishTank::carryRelease(uint32_t id)
{
    dc_sim_carry_release(m_sim, id);
}

QRectF FishTank::carryRect(uint32_t id) const
{
    for (const DcCarry &c : m_carries) {
        if (c.id == id)
            return QRectF(c.x - c.radius, c.y - c.radius, c.radius * 2, c.radius * 2);
    }
    return {};
}

void FishTank::carryEnd(uint32_t id)
{
    dc_sim_carry_end(m_sim, id);
    pullState();
    update();
}

void FishTank::refreshGrowth()
{
    // Once a minute is also often enough to keep trust and eggs saved
    if (m_stateLoaded)
        saveState();
    bool changed = false;
    for (size_t i = 0; i < m_specs.size(); ++i) {
        const FishEntry &e = m_specEntry[i];
        if (e.bornAt <= 0)
            continue;
        const double g = FishLibrary::growth(e);
        const float scale = float(e.scale * g);
        const uint32_t adult = g >= 1.0 ? 1 : 0;
        if (std::abs(scale - m_specs[i].scale) > 0.002f || adult != m_specs[i].adult) {
            m_specs[i].scale = scale;
            m_specs[i].adult = adult;
            changed = true;
            // ...and in the full list, so a later reshuffle keeps the size
            for (DcFishSpec &s : m_allSpecs) {
                if (s.id == m_specs[i].id) {
                    s.scale = scale;
                    s.adult = adult;
                }
            }
        }
    }
    if (changed)
        dc_sim_set_population(m_sim, m_specs.data(), uint32_t(m_specs.size()), false);
}

void FishTank::befriendAll()
{
    std::vector<DcRelation> list;
    for (size_t i = 0; i < m_specs.size(); ++i) {
        for (size_t j = i + 1; j < m_specs.size(); ++j)
            list.push_back({m_specs[i].id, m_specs[j].id, 0.95f});
    }
    dc_sim_set_relations(m_sim, list.data(), uint32_t(list.size()));
}

void FishTank::announce()
{
    dc_sim_announce(m_sim);
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
    // Asleep: night for the fish only
    dc_sim_set_daylight(m_sim, m_asleep ? 0.f : float(d));
    if (std::abs(d - m_daylight) < 0.0005)
        return;
    m_daylight = d;
    update();
    Q_EMIT daylightChanged();
}

void FishTank::setAsleep(bool asleep)
{
    if (asleep == m_asleep)
        return;
    m_asleep = asleep;
    updateDaylight();
    Q_EMIT asleepChanged();
}

qreal FishTank::averageHunger() const
{
    if (m_fish.empty())
        return 0;
    double hunger = 0;
    for (const DcFish &f : m_fish)
        hunger += f.hunger;
    return hunger / m_fish.size();
}

QPair<int, int> FishTank::relationCounts() const
{
    int friends = 0, rivals = 0;
    const uint32_t n = dc_sim_relations(m_sim, nullptr, 0);
    std::vector<DcRelation> rel(n);
    rel.resize(std::min(n, dc_sim_relations(m_sim, rel.data(), n)));
    for (const DcRelation &r : rel) {
        friends += r.affinity > DC_FRIEND;
        rivals += r.affinity < DC_RIVAL;
    }
    return {friends, rivals};
}

bool FishTank::exportSprites(const QString &dir) const
{
    QDir().mkpath(dir);
    const QDir d(dir);
    bool ok = true;
    ok &= makeFoodSprite().save(d.filePath(QStringLiteral("pellet.png")));
    ok &= makeBubbleSprite().save(d.filePath(QStringLiteral("bubble.png")));
    ok &= makeRippleSprite().save(d.filePath(QStringLiteral("ripple.png")));
    ok &= makeEggSprite().save(d.filePath(QStringLiteral("egg.png")));
    ok &= makeHeartSprite().save(d.filePath(QStringLiteral("heart.png")));
    ok &= makeBigBubbleSprite().save(d.filePath(QStringLiteral("carry-bubble.png")));
    return ok;
}

QString FishTank::stats() const
{
    const auto [friends, rivals] = relationCounts();
    const auto inCaves = std::count_if(m_fish.begin(), m_fish.end(), [](const DcFish &f) { return f.sheltered != 0; });
    return QStringLiteral("Hunger: %1%  Daylight: %2%\nFriends: %3  Rivals: %4  Bubbles: %5\nIn caves: %6  Eggs: %7")
        .arg(qRound(averageHunger() * 100))
        .arg(qRound(m_daylight * 100))
        .arg(friends)
        .arg(rivals)
        .arg(m_bubbles.size())
        .arg(inCaves)
        .arg(m_eggs.size());
}

void FishTank::loadState()
{
    QFile file(FishLibrary::statePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    std::vector<DcRelation> list;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray rel = root.value(QStringLiteral("relations")).toArray();
    for (const QJsonValue &v : rel) {
        const QJsonObject o = v.toObject();
        list.push_back({o.value(QStringLiteral("a")).toString().toULongLong(),
                        o.value(QStringLiteral("b")).toString().toULongLong(),
                        float(o.value(QStringLiteral("affinity")).toDouble())});
    }
    dc_sim_set_relations(m_sim, list.data(), uint32_t(list.size()));

    std::vector<DcTrust> trust;
    const QJsonObject trustMap = root.value(QStringLiteral("trust")).toObject();
    for (auto it = trustMap.begin(); it != trustMap.end(); ++it)
        trust.push_back({it.key().toULongLong(), float(it.value().toDouble())});
    dc_sim_set_trust(m_sim, trust.data(), uint32_t(trust.size()));

    // Eggs belong to the tank that records babies
    if (!TankHub::isPrimary(this))
        return;
    m_savedEggs.clear();
    for (const QJsonValue &v : root.value(QStringLiteral("eggs")).toArray()) {
        const QJsonObject o = v.toObject();
        m_savedEggs.push_back({o.value(QStringLiteral("a")).toString().toULongLong(),
                               o.value(QStringLiteral("b")).toString().toULongLong(),
                               float(o.value(QStringLiteral("x")).toDouble()), float(o.value(QStringLiteral("y")).toDouble()),
                               uint32_t(o.value(QStringLiteral("count")).toInt(1)),
                               float(o.value(QStringLiteral("progress")).toDouble())});
    }
    restoreEggs();
}

void FishTank::restoreEggs()
{
    if (m_savedEggs.empty() || width() <= 0 || height() <= 0)
        return;
    std::vector<DcEgg> eggs = m_savedEggs;
    for (DcEgg &e : eggs) {
        e.x *= float(width());
        e.y *= float(height());
    }
    dc_sim_set_eggs(m_sim, eggs.data(), uint32_t(eggs.size()));
    m_savedEggs.clear();
}

void FishTank::saveState() const
{
    if (!m_stateLoaded)
        return;   // never overwrite saved friendships before they were read
    if (TankHub::isLockScreen())
        return;   // plasmashell's tank keeps the state; the lock screen only reads it

    // With several screens every tank holds some of the fish: start from what
    // is saved and replace only what concerns the fish swimming here
    QJsonObject old;
    {
        QFile file(FishLibrary::statePath());
        if (file.open(QIODevice::ReadOnly))
            old = QJsonDocument::fromJson(file.readAll()).object();
    }
    QSet<quint64> here;
    for (const DcFish &f : m_fish)
        here.insert(f.id);
    const auto idOf = [](const QJsonValue &v) { return v.toString().toULongLong(); };

    const uint32_t n = dc_sim_relations(m_sim, nullptr, 0);
    std::vector<DcRelation> rel(n);
    rel.resize(std::min(n, dc_sim_relations(m_sim, rel.data(), n)));
    for (const QJsonValue &v : old.value(QStringLiteral("relations")).toArray()) {
        const QJsonObject o = v.toObject();
        const quint64 a = idOf(o.value(QStringLiteral("a"))), b = idOf(o.value(QStringLiteral("b")));
        if (!here.contains(a) && !here.contains(b))
            rel.push_back({a, b, float(o.value(QStringLiteral("affinity")).toDouble())});
    }

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

    // Character of every fish, and of every copy by library fish for the editor's cards
    QJsonObject fishCharacters = old.value(QStringLiteral("fishCharacters")).toObject();
    for (const DcFish &f : m_fish)
        fishCharacters.insert(QString::number(f.id), int(f.character));
    // Forget fish that are no longer in the library
    for (auto it = fishCharacters.begin(); it != fishCharacters.end();)
        it = m_specOwner.contains(it.key().toULongLong()) ? std::next(it) : fishCharacters.erase(it);
    static const char *const kCharacters[] = {"explorer", "homebody", "bottom dweller", "skimmer", "lazy", "playful", "shy"};
    QHash<QString, QStringList> characters;
    for (auto it = fishCharacters.begin(); it != fishCharacters.end(); ++it) {
        const QString owner = m_specOwner.value(it.key().toULongLong());
        const int c = it.value().toInt(-1);
        if (!owner.isEmpty() && c >= 0 && c < int(std::size(kCharacters)))
            characters[owner] << QString::fromLatin1(kCharacters[c]);
    }
    QJsonObject characterMap;
    for (auto it = characters.cbegin(); it != characters.cend(); ++it)
        characterMap.insert(it.key(), QJsonArray::fromStringList(it.value()));

    // Eggs, as fractions of the tank so another screen size still works
    QJsonArray eggs;
    QSet<quint64> ourClutches;
    if (width() > 0 && height() > 0) {
        for (const DcEgg &e : m_eggs) {
            ourClutches.insert(e.parent_a);
            eggs.append(QJsonObject{{QStringLiteral("a"), QString::number(e.parent_a)},
                                    {QStringLiteral("b"), QString::number(e.parent_b)},
                                    {QStringLiteral("x"), e.x / width()},
                                    {QStringLiteral("y"), e.y / height()},
                                    {QStringLiteral("count"), int(e.count)},
                                    {QStringLiteral("progress"), double(e.progress)}});
        }
    }
    for (const QJsonValue &v : old.value(QStringLiteral("eggs")).toArray()) {
        const quint64 a = idOf(v.toObject().value(QStringLiteral("a")));
        if (!here.contains(a) && !ourClutches.contains(a))
            eggs.append(v);
    }

    // How much each fish trusts you
    QJsonObject trust = old.value(QStringLiteral("trust")).toObject();
    const uint32_t nt = dc_sim_trust(m_sim, nullptr, 0);
    std::vector<DcTrust> list(nt);
    list.resize(std::min(nt, dc_sim_trust(m_sim, list.data(), nt)));
    for (const DcTrust &t : list) {
        if (here.contains(t.id))
            trust.insert(QString::number(t.id), double(t.trust));
    }

    const QJsonObject root{{QStringLiteral("relations"), relations},
                           {QStringLiteral("friends"), friends},
                           {QStringLiteral("rivals"), rivals},
                           {QStringLiteral("characters"), characterMap},
                           {QStringLiteral("fishCharacters"), fishCharacters},
                           {QStringLiteral("theme"), m_theme},
                           {QStringLiteral("eggs"), eggs},
                           {QStringLiteral("trust"), trust}};
    QSaveFile file(FishLibrary::statePath());
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

void FishTank::setMatchRefresh(bool match)
{
    if (match == m_matchRefresh)
        return;
    m_matchRefresh = match;
    m_timer.stop();
    m_vsyncActive = false;
    updateTimer();
    Q_EMIT fpsChanged();
}

void FishTank::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change != ItemSceneChange)
        return;
    disconnect(m_frameSwapped);
    if (data.window) {
        // Emitted on the render thread; queued so ticks run on the GUI thread
        m_frameSwapped = connect(data.window, &QQuickWindow::frameSwapped, this, &FishTank::onFrameSwapped,
                                 Qt::QueuedConnection);
    }
}

void FishTank::onFrameSwapped()
{
    if (!m_vsyncActive)
        return;
    tick();   // asks for the next frame via update()
    m_vsyncWatchdog.start();
}

void FishTank::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    m_paused = paused;
    updateTimer();
    Q_EMIT pausedChanged();
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

void FishTank::setTheme(const QString &theme)
{
    if (theme == m_theme)
        return;
    m_theme = theme;
    saveState();   // so the editor's tank layout shows the same colours
    Q_EMIT themeChanged();
}

QVariantMap FishTank::themeColors() const
{
    return Themes::colors(m_theme);
}

void FishTank::setFogColor(const QColor &color)
{
    if (color == m_fogColor)
        return;
    m_fogColor = color;
    update();
    Q_EMIT fogColorChanged();
}

void FishTank::setAutoFeed(bool on)
{
    if (on == m_autoFeed)
        return;
    m_autoFeed = on;
    Q_EMIT autoFeedChanged();
}

void FishTank::maybeAutoFeed()
{
    // Only while someone can see it, when the fish are really hungry, the
    // last food is gone and nobody fed them in the last couple of minutes
    constexpr qreal kVeryHungry = 0.7;
    constexpr qint64 kGapMs = 150 * 1000;
    const bool visible = m_running && !m_paused && m_healthy && width() > 0;
    if (!m_autoFeed || !visible || m_fish.empty() || !m_food.empty() || averageHunger() < kVeryHungry)
        return;
    if (m_lastFeed.isValid() && m_lastFeed.elapsed() < kGapMs)
        return;
    feedEverywhere();   // all along the surface, so everyone gets some
}

void FishTank::feed(qreal x)
{
    dc_sim_feed(m_sim, float(x));
    afterFeeding();
}

void FishTank::feedAt(QPointF at)
{
    dc_sim_feed_at(m_sim, float(at.x()), float(at.y()));
    afterFeeding();
}

void FishTank::feedEverywhere()
{
    dc_sim_feed_everywhere(m_sim);
    afterFeeding();
}

void FishTank::afterFeeding()
{
    m_lastFeed.start();
    pullState();
    m_lastFood = m_food.size();
    update();
    // Pellets landing in the water, one after another
    for (const int ms : {0, 140, 330})
        QTimer::singleShot(ms, this, [] { SoundBoard::instance().play(SoundBoard::Sound::Plop, 0.8); });
}

void FishTank::setSounds(bool on)
{
    if (on == m_sounds)
        return;
    m_sounds = on;
    SoundBoard::instance().setEnabled(on);
    Q_EMIT soundsChanged();
}

void FishTank::setSoundVolume(qreal volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume, m_soundVolume))
        return;
    m_soundVolume = volume;
    SoundBoard::instance().setVolume(volume);
    Q_EMIT soundsChanged();
}

void FishTank::setAmbientSound(bool on)
{
    if (on == m_ambientSound)
        return;
    m_ambientSound = on;
    SoundBoard::instance().setAmbient(on);
    Q_EMIT soundsChanged();
}

void FishTank::setSoundDirectory(const QUrl &dir)
{
    if (dir == m_soundDir)
        return;
    m_soundDir = dir;
    // Resolved against: make sure it reads as a directory
    QUrl d = dir;
    if (!d.path().endsWith(QLatin1Char('/')))
        d.setPath(d.path() + QLatin1Char('/'));
    SoundBoard::instance().setDirectory(d);
    Q_EMIT soundsChanged();
}

void FishTank::setSoundsMuted(bool muted)
{
    if (muted == m_soundsMuted)
        return;
    m_soundsMuted = muted;
    updateSoundActive();
    Q_EMIT soundsChanged();
}

void FishTank::updateSoundActive()
{
    // Quiet while nobody can see the tank, when muted, and always on the lock screen
    SoundBoard::instance().setActive(this, m_soundRunning && !m_soundsMuted && !TankHub::isLockScreen());
}

void FishTank::playEventSounds()
{
    auto &board = SoundBoard::instance();
    // A pellet gone (mostly eaten; dissolving ones fade out quietly anyway)
    if (m_food.size() < m_lastFood)
        board.play(SoundBoard::Sound::Nom);
    // Bubbles reaching the surface, now and then
    if (m_bubbles.size() < m_lastBubbles && QRandomGenerator::global()->bounded(3) == 0)
        board.play(SoundBoard::Sound::Bubble, 0.8);
    // A fish started enjoying a pet
    if (m_hearts.size() > m_lastHearts && m_lastHearts == 0)
        board.play(SoundBoard::Sound::Chime);
    m_lastFood = m_food.size();
    m_lastBubbles = m_bubbles.size();
    m_lastHearts = m_hearts.size();
}

void FishTank::feedAtCursor()
{
    // Right where the cursor is; somewhere random if it isn't over the tank
    const QPointF at = cursorInItem();
    if (at.x() >= 0)
        feedAt(at);
    else
        feed(-1);
}

void FishTank::clearFood()
{
    dc_sim_clear_food(m_sim);
    pullState();
    update();
}

bool FishTank::openEditor(const QString &page)
{
    return launchEditor(page);
}

bool FishTank::launchEditor(const QString &page)
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
    QStringList args;
    if (!page.isEmpty())
        args << QStringLiteral("--page") << page;
    return QProcess::startDetached(exe, args);
}

void FishTank::componentComplete()
{
    QQuickItem::componentComplete();
    updateDaylight();
    loadLibrary(false);
    updateTimer();
}

QRectF FishTank::decorRect(const DecorItem &d) const
{
    const float aspect = d.sprite < m_sprites.size() ? m_sprites[d.sprite].aspect : 1.f;
    const float h = std::min(float(height()) * kDecorHeight, float(width()) * kDecorMaxWidth * aspect)
                  * float(d.placement.scale);
    const float w = h / aspect;
    return QRectF(d.placement.x * width() - w / 2, d.placement.y * height() - h, w, h);
}

float FishTank::decorDepth(const DecorItem &d) const
{
    // Where it stands on the floor: lower down is nearer
    return std::clamp(1.f - (float(d.placement.y) - DC_SAND_TOP) / (DC_FLOOR_NEAR - DC_SAND_TOP), 0.f, 1.f);
}

void FishTank::updateShelters()
{
    std::vector<DcShelter> shelters;
    for (const DecorItem &d : m_decor) {
        if (d.opening.isNull())
            continue;
        const QRectF box = decorRect(d);
        const QRectF &o = d.opening;
        const double left = d.placement.flip ? 1 - o.right() : o.left();
        shelters.push_back({float(box.x() + left * box.width()), float(box.y() + o.y() * box.height()),
                            float(o.width() * box.width()), float(o.height() * box.height())});
    }
    dc_sim_set_shelters(m_sim, shelters.data(), uint32_t(shelters.size()));

    // Eggs go on plants: a couple of spots up each one
    std::vector<float> nests;
    for (const DecorItem &d : m_decor) {
        if (d.sway <= 0 || !d.opening.isNull())
            continue;
        const QRectF box = decorRect(d);
        for (const float up : {0.3f, 0.55f}) {
            nests.push_back(float(box.center().x() + (up - 0.4f) * box.width() * 0.3f));
            nests.push_back(float(box.bottom() - up * box.height()));
        }
    }
    dc_sim_set_nests(m_sim, nests.data(), uint32_t(nests.size() / 2));
    updateGround();
}

void FishTank::updateGround()
{
    // Crawlers climb over rocks at about their depth; caves they walk into
    // (behind the front)
    constexpr float kStep = 8.f;
    const int n = int(width() / kStep) + 2;
    if (width() <= 0 || height() <= 0)
        return;
    std::vector<float> heights(size_t(n), std::numeric_limits<float>::infinity());
    std::vector<float> depths(size_t(n), 0.5f);
    for (const DecorItem &d : m_decor) {
        if (!d.opening.isNull() || d.sway > 0 || d.sprite >= m_sprites.size())
            continue;
        const std::vector<float> &top = m_sprites[d.sprite].top;
        if (top.empty())
            continue;
        const QRectF box = decorRect(d);
        for (int i = std::max(0, int(box.left() / kStep)); i < n && i * kStep <= box.right(); ++i) {
            double u = (i * kStep - box.left()) / box.width();
            if (u < 0 || u > 1)
                continue;
            if (d.placement.flip)
                u = 1 - u;
            const float t = top[std::min(top.size() - 1, size_t(u * top.size()))];
            if (t >= 0 && float(box.top() + t * box.height()) < heights[size_t(i)]) {
                heights[size_t(i)] = float(box.top() + t * box.height());
                depths[size_t(i)] = decorDepth(d);
            }
        }
    }
    dc_sim_set_ground(m_sim, heights.data(), depths.data(), uint32_t(heights.size()), kStep);
}

void FishTank::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size())
        return;
    dc_sim_resize(m_sim, float(newGeometry.width()), float(newGeometry.height()));
    updateShelters();
    restoreEggs();
    TankHub::updateNeighbours();
    pullState();
    update();
    updateTimer();
}

void FishTank::updateTimer()
{
    const bool shouldRun = m_running && !m_paused && m_healthy && isComponentComplete() && width() > 0 && height() > 0;
    m_soundRunning = shouldRun;
    updateSoundActive();
    if (m_matchRefresh) {
        m_timer.stop();
        if (shouldRun && !m_vsyncActive) {
            m_vsyncActive = true;
            m_clock.start();
            tick();   // the first frame; each swap then ticks again
            m_vsyncWatchdog.start();
        } else if (!shouldRun) {
            m_vsyncActive = false;
            m_vsyncWatchdog.stop();
        }
    } else if (shouldRun && !m_timer.isActive()) {
        m_timer.start(1000 / m_fps);
        m_clock.start();
    } else if (!shouldRun && m_timer.isActive()) {
        m_timer.stop();
    }
    // Only watch the CPU while someone can see the water move
    if (shouldRun && m_reactToCpu && !m_cpuTimer.isActive())
        m_cpuTimer.start();
    else if ((!shouldRun || !m_reactToCpu) && m_cpuTimer.isActive())
        m_cpuTimer.stop();
}

void FishTank::setShowNames(bool show)
{
    if (show == m_showNames)
        return;
    m_showNames = show;
    Q_EMIT showNamesChanged();
    updateHover();
}

bool FishTank::controlFishAt(QPointF at)
{
    // The front-most fish whose body (a little generously) is under the point
    const DcFish *hit = nullptr;
    for (const DcFish &f : m_fish) {
        const qreal dx = (at.x() - f.x) / (f.w * 0.6), dy = (at.y() - f.y) / (f.h * 0.7);
        if (dx * dx + dy * dy <= 1.0 && (!hit || f.depth < hit->depth))
            hit = &f;
    }
    if (!hit || !dc_sim_control_start(m_sim, hit->id))
        return false;
    m_controlledName = m_specName.value(hit->id);
    Q_EMIT controlChanged();
    return true;
}

void FishTank::controlInput(qreal dx, qreal dy, qreal dz, bool dash)
{
    dc_sim_control_input(m_sim, float(dx), float(dy), float(dz), dash ? 1u : 0u);
}

void FishTank::controlEnd()
{
    dc_sim_control_end(m_sim);
    if (!m_controlledName.isEmpty()) {
        m_controlledName.clear();
        Q_EMIT controlChanged();
    }
}

void FishTank::updateHover()
{
    QString name;
    QPointF point;
    if (m_showNames && m_pointer.x() >= 0) {
        // The front-most fish whose body is under the cursor
        const DcFish *hit = nullptr;
        for (const DcFish &f : m_fish) {
            const qreal dx = (m_pointer.x() - f.x) / (f.w * 0.5), dy = (m_pointer.y() - f.y) / (f.h * 0.5);
            if (dx * dx + dy * dy <= 1.0 && (!hit || f.depth < hit->depth))
                hit = &f;
        }
        if (hit) {
            name = m_specName.value(hit->id);
            point = QPointF(hit->x, hit->y - hit->h * 0.5);
        } else {
            // Crawlers are behind the fish: only when no fish is in the way
            for (const DcCrawler &c : m_crawlers) {
                if (std::abs(m_pointer.x() - c.x) < c.w * 0.45 && m_pointer.y() < c.y && m_pointer.y() > c.y - c.h) {
                    name = m_specName.value(c.id);
                    point = QPointF(c.x, c.y - c.h);
                    break;
                }
            }
        }
    }
    if (name == m_hoveredName && (name.isEmpty() || point == m_hoveredPoint))
        return;
    m_hoveredName = name;
    m_hoveredPoint = point;
    Q_EMIT hoverChanged();
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
    m_flow += dt * (1 + 2 * agitation());
    m_pointer = cursorInItem();
    QElapsedTimer t;
    t.start();
    dc_sim_step(m_sim, float(dt), float(m_pointer.x()), float(m_pointer.y()));
    handleBirths();
    // Fish that swam off to the screen next door
    DcDeparture gone[8];
    const uint32_t departed = dc_sim_take_departures(m_sim, gone, 8);
    for (uint32_t i = 0; i < departed; ++i)
        TankHub::handOver(this, gone[i].id, gone[i].side, gone[i].y);
    playEventSounds();
    // The steered fish left (another screen, removed from the library)
    if (!m_controlledName.isEmpty() && !dc_sim_controlled(m_sim, nullptr)) {
        m_controlledName.clear();
        Q_EMIT controlChanged();
    }
    const qint64 stepped = t.nsecsElapsed();
    pullState();
    updateHover();
    if (m_profile.enabled) {
        m_profile.stepNs += stepped;
        m_profile.pullNs += t.nsecsElapsed() - stepped;
        ++m_profile.ticks;
        reportProfile();
    }
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

    n = dc_sim_bubbles(m_sim, nullptr, 0);
    m_bubbles.resize(n);
    m_bubbles.resize(std::min(n, dc_sim_bubbles(m_sim, m_bubbles.data(), n)));

    n = dc_sim_ripples(m_sim, nullptr, 0);
    m_ripples.resize(n);
    m_ripples.resize(std::min(n, dc_sim_ripples(m_sim, m_ripples.data(), n)));

    n = dc_sim_carries(m_sim, nullptr, 0);
    m_carries.resize(n);
    m_carries.resize(std::min(n, dc_sim_carries(m_sim, m_carries.data(), n)));

    n = dc_sim_hearts(m_sim, nullptr, 0);
    m_hearts.resize(n);
    m_hearts.resize(std::min(n, dc_sim_hearts(m_sim, m_hearts.data(), n)));

    const size_t oldEggs = m_eggs.size();
    n = dc_sim_eggs(m_sim, nullptr, 0);
    m_eggs.resize(n);
    m_eggs.resize(std::min(n, dc_sim_eggs(m_sim, m_eggs.data(), n)));
    // Laid or hatched: keep the file up to date in case Plasma stops
    if (m_eggs.size() != oldEggs && m_stateLoaded)
        saveState();

    n = dc_sim_crawlers(m_sim, nullptr, 0);
    m_crawlers.resize(n);
    m_crawlers.resize(std::min(n, dc_sim_crawlers(m_sim, m_crawlers.data(), n)));

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

void FishTank::seedDecor(const QString &only)
{
    if (m_starterDecor.isEmpty())
        return;
    for (const QUrl &url : std::as_const(m_starterDecor)) {
        // "decor-seaweed-tall.svg" -> "seaweed-tall"
        const QString base = QFileInfo(url.path()).baseName().section(QLatin1Char('-'), 1);
        if (!only.isEmpty() && base != only)
            continue;
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
        if (it->style) {
            meta.style = QLatin1String(it->style);
            meta.count = it->count;
        }
        const QRectF opening = it->opening.isNull()
            ? QRectF()
            : QRectF(it->opening.x() * img.width(), it->opening.y() * img.height(), it->opening.width() * img.width(),
                     it->opening.height() * img.height());
        QString error;
        if (!FishLibrary::add({img, {}, {-1, -1}, opening}, meta, &error))
            qWarning("Desktocean: cannot seed decorations: %s", qPrintable(error));
    }
    const auto mark = [](const QString &name) {
        QFile marker(seededMarker(name));
        if (!marker.open(QIODevice::WriteOnly))
            qWarning("Desktocean: cannot write %s", qPrintable(marker.fileName()));
    };
    if (only.isEmpty()) {
        FishLibrary::markDecorSeeded();
        // A fresh tank already has everything
        for (const StarterDecor &d : starterLayout())
            mark(QLatin1String(d.name));
    } else {
        mark(only);
    }
}

void FishTank::watchLibrary()
{
    const QString dir = FishLibrary::directory();
    if (!TankHub::isLockScreen())
        QDir().mkpath(dir);
    const QString parent = QFileInfo(dir).dir().path();
    for (const QString &path : {dir, parent}) {
        if (!m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }
    const QString requests = FishLibrary::themeRequestPath();
    if (QFileInfo::exists(requests) && !m_watcher.files().contains(requests))
        m_watcher.addPath(requests);
}

void FishTank::checkRequests()
{
    // Replacing a file drops it from the watcher; keep watching the new one
    const QString file = FishLibrary::themeRequestPath();
    if (QFileInfo::exists(file) && !m_watcher.files().contains(file))
        m_watcher.addPath(file);
    const FishLibrary::ThemeRequest request = FishLibrary::themeRequest();
    if (request.at <= m_handledRequest || request.theme.isEmpty())
        return;
    m_handledRequest = request.at;
    if (request.theme != m_theme)
        Q_EMIT themeRequested(request.theme);
}

void FishTank::loadLibrary(bool animateNew)
{
    // The lock screen shows the library as it is; plasmashell seeds it
    const bool writable = !TankHub::isLockScreen();
    if (writable && !FishLibrary::exists() && !m_starterFish.isEmpty())
        seedLibrary();
    if (writable && !FishLibrary::decorSeeded())
        seedDecor();
    else if (TankHub::isPrimary(this)) {
        // Starters that came later (the cave, crawlers): existing tanks get
        // each once, and deleting it afterwards is respected
        for (const StarterDecor &d : starterLayout()) {
            const QString name = QLatin1String(d.name);
            if (!QFileInfo::exists(seededMarker(name)) && name != QLatin1String("rock") && name != QLatin1String("coral")
                && !name.startsWith(QLatin1String("seaweed")))
                seedDecor(name);
        }
    }
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
    std::vector<FishEntry> specEntry;
    std::vector<DecorItem> decor;
    std::vector<DcCrawlerSpec> crawlers;
    QHash<quint64, QString> owners;
    QHash<quint64, QString> names;
    for (const FishEntry &e : entries) {
        const bool isFish = e.kind == FishEntry::Kind::Fish;
        const bool counted = isFish || e.kind == FishEntry::Kind::Crawler;
        if (counted ? e.count <= 0 : e.placements.isEmpty())
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
        if (e.kind == FishEntry::Kind::Crawler) {
            const auto gait = FishMesh::gaitFromId(e.style);
            for (int k = 0; k < e.count; ++k) {
                const auto id = quint64(qHash(e.id, size_t(k)));
                crawlers.push_back({id, sprite, aspect, float(e.scale), FishMesh::gaitIndex(gait), float(e.speed)});
                owners.insert(id, e.id);
                names.insert(id, e.fishName(k));
            }
            continue;
        }
        if (!isFish) {
            const float sway = e.kind == FishEntry::Kind::Plant ? float(e.sway) : 0.f;
            if (e.kind == FishEntry::Kind::Decor) {
                // The top edge of the drawing, for crawlers climbing it
                const QImage &a = images.last();
                std::vector<float> top(48, -1.f);
                for (size_t c = 0; c < top.size(); ++c) {
                    const int x = std::min(a.width() - 1, int((c + 0.5) * a.width() / top.size()));
                    int first = -1, solid = 0;
                    for (int y = 0; y < a.height(); ++y) {
                        if (qAlpha(a.pixel(x, y)) > 128) {
                            first = first < 0 ? y : first;
                            ++solid;
                        }
                    }
                    // Only solid ground: a column that is mostly filled from its
                    // top down (a rock), not branches with water between (coral)
                    if (first >= 0 && solid > 0.85 * (a.height() - first))
                        top[c] = float(first) / a.height();
                }
                sprites.back().top = std::move(top);
            }
            uint32_t front = sprite;
            if (!e.opening.isNull()) {
                // The same drawing with the opening cut out, laid over fish inside
                QImage cut = images.last();
                const QRectF r(e.opening.x() * cut.width(), e.opening.y() * cut.height(),
                               e.opening.width() * cut.width(), e.opening.height() * cut.height());
                QPainter painter(&cut);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                painter.fillPath(caveArch(r), Qt::black);
                painter.end();
                front = uint32_t(sprites.size());
                SpriteInfo cutInfo;
                cutInfo.aspect = aspect;
                cutInfo.style = &FishMesh::style(e.style);
                sprites.push_back(std::move(cutInfo));
                images << cut;
            }
            for (const Placement &p : e.placements)
                decor.push_back({sprite, sway, float(p.x * 17.0 + p.y * 5.0), p, e.opening, front});
            continue;
        }
        // Babies start small and grow up over a couple of hours
        const double growth = FishLibrary::growth(e);
        // Marked in the editor, or guessed from the front of the drawing
        const QPointF mouth = e.mouth.x() >= 0 ? e.mouth : FishLibrary::guessMouth(img);
        for (int k = 0; k < e.count; ++k) {
            const auto id = quint64(qHash(e.id, size_t(k)));
            specs.push_back({id, sprite, aspect, float(e.scale * growth), growth >= 1.0 ? 1u : 0u, float(mouth.x()),
                             float(mouth.y()), uint32_t(FishMesh::style(e.style).body), float(e.speed)});
            specEntry.push_back(e);
            owners.insert(id, e.id);
            names.insert(id, e.fishName(k));
        }
    }

    m_sprites = std::move(sprites);
    // Back to front by where they stand: lower on the screen is closer
    std::sort(decor.begin(), decor.end(), [](const DecorItem &a, const DecorItem &b) { return a.placement.y < b.placement.y; });
    m_decor = std::move(decor);
    updateShelters();
    rebuildAtlas(images);
    m_allSpecs = std::move(specs);
    m_allSpecEntry = std::move(specEntry);
    m_allCrawlers = std::move(crawlers);
    applyAssignment(animateNew && m_libraryLoaded);
    m_specOwner = owners;
    m_specName = names;
    // Newborns appear between their parents rather than dropping in from above
    for (auto it = m_newborns.begin(); it != m_newborns.end();) {
        if (owners.key(it.key()) != 0) {
            dc_sim_place_fish(m_sim, quint64(qHash(it.key(), size_t(0))), float(it->x()), float(it->y()));
            it = m_newborns.erase(it);
        } else {
            ++it;
        }
    }
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
    place(QSize(kBigBubble, kBigBubble));   // the wand's bubble
    place(QSize(kFoodCell, kFoodCell));   // heart
    place(QSize(kFoodCell, kFoodCell));   // egg
    place(QSize(kFoodCell, kFoodCell));
    place(QSize(kFoodCell, kFoodCell));
    place(QSize(kFoodCell * 2, kFoodCell * 2));
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
    const QRect bigBubbleRect = placed[placed.size() - 6];
    painter.drawImage(bigBubbleRect.topLeft(), makeBigBubbleSprite());
    m_bigBubbleUv = normalised(bigBubbleRect);
    const QRect heartRect = placed[placed.size() - 5];
    painter.drawImage(heartRect.topLeft(), makeHeartSprite());
    m_heartUv = normalised(heartRect);
    const QRect eggRect = placed[placed.size() - 4];
    painter.drawImage(eggRect.topLeft(), makeEggSprite());
    m_eggUv = normalised(eggRect);
    const QRect foodRect = placed[placed.size() - 3];
    const QRect bubbleRect = placed[placed.size() - 2];
    const QRect rippleRect = placed.back();
    painter.drawImage(foodRect.topLeft(), makeFoodSprite());
    painter.drawImage(bubbleRect.topLeft(), makeBubbleSprite());
    painter.drawImage(rippleRect.topLeft(), makeRippleSprite());
    m_foodUv = normalised(foodRect);
    m_bubbleUv = normalised(bubbleRect);
    m_rippleUv = normalised(rippleRect);
    painter.end();

    m_atlas = atlas;
    m_atlasDirty = true;
    update();
}

QSGNode *FishTank::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QElapsedTimer profileTimer;
    profileTimer.start();
    auto *node = static_cast<SpriteNode *>(oldNode);
    if (!node)
        node = new SpriteNode;

    if (m_atlasDirty && !m_atlas.isNull()) {
        m_atlasDirty = false;
        node->setAtlas(window(), m_atlas);
    }
    node->setFogColor(m_fogColor);
    node->setLight(float((0.35 + 0.65 * m_daylight) * m_weatherLight));

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
    const int nBubbles = node->hasAtlas() ? int(m_bubbles.size() + m_ripples.size() + m_hearts.size() + m_carries.size()) : 0;
    // Caves are drawn twice: whole behind the fish, and cut open over the fish inside
    const int nDecor = node->hasAtlas() ? int(std::accumulate(m_decor.begin(), m_decor.end(), 0, [&](int n, const DecorItem &d) {
                                               if (d.sprite >= m_sprites.size())
                                                   return n;
                                               return n + (d.opening.isNull() ? 1 : 2);
                                           }))
                                         : 0;
    // Each clutch is a little cluster of eggs
    constexpr int kEggsPerClutch = 7;
    const int nEggs = node->hasAtlas() ? int(m_eggs.size()) * kEggsPerClutch : 0;
    const int nCrawlers = node->hasAtlas() ? int(std::count_if(m_crawlers.begin(), m_crawlers.end(), [&](const DcCrawler &c) {
                                                 return c.sprite < m_sprites.size();
                                             }))
                                           : 0;

    quint32 *idx = nullptr;
    FishMesh::Vertex *v = node->reserve(
        (nFish + nCrawlers) * FishMesh::kVertsPerFish + (nFood + nBubbles + nEggs) * 4 + nDecor * FishMesh::kVertsPerDecor,
        (nFish + nCrawlers) * FishMesh::kIndicesPerFish + (nFood + nBubbles + nEggs) * 6
            + nDecor * FishMesh::kIndicesPerDecor,
        &idx);
    quint32 base = 0;

    // Everything that stands or swims in the tank is drawn back to front by
    // depth, in one go: decorations (deeper the higher up the sand they
    // stand), crawlers, eggs and fish. A cave is drawn whole just behind its
    // depth and cut open just in front of it, so a fish inside goes between.
    enum class Kind { Decor, CaveFront, Crawler, Eggs, Fish };
    struct Draw {
        float depth;
        Kind kind;
        int index;
    };
    std::vector<Draw> draws;
    draws.reserve(m_decor.size() * 2 + m_crawlers.size() + m_eggs.size() + order.size());
    std::vector<QRectF> caveOpenings(m_decor.size());
    for (int i = 0; i < int(m_decor.size()); ++i) {
        const DecorItem &d = m_decor[size_t(i)];
        if (d.sprite >= m_sprites.size())
            continue;
        const float depth = decorDepth(d);
        if (d.opening.isNull()) {
            draws.push_back({depth, Kind::Decor, i});
        } else {
            draws.push_back({depth + 0.002f, Kind::Decor, i});
            draws.push_back({depth - 0.002f, Kind::CaveFront, i});
            const QRectF box = decorRect(d);
            const QRectF &o = d.opening;
            const double left = d.placement.flip ? 1 - o.right() : o.left();
            caveOpenings[size_t(i)] = QRectF(box.x() + left * box.width(), box.y() + o.y() * box.height(),
                                             o.width() * box.width(), o.height() * box.height());
        }
    }
    for (int i = 0; i < int(m_crawlers.size()); ++i) {
        if (m_crawlers[size_t(i)].sprite < m_sprites.size())
            draws.push_back({m_crawlers[size_t(i)].depth, Kind::Crawler, i});
    }
    // Eggs sit on a plant: just in front of it
    for (int i = 0; i < nEggs / kEggsPerClutch; ++i) {
        const DcEgg &e = m_eggs[size_t(i)];
        float depth = 0.5f;
        for (const DecorItem &d : m_decor) {
            if (d.sway > 0 && decorRect(d).contains(QPointF(e.x, e.y)))
                depth = decorDepth(d) - 0.001f;
        }
        draws.push_back({depth, Kind::Eggs, i});
    }
    for (int i : order) {
        const DcFish &f = m_fish[size_t(i)];
        float depth = f.depth;
        // In a cave: at the cave's depth, between its back and its front
        if (f.sheltered) {
            for (size_t k = 0; k < caveOpenings.size(); ++k) {
                if (!caveOpenings[k].isNull() && caveOpenings[k].adjusted(-f.w, -f.h, f.w, f.h).contains(QPointF(f.x, f.y)))
                    depth = decorDepth(m_decor[k]);
            }
        }
        draws.push_back({depth, Kind::Fish, i});
    }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw &a, const Draw &b) { return a.depth > b.depth; });

    const float eggSize = std::clamp(float(height()) / 1080.f, 0.7f, 2.f) * 12.f;
    for (const Draw &item : draws) {
        switch (item.kind) {
        case Kind::Decor:
        case Kind::CaveFront: {
            const DecorItem &d = m_decor[size_t(item.index)];
            const uint32_t sprite = item.kind == Kind::CaveFront ? d.frontSprite : d.sprite;
            const QRectF box = decorRect(d);
            // A busy computer stirs the current
            const float sway = d.sway * float(1 + 1.5 * agitation());
            // Farther back, a little more of the water's colour (as with fish)
            const FishMesh::DecorPose pose{float(box.center().x()), float(box.bottom()), float(box.width()),
                                           float(box.height()), sway, d.phase, d.placement.flip,
                                           decorDepth(d) * 0.45f};
            FishMesh::writeDecor(pose, m_sprites[sprite].uv, float(m_flow), v, idx, base);
            v += FishMesh::kVertsPerDecor;
            idx += FishMesh::kIndicesPerDecor;
            base += FishMesh::kVertsPerDecor;
            break;
        }
        case Kind::Crawler: {
            const DcCrawler &c = m_crawlers[size_t(item.index)];
            // (lifted while hopping about in play)
            const FishMesh::CrawlerPose pose{c.x, c.y - c.hop, c.w, c.h, c.facing, c.phase, c.tilt, c.buried,
                                             FishMesh::Gait(c.gait)};
            FishMesh::writeCrawler(pose, m_sprites[c.sprite].uv, v, idx, base);
            v += FishMesh::kVertsPerFish;
            idx += FishMesh::kIndicesPerFish;
            base += FishMesh::kVertsPerFish;
            break;
        }
        case Kind::Eggs: {
            // A rosette; they start to wobble just before they hatch
            const DcEgg &e = m_eggs[size_t(item.index)];
            const float wobble = e.progress > 0.85f ? std::sin(float(m_time) * 18.f + e.x) * 1.5f : 0.f;
            for (int k = 0; k < kEggsPerClutch; ++k) {
                const float a = float(k) * 2.4f + e.x * 0.1f;
                const float r = k == 0 ? 0.f : eggSize * (0.7f + 0.25f * float(k % 2));
                const float ex = e.x + std::cos(a) * r + wobble, ey = e.y + std::sin(a) * r * 0.8f;
                FishMesh::writeQuad(QRectF(ex - eggSize / 2, ey - eggSize / 2, eggSize, eggSize), m_eggUv, 0.95f, v,
                                    idx, base);
                v += 4;
                idx += 6;
                base += 4;
            }
            break;
        }
        case Kind::Fish: {
            const DcFish &f = m_fish[size_t(item.index)];
            const SpriteInfo &sprite = m_sprites[f.sprite];
            const FishMesh::Pose pose{f.x, f.y, f.w, f.h, f.facing, f.phase, f.amplitude, f.tilt, f.depth};
            FishMesh::writeFish(pose, sprite.uv, *sprite.style, sprite.weights, v, idx, base);
            v += FishMesh::kVertsPerFish;
            idx += FishMesh::kIndicesPerFish;
            base += FishMesh::kVertsPerFish;
            break;
        }
        }
    }

    for (int i = 0; i < nFood; ++i) {
        // Pellets come in three sizes, one for each size of fish
        const DcFood &p = m_food[i];
        const float size = p.size > 0 ? p.size : kFoodSize, half = size * 0.5f;
        FishMesh::writeQuad(QRectF(p.x - half, p.y - half, size, size), m_foodUv, p.opacity, v, idx, base);
        v += 4;
        idx += 6;
        base += 4;
    }
    if (nBubbles > 0) {
        for (const DcBubble &b : m_bubbles) {
            FishMesh::writeQuad(QRectF(b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2), m_bubbleUv,
                                b.alpha, v, idx, base);
            v += 4;
            idx += 6;
            base += 4;
        }
        // The wand's bubbles, around the fish they carry; each firms up as a
        // fish decides to come in
        for (const DcCarry &b : m_carries) {
            FishMesh::writeQuad(QRectF(b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2), m_bigBubbleUv,
                                0.6f + 0.4f * b.alpha, v, idx, base);
            v += 4;
            idx += 6;
            base += 4;
        }
        for (const DcBubble &h : m_hearts) {
            FishMesh::writeQuad(QRectF(h.x - h.radius, h.y - h.radius, h.radius * 2, h.radius * 2), m_heartUv,
                                h.alpha, v, idx, base);
            v += 4;
            idx += 6;
            base += 4;
        }
        // Rings spread out along the surface and fade
        for (const DcRipple &r : m_ripples) {
            const float w = r.strength * (2.f + 9.f * r.progress);
            const float h = w * 0.22f;
            const float alpha = (1.f - r.progress) * (1.f - r.progress);
            FishMesh::writeQuad(QRectF(r.x - w, r.y - h, w * 2, h * 2), m_rippleUv, alpha, v, idx, base);
            v += 4;
            idx += 6;
            base += 4;
        }
    }

    node->commit();
    if (m_profile.enabled) {
        // Runs on the render thread while the GUI thread waits, so no race
        m_profile.paintNs += profileTimer.nsecsElapsed();
        ++m_profile.paints;
    }
    return node;
}
