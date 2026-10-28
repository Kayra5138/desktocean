#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <desktocean_sim.h>

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
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    // Sprites (head facing right) copied into an empty library on first run
    Q_PROPERTY(QList<QUrl> starterFish READ starterFish WRITE setStarterFish NOTIFY starterFishChanged)
    Q_PROPERTY(QColor fogColor READ fogColor WRITE setFogColor NOTIFY fogColorChanged)

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
    int fps() const { return m_fps; }
    void setFps(int fps);
    QList<QUrl> starterFish() const { return m_starterFish; }
    void setStarterFish(const QList<QUrl> &urls);
    QColor fogColor() const { return m_fogColor; }
    void setFogColor(const QColor &color);

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
    // Starts the drawing editor; false if it is not installed
    Q_INVOKABLE bool openEditor();

Q_SIGNALS:
    void runningChanged();
    void fpsChanged();
    void starterFishChanged();
    void fogColorChanged();
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
    void watchLibrary();
    void rebuildAtlas(const QList<QImage> &sprites);
    QPointF cursorInItem() const;
    void checkHealth();

    DcSim *m_sim = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadDebounce;
    bool m_libraryLoaded = false;

    bool m_running = true;
    int m_fps = 30;
    QList<QUrl> m_starterFish;
    QColor m_fogColor = QColor(0x0b, 0x30, 0x48);

    qreal m_time = 0;
    bool m_lureActive = false;
    QPointF m_pointer{-1, -1};
    bool m_healthy = true;

    // Latest simulation state, copied on the GUI thread, read during sync
    std::vector<DcFish> m_fish;
    std::vector<DcFood> m_food;

    // Sprite atlas: one rect per library sprite plus the food pellet
    QImage m_atlas;
    std::vector<QRectF> m_spriteUv;   // normalised atlas rects, indexed by DcFish::sprite
    QRectF m_foodUv;
    bool m_atlasDirty = false;
};
