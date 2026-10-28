#include "fishtank.h"
#include "fishlibrary.h"
#include "spritenode.h"
#include "tankhub.h"
#include "themes.h"

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

#include <QDateTime>

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
    // Only one tank records babies, or every screen would add its own
    if (!TankHub::isPrimary(this) || !m_babies)
        return;
    for (uint32_t i = 0; i < n; ++i)
        recordBaby(births[i]);
}

void FishTank::recordBaby(const DcBirth &birth)
{
    if (fishCount() >= m_maxFish)
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
    meta.name = a->id == b->id ? QStringLiteral("Baby %1").arg(a->name)
                               : QStringLiteral("Baby of %1 & %2").arg(a->name, b->name);
    meta.style = a->style;
    meta.scale = (a->scale + b->scale) / 2;
    meta.count = 1;
    meta.bornAt = QDateTime::currentMSecsSinceEpoch();
    meta.parents = {a->id, b->id};
    QString error;
    const auto added = FishLibrary::add(drawing, meta, &error);
    if (!added) {
        qWarning("Desktocean: could not record a baby: %s", qPrintable(error));
        return;
    }
    // Placed between the parents once the library reload brings it in
    m_newborns.insert(added->id, QPointF(birth.x, birth.y));
}

void FishTank::refreshGrowth()
{
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
    dc_sim_set_daylight(m_sim, float(d));
    if (std::abs(d - m_daylight) < 0.0005)
        return;
    m_daylight = d;
    update();
    Q_EMIT daylightChanged();
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

QString FishTank::stats() const
{
    const auto [friends, rivals] = relationCounts();
    return QStringLiteral("Hunger: %1%  Daylight: %2%\nFriends: %3  Rivals: %4  Bubbles: %5")
        .arg(qRound(averageHunger() * 100))
        .arg(qRound(m_daylight * 100))
        .arg(friends)
        .arg(rivals)
        .arg(m_bubbles.size());
}

void FishTank::loadState()
{
    QFile file(FishLibrary::statePath());
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
    // Character of every copy, by library fish, for the editor's cards
    static const char *const kCharacters[] = {"explorer", "homebody", "bottom dweller", "skimmer", "lazy", "playful", "shy"};
    QHash<QString, QStringList> characters;
    for (const DcFish &f : m_fish) {
        const QString owner = m_specOwner.value(f.id);
        if (!owner.isEmpty() && f.character < std::size(kCharacters))
            characters[owner] << QString::fromLatin1(kCharacters[f.character]);
    }
    QJsonObject characterMap;
    for (auto it = characters.cbegin(); it != characters.cend(); ++it)
        characterMap.insert(it.key(), QJsonArray::fromStringList(it.value()));

    const QJsonObject root{{QStringLiteral("relations"), relations},
                           {QStringLiteral("friends"), friends},
                           {QStringLiteral("rivals"), rivals},
                           {QStringLiteral("characters"), characterMap},
                           {QStringLiteral("theme"), m_theme}};
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
    const bool shouldRun = m_running && !m_paused && m_healthy && isComponentComplete() && width() > 0 && height() > 0;
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
    const qint64 stepped = t.nsecsElapsed();
    pullState();
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
    std::vector<FishEntry> specEntry;
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
        // Babies start small and grow up over a couple of hours
        const double growth = FishLibrary::growth(e);
        // Marked in the editor, or guessed from the front of the drawing
        const QPointF mouth = e.mouth.x() >= 0 ? e.mouth : FishLibrary::guessMouth(img);
        for (int k = 0; k < e.count; ++k) {
            const auto id = quint64(qHash(e.id, size_t(k)));
            specs.push_back({id, sprite, aspect, float(e.scale * growth), growth >= 1.0 ? 1u : 0u, float(mouth.x()),
                             float(mouth.y())});
            specEntry.push_back(e);
            owners.insert(id, e.id);
        }
    }

    m_sprites = std::move(sprites);
    // Back to front by where they stand: lower on the screen is closer
    std::sort(decor.begin(), decor.end(), [](const DecorItem &a, const DecorItem &b) { return a.placement.y < b.placement.y; });
    m_decor = std::move(decor);
    rebuildAtlas(images);
    dc_sim_set_population(m_sim, specs.data(), uint32_t(specs.size()), animateNew && m_libraryLoaded);
    m_specs = std::move(specs);
    m_specEntry = std::move(specEntry);
    m_specOwner = owners;
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
    const int nBubbles = node->hasAtlas() ? int(m_bubbles.size() + m_ripples.size()) : 0;
    const int nDecor = node->hasAtlas() ? int(std::count_if(m_decor.begin(), m_decor.end(), [&](const DecorItem &d) {
                                               return d.sprite < m_sprites.size();
                                           }))
                                         : 0;

    quint32 *idx = nullptr;
    FishMesh::Vertex *v = node->reserve(
        nFish * FishMesh::kVertsPerFish + (nFood + nBubbles) * 4 + nDecor * FishMesh::kVertsPerDecor,
        nFish * FishMesh::kIndicesPerFish + (nFood + nBubbles) * 6 + nDecor * FishMesh::kIndicesPerDecor, &idx);
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
            // A busy computer stirs the current
            const float sway = d.sway * float(1 + 1.5 * agitation());
            const FishMesh::DecorPose pose{float(d.placement.x * width()), float(d.placement.y * height()), h / aspect, h,
                                           sway, d.phase, d.placement.flip, front ? 0.f : 0.15f};
            FishMesh::writeDecor(pose, m_sprites[d.sprite].uv, float(m_flow), v, idx, base);
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
    if (nBubbles > 0) {
        for (const DcBubble &b : m_bubbles) {
            FishMesh::writeQuad(QRectF(b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2), m_bubbleUv,
                                b.alpha, v, idx, base);
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
    writeDecorLayer(true);

    node->commit();
    if (m_profile.enabled) {
        // Runs on the render thread while the GUI thread waits, so no race
        m_profile.paintNs += profileTimer.nsecsElapsed();
        ++m_profile.paints;
    }
    return node;
}
