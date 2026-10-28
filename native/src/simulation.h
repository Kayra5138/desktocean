#pragma once

#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QtQml/qqmlregistration.h>

struct DcSim;

// QML face of the Rust simulation core.
//
// fish() and food() return flat arrays (see fishStride / foodStride) so a tick
// crosses into JavaScript once instead of once per fish property.
class Simulation : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int fishCount READ fishCount WRITE setFishCount NOTIFY fishCountChanged)
    Q_PROPERTY(bool healthy READ healthy NOTIFY healthyChanged)
    Q_PROPERTY(QString coreVersion READ coreVersion CONSTANT)
    Q_PROPERTY(int fishStride READ fishStride CONSTANT)
    Q_PROPERTY(int foodStride READ foodStride CONSTANT)

public:
    explicit Simulation(QObject *parent = nullptr);
    ~Simulation() override;

    int fishCount() const { return m_fishCount; }
    void setFishCount(int count);
    bool healthy() const { return m_healthy; }
    QString coreVersion() const;
    static int fishStride();
    static int foodStride();

    Q_INVOKABLE void resize(qreal width, qreal height);
    // x < 0 drops food somewhere random
    Q_INVOKABLE void feed(qreal x = -1);
    Q_INVOKABLE void clearFood();
    // Curious fish are following the cursor right now
    Q_INVOKABLE bool lureActive() const;
    // pointer with negative coordinates means "not over the aquarium"
    Q_INVOKABLE void step(qreal dt, QPointF pointer);
    // [x, y, w, h, facing, phase, amplitude, tilt, depth] per fish
    Q_INVOKABLE QList<qreal> fish() const;
    // [x, y, opacity] per pellet
    Q_INVOKABLE QList<qreal> food() const;

Q_SIGNALS:
    void fishCountChanged();
    void healthyChanged();

private:
    void checkHealth();

    DcSim *m_sim = nullptr;
    int m_fishCount = 7;
    bool m_healthy = true;
};
