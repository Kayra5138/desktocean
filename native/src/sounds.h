#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QUrl>

#include <array>
#include <memory>

class QProcess;

// The aquarium's optional sounds (scripts/make-sounds.py makes them). One per
// process, shared by all tanks, so several screens don't play everything
// twice. Off unless the user turns them on; each sound has a minimum gap so
// a busy tank never chatters.
//
// Sounds are played by a small helper process (pw-play, or paplay) rather than
// an audio stack inside plasmashell: whatever happens on the audio side can
// never stall the desktop and the panels that share the process with us.
class SoundBoard
{
public:
    enum class Sound { Bubble, Plop, Nom, Chime, Hatch, Count };

    static SoundBoard &instance();
    ~SoundBoard();

    void setDirectory(const QUrl &dir);
    void setEnabled(bool enabled);
    void setVolume(double volume);    // 0 .. 1
    void setAmbient(bool ambient);    // quiet water loop
    // Some tank is visible and running (ambient only plays then)
    void setActive(const void *tank, bool active);

    void play(Sound sound, double loudness = 1.0);

private:
    SoundBoard() = default;
    void updateAmbient();
    void startAmbient();
    QString file(const char *name) const;

    QUrl m_dir;
    bool m_enabled = false;
    bool m_ambient = false;
    double m_volume = 0.4;
    QHash<const void *, bool> m_active;
    std::array<QElapsedTimer, size_t(Sound::Count)> m_last;
    // The water loop: a player we restart each time the file ends
    QProcess *m_ambientPlayer = nullptr;
    QElapsedTimer m_ambientStarted;
    int m_quickExits = 0;   // a player that keeps dying at once is given up on
};
