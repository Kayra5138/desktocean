#include "sounds.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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
    {"chime.wav", 0.3, 2500},
    {"hatch.wav", 0.6, 2000},
}};
constexpr double kAmbientGain = 0.18;

// pw-play takes a 0..1 volume; paplay a 0..65536 one
struct Player {
    QString program;
    bool pipewire = false;
};
const Player &player()
{
    static const Player p = [] {
        if (const QString pw = QStandardPaths::findExecutable(QStringLiteral("pw-play")); !pw.isEmpty())
            return Player{pw, true};
        return Player{QStandardPaths::findExecutable(QStringLiteral("paplay")), false};
    }();
    return p;
}

QStringList playerArgs(const QString &file, double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (player().pipewire)
        return {QStringLiteral("--volume"), QString::number(volume, 'f', 3), QStringLiteral("--media-role"),
                QStringLiteral("Game"), file};
    return {QStringLiteral("--volume=%1").arg(qRound(volume * 65536)), file};
}
} // namespace

SoundBoard &SoundBoard::instance()
{
    static SoundBoard board;
    return board;
}

SoundBoard::~SoundBoard()
{
    if (m_ambientPlayer) {
        m_ambientPlayer->disconnect();
        m_ambientPlayer->kill();
        m_ambientPlayer->waitForFinished(500);
        delete m_ambientPlayer;
    }
}

QString SoundBoard::file(const char *name) const
{
    return m_dir.resolved(QUrl(QString::fromLatin1(name))).toLocalFile();
}

void SoundBoard::setDirectory(const QUrl &dir)
{
    if (dir == m_dir)
        return;
    m_dir = dir;
    if (m_ambientPlayer)
        m_ambientPlayer->kill();   // restarts with the new file
    updateAmbient();
}

void SoundBoard::setEnabled(bool enabled)
{
    m_enabled = enabled;
    updateAmbient();
}

void SoundBoard::setVolume(double volume)
{
    const double old = m_volume;
    m_volume = std::clamp(volume, 0.0, 1.0);
    // The loop picks the new volume up when it next starts; a big change, now
    if (m_ambientPlayer && std::abs(old - m_volume) > 0.05)
        m_ambientPlayer->kill();
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

void SoundBoard::play(Sound sound, double loudness)
{
    if (!m_enabled || m_volume <= 0 || m_active.isEmpty() || m_dir.isEmpty() || player().program.isEmpty())
        return;
    const size_t i = size_t(sound);
    if (m_last[i].isValid() && m_last[i].elapsed() < kSounds[i].gapMs)
        return;
    const QString path = file(kSounds[i].file);
    if (!QFileInfo::exists(path))
        return;
    m_last[i].start();
    QProcess::startDetached(player().program, playerArgs(path, m_volume * kSounds[i].gain * loudness));
}

void SoundBoard::updateAmbient()
{
    const bool on = m_enabled && m_ambient && !m_active.isEmpty() && !m_dir.isEmpty() && !player().program.isEmpty()
                 && m_quickExits < 5;
    if (!on) {
        if (m_ambientPlayer && m_ambientPlayer->state() != QProcess::NotRunning) {
            m_ambientPlayer->disconnect();
            m_ambientPlayer->kill();
            m_ambientPlayer->deleteLater();
            m_ambientPlayer = nullptr;
        }
        return;
    }
    if (!m_ambientPlayer || m_ambientPlayer->state() == QProcess::NotRunning)
        startAmbient();
}

void SoundBoard::startAmbient()
{
    const QString path = file("ambient.wav");
    if (!QFileInfo::exists(path))
        return;
    if (!m_ambientPlayer) {
        m_ambientPlayer = new QProcess;
        m_ambientPlayer->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        QObject::connect(m_ambientPlayer, &QProcess::finished, m_ambientPlayer, [this] {
            // The file ended (or the volume changed): play it again. A player
            // that dies straight away every time is given up on.
            m_quickExits = m_ambientStarted.elapsed() < 1000 ? m_quickExits + 1 : 0;
            updateAmbient();
        });
    }
    m_ambientStarted.start();
    m_ambientPlayer->start(player().program, playerArgs(path, m_volume * kAmbientGain));
}
