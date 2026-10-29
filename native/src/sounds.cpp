#include "sounds.h"

#include <QSoundEffect>

namespace {
struct Info {
    const char *file;
    double gain;     // relative loudness
    int gapMs;       // never more often than this
};
constexpr std::array<Info, size_t(SoundBoard::Sound::Count)> kSounds = {{
    {"bubble.wav", 0.25, 700},
    {"plop.wav", 0.6, 120},
    {"nom.wav", 0.35, 250},
    {"chime.wav", 0.45, 1500},
    {"hatch.wav", 0.6, 2000},
}};
constexpr double kAmbientGain = 0.18;
} // namespace

SoundBoard &SoundBoard::instance()
{
    static SoundBoard board;
    return board;
}

void SoundBoard::setDirectory(const QUrl &dir)
{
    if (dir == m_dir)
        return;
    m_dir = dir;
    // Reloaded on demand
    for (auto &e : m_effects)
        e.reset();
    m_ambientLoop.reset();
    updateAmbient();
}

void SoundBoard::setEnabled(bool enabled)
{
    m_enabled = enabled;
    updateAmbient();
}

void SoundBoard::setVolume(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_ambientLoop)
        m_ambientLoop->setVolume(float(m_volume * kAmbientGain));
}

void SoundBoard::setAmbient(bool ambient)
{
    m_ambient = ambient;
    updateAmbient();
}

void SoundBoard::setActive(const void *tank, bool active)
{
    if (active)
        m_active.insert(tank, true);
    else
        m_active.remove(tank);
    updateAmbient();
}

void SoundBoard::load()
{
    if (m_dir.isEmpty())
        return;
    for (size_t i = 0; i < kSounds.size(); ++i) {
        if (m_effects[i])
            continue;
        auto effect = std::make_unique<QSoundEffect>();
        effect->setSource(m_dir.resolved(QUrl(QString::fromLatin1(kSounds[i].file))));
        m_effects[i] = std::move(effect);
    }
}

void SoundBoard::play(Sound sound, double loudness)
{
    if (!m_enabled || m_volume <= 0 || m_active.isEmpty())
        return;
    const size_t i = size_t(sound);
    if (m_last[i].isValid() && m_last[i].elapsed() < kSounds[i].gapMs)
        return;
    load();
    QSoundEffect *effect = m_effects[i].get();
    if (!effect || effect->status() == QSoundEffect::Error)
        return;
    m_last[i].start();
    effect->setVolume(float(std::clamp(m_volume * kSounds[i].gain * loudness, 0.0, 1.0)));
    effect->play();
}

void SoundBoard::updateAmbient()
{
    const bool on = m_enabled && m_ambient && !m_active.isEmpty() && !m_dir.isEmpty();
    if (!on) {
        if (m_ambientLoop)
            m_ambientLoop->stop();
        return;
    }
    if (!m_ambientLoop) {
        m_ambientLoop = std::make_unique<QSoundEffect>();
        m_ambientLoop->setSource(m_dir.resolved(QUrl(QStringLiteral("ambient.wav"))));
        m_ambientLoop->setLoopCount(QSoundEffect::Infinite);
    }
    m_ambientLoop->setVolume(float(m_volume * kAmbientGain));
    if (!m_ambientLoop->isPlaying())
        m_ambientLoop->play();
}
