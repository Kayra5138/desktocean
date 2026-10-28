#include "simulation.h"

#include <QRandomGenerator>
#include <QtLogging>

#include <desktocean_sim.h>

#include <vector>

namespace {
constexpr int kFishStride = 9;
constexpr int kFoodStride = 3;
static_assert(sizeof(DcFish) == kFishStride * sizeof(float));
static_assert(sizeof(DcFood) == kFoodStride * sizeof(float));
}

Simulation::Simulation(QObject *parent)
    : QObject(parent)
    , m_sim(dc_sim_new(QRandomGenerator::global()->generate64()))
{
    dc_sim_set_fish_count(m_sim, m_fishCount);
    checkHealth();
}

Simulation::~Simulation()
{
    dc_sim_free(m_sim);
}

void Simulation::setFishCount(int count)
{
    count = qBound(0, count, 200);
    if (count == m_fishCount)
        return;
    m_fishCount = count;
    dc_sim_set_fish_count(m_sim, count);
    checkHealth();
    Q_EMIT fishCountChanged();
}

QString Simulation::coreVersion() const
{
    return QString::fromUtf8(dc_sim_version());
}

int Simulation::fishStride()
{
    return kFishStride;
}

int Simulation::foodStride()
{
    return kFoodStride;
}

void Simulation::resize(qreal width, qreal height)
{
    dc_sim_resize(m_sim, float(width), float(height));
    checkHealth();
}

void Simulation::feed(qreal x)
{
    dc_sim_feed(m_sim, float(x));
    checkHealth();
}

void Simulation::step(qreal dt, QPointF pointer)
{
    dc_sim_step(m_sim, float(dt), float(pointer.x()), float(pointer.y()));
    checkHealth();
}

QList<qreal> Simulation::fish() const
{
    const uint32_t n = dc_sim_fish(m_sim, nullptr, 0);
    std::vector<DcFish> buf(n);
    const uint32_t got = std::min(n, dc_sim_fish(m_sim, buf.data(), n));

    QList<qreal> out;
    out.reserve(got * kFishStride);
    for (uint32_t i = 0; i < got; ++i) {
        const DcFish &f = buf[i];
        out << f.x << f.y << f.w << f.h << f.facing << f.phase << f.amplitude << f.tilt << f.depth;
    }
    return out;
}

QList<qreal> Simulation::food() const
{
    const uint32_t n = dc_sim_food(m_sim, nullptr, 0);
    std::vector<DcFood> buf(n);
    const uint32_t got = std::min(n, dc_sim_food(m_sim, buf.data(), n));

    QList<qreal> out;
    out.reserve(got * kFoodStride);
    for (uint32_t i = 0; i < got; ++i)
        out << buf[i].x << buf[i].y << buf[i].opacity;
    return out;
}

void Simulation::checkHealth()
{
    const bool ok = dc_sim_ok(m_sim);
    if (ok == m_healthy)
        return;
    m_healthy = ok;
    if (!ok)
        qWarning("Desktocean: simulation core stopped after an internal error; the aquarium is frozen");
    Q_EMIT healthyChanged();
}
