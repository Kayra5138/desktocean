#include "fishlibrary.h"

#include <QDateTime>
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

const QString kBusy = QStringLiteral("The fish library is busy, try again");

// Plain file names only: never follow a path out of the library directory
bool safeName(const QString &name)
{
    return !name.contains(QLatin1Char('/')) && name != QLatin1String("..") && name != QLatin1String(".");
}

bool save(const QList<FishEntry> &entries, QString *error)
{
    QJsonArray list;
    for (const FishEntry &e : entries) {
        QJsonObject o{
            {QStringLiteral("id"), e.id},
            {QStringLiteral("kind"), FishLibrary::kindName(e.kind)},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("file"), e.file},
            {QStringLiteral("style"), e.style},
            {QStringLiteral("scale"), e.scale},
            {QStringLiteral("count"), e.count},
        };
        if (!e.wiggle.isEmpty())
            o.insert(QStringLiteral("wiggle"), e.wiggle);
        if (e.kind != FishEntry::Kind::Fish) {
            o.insert(QStringLiteral("sway"), e.sway);
            QJsonArray places;
            for (const Placement &p : e.placements) {
                places.append(QJsonObject{{QStringLiteral("x"), p.x},
                                          {QStringLiteral("y"), p.y},
                                          {QStringLiteral("scale"), p.scale},
                                          {QStringLiteral("front"), p.front},
                                          {QStringLiteral("flip"), p.flip}});
            }
            o.insert(QStringLiteral("placements"), places);
        }
        list.append(o);
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

bool writePng(const QString &path, const QImage &image, QString *error)
{
    QSaveFile png(path);
    if (!png.open(QIODevice::WriteOnly) || !image.save(&png, "PNG") || !png.commit()) {
        setError(error, png.errorString());
        return false;
    }
    return true;
}

void applyMeta(FishEntry &e, const FishEntry &meta, int minCount)
{
    if (!meta.name.trimmed().isEmpty())
        e.name = meta.name.trimmed();
    e.style = meta.style.isEmpty() ? QStringLiteral("classic") : meta.style;
    e.scale = std::clamp(meta.scale, 0.3, 3.0);
    e.count = std::clamp(meta.count, minCount, 50);
    e.kind = meta.kind;
    e.sway = std::clamp(meta.sway, 0.0, 3.0);
    e.placements.clear();
    for (Placement p : meta.placements) {
        p.x = std::clamp(p.x, -0.2, 1.2);
        p.y = std::clamp(p.y, 0.0, 1.2);
        p.scale = std::clamp(p.scale, 0.1, 5.0);
        e.placements << p;
    }
}

// Crops sprite and mask with the same box; false if the drawing is empty
bool cropDrawing(const FishDrawing &in, QImage *sprite, QImage *wiggle, QString *error)
{
    const QRect box = FishLibrary::contentBox(in.sprite);
    if (box.isNull()) {
        setError(error, QStringLiteral("The drawing is empty"));
        return false;
    }
    *sprite = in.sprite.convertToFormat(QImage::Format_ARGB32).copy(box);
    *wiggle = in.wiggle.isNull() ? QImage() : in.wiggle.convertToFormat(QImage::Format_Grayscale8).copy(box);
    return true;
}

// Writes sprite (and mask) under fresh file names for `e`; a new name each
// time so image caches and the tank notice the change
bool writeDrawing(FishEntry &e, const QImage &sprite, const QImage &wiggle, QString *error)
{
    const QString stem = e.id + QLatin1Char('-') + QString::number(QDateTime::currentMSecsSinceEpoch());
    e.file = stem + QStringLiteral(".png");
    e.wiggle = wiggle.isNull() ? QString() : stem + QStringLiteral(".wiggle.png");
    if (!writePng(e.path(), sprite, error))
        return false;
    if (!wiggle.isNull() && !writePng(e.wigglePath(), wiggle, error)) {
        QFile::remove(e.path());
        return false;
    }
    return true;
}

void removeFiles(const FishEntry &e)
{
    QFile::remove(e.path());
    if (!e.wiggle.isEmpty())
        QFile::remove(e.wigglePath());
}

} // namespace

QString FishEntry::path() const
{
    return QDir(FishLibrary::directory()).filePath(file);
}

