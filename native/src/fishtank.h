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

    Q_PROPERTY(int fishCount READ fishCount NOTIFY fishCountChanged)
    // Seconds of simulated time; drives the water shader
    Q_PROPERTY(qreal time READ time NOTIFY timeChanged)
    Q_PROPERTY(int foodCount READ foodCount NOTIFY foodCountChanged)
    // Curious fish are following the cursor right now
    Q_PROPERTY(bool lureActive READ lureActive NOTIFY lureActiveChanged)
    // Cursor in item coordinates, or (-1, -1) when it is not over the tank
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY timeChanged)
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

    int fishCount() const { return int(m_fish.size()); }
    qreal time() const { return m_time; }
    int foodCount() const { return int(m_food.size()); }
    bool lureActive() const { return m_lureActive; }
    QPointF pointer() const { return m_pointer; }
    bool healthy() const { return m_healthy; }
    QString coreVersion() const;
    QString libraryPath() const;

    // Drops food from the surface above x (item coordinates); x < 0 = random spot
    Q_INVOKABLE void feed(qreal x = -1);
    // Drops food above the cursor if it is over the tank, else somewhere random
    Q_INVOKABLE void feedAtCursor();
    Q_INVOKABLE void clearFood();
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
    void followTimeOfDayChanged();
    void hourOverrideChanged();
    void daylightChanged();
    void fishCountChanged();
    void timeChanged();
    void foodCountChanged();
    void lureActiveChanged();
    void healthyChanged();

protected:
    void componentComplete() override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void tick();
    void pullState();
    void updateTimer();
    void loadLibrary(bool animateNew);
    void seedLibrary();
    void seedDecor();
    void watchLibrary();
    void rebuildAtlas(const QList<QImage> &images);
    void updateDaylight();
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
    bool m_libraryLoaded = false;
    bool m_stateLoaded = false;
    // Which library fish each simulated fish is a copy of (spec id -> entry id)
    QHash<quint64, QString> m_specOwner;

    bool m_running = true;
    bool m_paused = false;
    int m_fps = 30;
    QList<QUrl> m_starterFish;
    QList<QUrl> m_starterDecor;
    QColor m_fogColor = QColor(0x0b, 0x30, 0x48);
    QString m_theme = QStringLiteral("tropical");
    bool m_followTimeOfDay = true;
    qreal m_hourOverride = -1;
    qreal m_daylight = 1;

    qreal m_time = 0;
    bool m_lureActive = false;
    QPointF m_pointer{-1, -1};
    bool m_healthy = true;

    // Latest simulation state, copied on the GUI thread, read during sync
    std::vector<DcFish> m_fish;
    std::vector<DcFood> m_food;

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
    };
    std::vector<DecorItem> m_decor;

    // Sprite atlas: every fish drawing plus the food pellet
    QImage m_atlas;
    QRectF m_foodUv;
    bool m_atlasDirty = false;
};
