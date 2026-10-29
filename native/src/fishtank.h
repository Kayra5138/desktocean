#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <desktocean_sim.h>

#include "fishlibrary.h"
#include "fishmesh.h"

#include <vector>

// The living part of the aquarium: owns the Rust simulation, ticks it at `fps`,
// reads the cursor, and draws every fish and food pellet in one scene graph
// node (one draw call). Nothing runs per frame in JavaScript.
//
// The fish come from the user's fish library on disk (see fishlibrary.h); the
// tank watches it and new fish drop in as soon as the editor saves them.
class FishTank : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    // Paused by the user (context menu or widget); the tank only ticks when
    // running and not paused
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    // Tick once per screen refresh (e.g. 165 Hz) instead of at `fps`
    Q_PROPERTY(bool matchRefresh READ matchRefresh WRITE setMatchRefresh NOTIFY fpsChanged)
    // Sprites (head facing right) copied into an empty library on first run
    Q_PROPERTY(QList<QUrl> starterFish READ starterFish WRITE setStarterFish NOTIFY starterFishChanged)
    // Plants and decorations ("decor-<name>.svg") added once to every library
    Q_PROPERTY(QList<QUrl> starterDecor READ starterDecor WRITE setStarterDecor NOTIFY starterDecorChanged)
    Q_PROPERTY(QColor fogColor READ fogColor WRITE setFogColor NOTIFY fogColorChanged)
    // Water and sand colours by id (see themes.h); remembered for the editor
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    // {shallow, deep, sandLight, sandDark} of the theme, for the water shader
    Q_PROPERTY(QVariantMap themeColors READ themeColors NOTIFY themeChanged)
    // Day and night follow the local clock; when off it is always day
    Q_PROPERTY(bool followTimeOfDay READ followTimeOfDay WRITE setFollowTimeOfDay NOTIFY followTimeOfDayChanged)
    // Pretend it is this hour (0..24) instead of the clock; -1 = use the clock
    Q_PROPERTY(qreal hourOverride READ hourOverride WRITE setHourOverride NOTIFY hourOverrideChanged)
    // 0 = night, 1 = day; drives the water shader and dims the fish
    Q_PROPERTY(qreal daylight READ daylight NOTIFY daylightChanged)
    // Busy computer, restless water: CPU load stirs the current and bubbles
    Q_PROPERTY(bool reactToCpu READ reactToCpu WRITE setReactToCpu NOTIFY reactToCpuChanged)
    // Smoothed CPU load 0..1 (0 when reactToCpu is off); drives the water's agitation
    Q_PROPERTY(qreal agitation READ agitation NOTIFY agitationChanged)
    // Water clock: like `time`, but runs faster when the water is agitated
    Q_PROPERTY(qreal flow READ flow NOTIFY timeChanged)
    // Close friends have babies now and then, up to maxFish fish in the tank
    Q_PROPERTY(bool babies READ babies WRITE setBabies NOTIFY babiesChanged)
    Q_PROPERTY(int maxFish READ maxFish WRITE setMaxFish NOTIFY babiesChanged)
    // Speeds up babies for testing (1 = normal)
    Q_PROPERTY(qreal lifeSpeed READ lifeSpeed WRITE setLifeSpeed NOTIFY babiesChanged)

    Q_PROPERTY(int fishCount READ fishCount NOTIFY fishCountChanged)
    // Seconds of simulated time; drives the water shader
    Q_PROPERTY(qreal time READ time NOTIFY timeChanged)
    Q_PROPERTY(int foodCount READ foodCount NOTIFY foodCountChanged)
    // Curious fish are following the cursor right now
    Q_PROPERTY(bool lureActive READ lureActive NOTIFY lureActiveChanged)
    // Cursor in item coordinates, or (-1, -1) when it is not over the tank
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY timeChanged)
    // Name the fish under the cursor (hoveredName, above the fish at hoveredPoint)
    Q_PROPERTY(bool showNames READ showNames WRITE setShowNames NOTIFY showNamesChanged)
    Q_PROPERTY(QString hoveredName READ hoveredName NOTIFY hoverChanged)
    Q_PROPERTY(QPointF hoveredPoint READ hoveredPoint NOTIFY hoverChanged)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthyChanged)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(QString libraryPath READ libraryPath CONSTANT)

