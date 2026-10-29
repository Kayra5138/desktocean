#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QUrl>

#include <array>
#include <memory>

class QSoundEffect;

// The aquarium's optional sounds (scripts/make-sounds.py makes them). One per
// process, shared by all tanks, so several screens don't play everything
// twice. Off unless the user turns them on; each sound has a minimum gap so
// a busy tank never chatters.
class SoundBoard
{
public:
    enum class Sound { Bubble, Plop, Nom, Chime, Hatch, Count };

    static SoundBoard &instance();

    void setDirectory(const QUrl &dir);
    void setEnabled(bool enabled);
    void setVolume(double volume);    // 0 .. 1
    void setAmbient(bool ambient);    // quiet water loop
    // Some tank is visible and running (ambient only plays then)
    void setActive(const void *tank, bool active);

    void play(Sound sound, double loudness = 1.0);

private:
    SoundBoard() = default;
    void load();
    void updateAmbient();

    QUrl m_dir;
    bool m_enabled = false;
    bool m_ambient = false;
    double m_volume = 0.4;
    QHash<const void *, bool> m_active;
    std::array<std::unique_ptr<QSoundEffect>, size_t(Sound::Count)> m_effects;
    std::unique_ptr<QSoundEffect> m_ambientLoop;
    std::array<QElapsedTimer, size_t(Sound::Count)> m_last;
};
