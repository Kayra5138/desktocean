#include "fishlibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kManifestVersion = 1;

// Serialises writers (editor, several wallpaper instances seeding at once).
class WriteLock
{
public:
    WriteLock()
        : m_lock(QDir(FishLibrary::directory()).filePath(QStringLiteral(".lock")))
    {
        QDir().mkpath(FishLibrary::directory());
        m_locked = m_lock.tryLock(5000);
    }
    bool locked() const { return m_locked; }

private:
    QLockFile m_lock;
    bool m_locked = false;
};

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool save(const QList<FishEntry> &entries, QString *error)
{
    QJsonArray list;
    for (const FishEntry &e : entries) {
        list.append(QJsonObject{
            {QStringLiteral("id"), e.id},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("file"), e.file},
            {QStringLiteral("scale"), e.scale},
            {QStringLiteral("count"), e.count},
        });
    }
    const QJsonObject root{{QStringLiteral("version"), kManifestVersion}, {QStringLiteral("fish"), list}};

    QSaveFile file(FishLibrary::manifestPath());
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

} // namespace

QString FishEntry::path() const
{
    return QDir(FishLibrary::directory()).filePath(file);
}

namespace FishLibrary {

QString directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/desktocean/fish");
}

QString manifestPath()
{
    return QDir(directory()).filePath(QStringLiteral("fish.json"));
}

bool exists()
{
    return QFileInfo::exists(manifestPath());
}

QList<FishEntry> load(QString *error)
{
    QFile file(manifestPath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (file.exists())
            setError(error, file.errorString());
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        setError(error, parseError.errorString());
        return {};
    }

    QList<FishEntry> entries;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("fish")).toArray()) {
        const QJsonObject o = v.toObject();
        FishEntry e;
        e.id = o.value(QStringLiteral("id")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.file = o.value(QStringLiteral("file")).toString();
        e.scale = std::clamp(o.value(QStringLiteral("scale")).toDouble(1.0), 0.3, 3.0);
        e.count = std::clamp(o.value(QStringLiteral("count")).toInt(1), 0, 50);
        // Never follow a path out of the library directory
        if (e.id.isEmpty() || e.file.isEmpty() || e.file.contains(QLatin1Char('/')))
            continue;
        entries << e;
    }
    return entries;
}

std::optional<FishEntry> add(const QImage &sprite, const QString &name, double scale, int count, QString *error)
{
    const QImage cropped = cropToContent(sprite);
    if (cropped.isNull()) {
        setError(error, QStringLiteral("The drawing is empty"));
        return std::nullopt;
    }

    WriteLock lock;
    if (!lock.locked()) {
        setError(error, QStringLiteral("The fish library is busy, try again"));
        return std::nullopt;
    }

    FishEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.name = name.trimmed().isEmpty() ? QStringLiteral("Fish") : name.trimmed();
    e.file = e.id + QStringLiteral(".png");
    e.scale = std::clamp(scale, 0.3, 3.0);
    e.count = std::clamp(count, 1, 50);

    // Sprite first, manifest second: readers never see an entry without its file
    QSaveFile png(e.path());
    if (!png.open(QIODevice::WriteOnly) || !cropped.save(&png, "PNG") || !png.commit()) {
        setError(error, png.errorString());
        return std::nullopt;
    }

    QString loadError;
    QList<FishEntry> entries = load(&loadError);
    if (!loadError.isEmpty()) {
        setError(error, loadError);
        QFile::remove(e.path());
        return std::nullopt;
    }
    entries << e;
    if (!save(entries, error)) {
        QFile::remove(e.path());
        return std::nullopt;
    }
    return e;
}

bool remove(const QString &id, QString *error)
{
    WriteLock lock;
    if (!lock.locked()) {
        setError(error, QStringLiteral("The fish library is busy, try again"));
        return false;
    }
    QList<FishEntry> entries = load(error);
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const FishEntry &e) { return e.id == id; });
    if (it == entries.end())
        return true;
    const QString path = it->path();
    entries.erase(it);
    if (!save(entries, error))
        return false;
    QFile::remove(path);
    return true;
}

bool update(const FishEntry &entry, QString *error)
{
    WriteLock lock;
    if (!lock.locked()) {
        setError(error, QStringLiteral("The fish library is busy, try again"));
        return false;
    }
    QList<FishEntry> entries = load(error);
    for (FishEntry &e : entries) {
        if (e.id == entry.id) {
            e.name = entry.name;
            e.scale = std::clamp(entry.scale, 0.3, 3.0);
            e.count = std::clamp(entry.count, 0, 50);
            return save(entries, error);
        }
    }
    setError(error, QStringLiteral("No such fish"));
    return false;
}

QImage cropToContent(const QImage &image, int margin)
{
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    int left = img.width(), right = -1, top = img.height(), bottom = -1;
    for (int y = 0; y < img.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) > 8) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }
    if (right < 0)
        return {};
    QRect box(QPoint(left, top), QPoint(right, bottom));
    box.adjust(-margin, -margin, margin, margin);
    box = box.intersected(img.rect());
    return img.copy(box);
}

} // namespace FishLibrary