public:
    explicit FishTank(QQuickItem *parent = nullptr);
    ~FishTank() override;

    bool running() const { return m_running; }
    void setRunning(bool running);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    int fps() const { return m_fps; }
    void setFps(int fps);
    bool matchRefresh() const { return m_matchRefresh; }
    void setMatchRefresh(bool match);
    QList<QUrl> starterFish() const { return m_starterFish; }
    void setStarterFish(const QList<QUrl> &urls);
    QList<QUrl> starterDecor() const { return m_starterDecor; }
    void setStarterDecor(const QList<QUrl> &urls);
    QColor fogColor() const { return m_fogColor; }
    void setFogColor(const QColor &color);
    QString theme() const { return m_theme; }
    void setTheme(const QString &theme);
    QVariantMap themeColors() const;
    bool followTimeOfDay() const { return m_followTimeOfDay; }
    void setFollowTimeOfDay(bool follow);
    qreal hourOverride() const { return m_hourOverride; }
    void setHourOverride(qreal hour);
    qreal daylight() const { return m_daylight; }
    bool reactToCpu() const { return m_reactToCpu; }
    void setReactToCpu(bool react);
    qreal agitation() const { return m_reactToCpu ? m_cpuLoad : 0; }
    qreal flow() const { return m_flow; }
    bool babies() const { return m_babies; }
    void setBabies(bool babies);
    int maxFish() const { return m_maxFish; }
    void setMaxFish(int maxFish);
    qreal lifeSpeed() const { return m_lifeSpeed; }
    void setLifeSpeed(qreal speed);

    int fishCount() const { return int(m_fish.size()); }
    qreal time() const { return m_time; }
    int foodCount() const { return int(m_food.size()); }
    bool lureActive() const { return m_lureActive; }
    QPointF pointer() const { return m_pointer; }
    bool showNames() const { return m_showNames; }
    void setShowNames(bool show);
    QString hoveredName() const { return m_hoveredName; }
    QPointF hoveredPoint() const { return m_hoveredPoint; }
    bool healthy() const { return m_healthy; }
    QString coreVersion() const;
    QString libraryPath() const;

    // Drops food from the surface above x (item coordinates); x < 0 = random spot
    Q_INVOKABLE void feed(qreal x = -1);
    // Drops food above the cursor if it is over the tank, else somewhere random
    Q_INVOKABLE void feedAtCursor();
    Q_INVOKABLE void clearFood();
    // Something happened (a notification): a fish swims up and blows bubbles
    Q_INVOKABLE void announce();
    // Development aid (preview --matchmake): every pair becomes best friends
    Q_INVOKABLE void befriendAll();
    // Starts the editor (page: "" drawing, "fish", "layout"); false if it is not installed
    Q_INVOKABLE bool openEditor(const QString &page = {});
    static bool launchEditor(const QString &page = {});
    // One line for the diagnostics overlay: hunger, friends, rivals
    Q_INVOKABLE QString stats() const;
    qreal averageHunger() const;
    // (friendships, rivalries)
    QPair<int, int> relationCounts() const;