QString FishEntry::wigglePath() const
{
    return wiggle.isEmpty() ? QString() : QDir(FishLibrary::directory()).filePath(wiggle);
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

QString statePath()
{
    return QFileInfo(directory()).dir().filePath(QStringLiteral("state.json"));
}

QString kindName(FishEntry::Kind kind)
{
    switch (kind) {
    case FishEntry::Kind::Plant:
        return QStringLiteral("plant");
    case FishEntry::Kind::Decor:
        return QStringLiteral("decor");
    case FishEntry::Kind::Fish:
        break;
    }
    return QStringLiteral("fish");
}

FishEntry::Kind kindFromName(const QString &name)
{
    if (name == QLatin1String("plant"))
        return FishEntry::Kind::Plant;
    if (name == QLatin1String("decor"))
        return FishEntry::Kind::Decor;
    return FishEntry::Kind::Fish;
}

bool decorSeeded()
{
    return QFileInfo::exists(QDir(directory()).filePath(QStringLiteral(".decor-seeded")));
}

void markDecorSeeded()
{
    QFile marker(QDir(directory()).filePath(QStringLiteral(".decor-seeded")));
    if (marker.open(QIODevice::WriteOnly))
        marker.write("1\n");
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
        e.wiggle = o.value(QStringLiteral("wiggle")).toString();
        e.style = o.value(QStringLiteral("style")).toString(QStringLiteral("classic"));
        e.scale = std::clamp(o.value(QStringLiteral("scale")).toDouble(1.0), 0.3, 3.0);
        e.count = std::clamp(o.value(QStringLiteral("count")).toInt(1), 0, 50);
        e.kind = kindFromName(o.value(QStringLiteral("kind")).toString());
        e.sway = std::clamp(o.value(QStringLiteral("sway")).toDouble(1.0), 0.0, 3.0);
        for (const QJsonValue &pv : o.value(QStringLiteral("placements")).toArray()) {
            const QJsonObject po = pv.toObject();
            Placement p;
            p.x = po.value(QStringLiteral("x")).toDouble(0.5);
            p.y = po.value(QStringLiteral("y")).toDouble(0.93);
            p.scale = po.value(QStringLiteral("scale")).toDouble(1.0);
            p.front = po.value(QStringLiteral("front")).toBool();
            p.flip = po.value(QStringLiteral("flip")).toBool();
            e.placements << p;
        }
        if (e.id.isEmpty() || e.file.isEmpty() || !safeName(e.file) || !safeName(e.wiggle))
            continue;
        entries << e;
    }
    return entries;
}

std::optional<FishEntry> add(const FishDrawing &drawing, const FishEntry &meta, QString *error)
{
    QImage sprite, wiggle;
    if (!cropDrawing(drawing, &sprite, &wiggle, error))
        return std::nullopt;

    WriteLock lock;
    if (!lock.locked()) {
        setError(error, kBusy);
        return std::nullopt;
    }

    FishEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.name = QStringLiteral("Fish");
    applyMeta(e, meta, 1);

    // Files first, manifest second: readers never see an entry without its files
    if (!writeDrawing(e, sprite, wiggle, error))
        return std::nullopt;

    QString loadError;
    QList<FishEntry> entries = load(&loadError);
    if (!loadError.isEmpty()) {
        setError(error, loadError);
        removeFiles(e);
        return std::nullopt;
    }
    entries << e;
    if (!save(entries, error)) {
        removeFiles(e);
        return std::nullopt;
    }
    return e;
}

bool remove(const QString &id, QString *error)
{
    WriteLock lock;
    if (!lock.locked()) {
        setError(error, kBusy);
        return false;
    }
    QList<FishEntry> entries = load(error);
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const FishEntry &e) { return e.id == id; });
    if (it == entries.end())
        return true;
    const FishEntry gone = *it;
    entries.erase(it);
    if (!save(entries, error))
        return false;
    removeFiles(gone);
    return true;
}

bool update(const FishEntry &meta, QString *error)
{
    WriteLock lock;
    if (!lock.locked()) {
        setError(error, kBusy);
        return false;
    }
    QList<FishEntry> entries = load(error);
    for (FishEntry &e : entries) {
        if (e.id == meta.id) {
            applyMeta(e, meta, 0);
            return save(entries, error);
        }
    }
    setError(error, QStringLiteral("No such fish"));
    return false;
}

bool replaceDrawing(const FishEntry &meta, const FishDrawing &drawing, QString *error)
{
    QImage sprite, wiggle;
    if (!cropDrawing(drawing, &sprite, &wiggle, error))
        return false;

    WriteLock lock;
    if (!lock.locked()) {
        setError(error, kBusy);
        return false;
    }
    QList<FishEntry> entries = load(error);
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const FishEntry &e) { return e.id == meta.id; });
    if (it == entries.end()) {
        setError(error, QStringLiteral("No such fish"));
        return false;
    }

    const FishEntry old = *it;
    FishEntry updated = *it;
    FishEntry merged = meta;
    // A redrawn plant or decoration stays where it was placed
    if (merged.placements.isEmpty())
        merged.placements = old.placements;
    applyMeta(updated, merged, 0);
    if (!writeDrawing(updated, sprite, wiggle, error))
        return false;
    *it = updated;
    if (!save(entries, error)) {
        removeFiles(updated);
        return false;
    }
    removeFiles(old);
    return true;
}

bool flip(const QString &id, QString *error)
{
    const QList<FishEntry> entries = load(error);
    for (const FishEntry &e : entries) {
        if (e.id != id)
            continue;
        const QImage sprite(e.path());
        if (sprite.isNull()) {
            setError(error, QStringLiteral("Cannot read %1").arg(e.path()));
            return false;
        }
        const QImage wiggle = e.wiggle.isEmpty() ? QImage() : QImage(e.wigglePath());
        const FishDrawing flipped{sprite.flipped(Qt::Horizontal),
                                  wiggle.isNull() ? QImage() : wiggle.flipped(Qt::Horizontal)};
        return replaceDrawing(e, flipped, error);
    }
    setError(error, QStringLiteral("No such fish"));
    return false;
}

QRect contentBox(const QImage &image, int margin)
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
    return box.intersected(img.rect());
}

QImage cropToContent(const QImage &image, int margin)
{
    const QRect box = contentBox(image, margin);
    return box.isNull() ? QImage() : image.convertToFormat(QImage::Format_ARGB32).copy(box);
}

} // namespace FishLibrary