Q_SIGNALS:
    void runningChanged();
    void pausedChanged();
    void fpsChanged();
    void starterFishChanged();
    void starterDecorChanged();
    void fogColorChanged();
    void themeChanged();
    // The editor asked for this theme; the wallpaper stores it in its settings
    void themeRequested(const QString &theme);
    void followTimeOfDayChanged();
    void hourOverrideChanged();
    void daylightChanged();
    void reactToCpuChanged();
    void agitationChanged();
    void babiesChanged();
    void fishCountChanged();
    void timeChanged();
    void foodCountChanged();
    void lureActiveChanged();
    void showNamesChanged();
    void hoverChanged();
    void healthyChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void tick();
    void updateHover();
    void onFrameSwapped();
    void pullState();
    void updateTimer();
    void loadLibrary(bool animateNew);
    void seedLibrary();
    // Adds the starter plants and decorations; `only` limits it to one of them
    // (used to bring decorations added in later versions to existing tanks)
    void seedDecor(const QString &only = {});
    void watchLibrary();
    void checkRequests();
    void rebuildAtlas(const QList<QImage> &images);
    void updateDaylight();
    void sampleCpu();
    void applyLifeSpeed();
    void handleBirths();
    void recordBaby(const DcBirth &birth);
    void refreshGrowth();
    void loadState();
    void saveState() const;
    QPointF cursorInItem() const;
    void checkHealth();

    DcSim *m_sim = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadDebounce;
    QTimer m_saveTimer;
    QTimer m_cpuTimer;
    QTimer m_growthTimer;
    quint64 m_cpuBusy = 0;
    quint64 m_cpuTotal = 0;
    bool m_libraryLoaded = false;
    // Time of the last editor request already handled (requests.json)
    qint64 m_handledRequest = 0;
    bool m_stateLoaded = false;
    // Which library fish each simulated fish is a copy of (spec id -> entry id)
    QHash<quint64, QString> m_specOwner;
    QHash<quint64, QString> m_specName;   // name of every copy, for the hover label
    bool m_showNames = false;
    QString m_hoveredName;
    QPointF m_hoveredPoint;

    bool m_running = true;
    bool m_paused = false;
    int m_fps = 30;
    bool m_matchRefresh = false;
    // Refresh-synced ticking is running: each swapped frame ticks and asks for the next
    bool m_vsyncActive = false;
    QTimer m_vsyncWatchdog;
    QMetaObject::Connection m_frameSwapped;
    QList<QUrl> m_starterFish;
    QList<QUrl> m_starterDecor;
    QColor m_fogColor = QColor(0x0b, 0x30, 0x48);
    QString m_theme = QStringLiteral("tropical");
    bool m_followTimeOfDay = true;
    qreal m_hourOverride = -1;
    qreal m_daylight = 1;
    bool m_reactToCpu = true;
    bool m_babies = true;
    int m_maxFish = 30;
    qreal m_lifeSpeed = 1;
    // Library entries of babies just born -> where to put them once loaded
    QHash<QString, QPointF> m_newborns;
    // DESKTOCEAN_PROFILE=1: average time spent per tick / per geometry update, logged every 5 s
    struct Profile {
        bool enabled = false;
        QElapsedTimer window;
        qint64 stepNs = 0, pullNs = 0, paintNs = 0;
        int ticks = 0, paints = 0;
    };
    Profile m_profile;
    void reportProfile();

    // Current population, kept so babies can grow without reloading images
    std::vector<DcFishSpec> m_specs;
    std::vector<FishEntry> m_specEntry;   // the library entry of each spec
    qreal m_cpuLoad = 0;
    qreal m_flow = 0;

    qreal m_time = 0;
    bool m_lureActive = false;
    QPointF m_pointer{-1, -1};
    bool m_healthy = true;

    // Latest simulation state, copied on the GUI thread, read during sync
    std::vector<DcFish> m_fish;
    std::vector<DcFood> m_food;
    std::vector<DcBubble> m_bubbles;
    std::vector<DcRipple> m_ripples;

    // One per library fish drawing, indexed by DcFish::sprite
    struct SpriteInfo {
        QRectF uv;                                  // normalised atlas rect
        float aspect = 1.f;                         // height / width
        const FishMesh::Style *style = nullptr;
        std::vector<float> weights;                 // wiggle mask per vertex, or empty
    };
    std::vector<SpriteInfo> m_sprites;

    // Plants and decorations standing in the tank
    struct DecorItem {
        uint32_t sprite;
        float sway;          // 0 for decorations
        float phase;
        Placement placement;
        // Caves: the opening (fractions of the sprite) and the sprite with the
        // opening cut out, drawn over the fish inside
        QRectF opening;
        uint32_t frontSprite = 0;
    };
    // Where a decoration is drawn: bottom-centre anchored, sized by height
    QRectF decorRect(const DecorItem &d) const;
    // Tells the sim where the cave openings are
    void updateShelters();
    std::vector<DecorItem> m_decor;

    // Sprite atlas: every fish drawing plus the food pellet
    QImage m_atlas;
    QRectF m_foodUv;
    QRectF m_bubbleUv;
    QRectF m_rippleUv;
    bool m_atlasDirty = false;
};
