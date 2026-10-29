#include "fishlibrary.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <functional>

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
// Automatic names: "Fish 3", and "Baby of A & B" for babies, following
// whatever the parents are called now
void resolveNames(QList<FishEntry> &entries)
{
    // Older libraries stored the defaults as names; those become automatic
    QHash<QString, int> index;
    QHash<int, int> lastNumber;
    for (int i = 0; i < entries.size(); ++i) {
        FishEntry &e = entries[i];
        index.insert(e.id, i);
        const QString kindDefault = e.kind == FishEntry::Kind::Fish      ? QStringLiteral("Fish")
                                    : e.kind == FishEntry::Kind::Crawler ? QStringLiteral("Crawler")
                                    : e.kind == FishEntry::Kind::Plant   ? QStringLiteral("Plant")
                                                                       : QStringLiteral("Decoration");
        if (e.name == kindDefault || (e.bornAt > 0 && e.name.startsWith(QLatin1String("Baby "))))
            e.name.clear();
        lastNumber[int(e.kind)] = std::max(lastNumber[int(e.kind)], e.number);
    }
    for (FishEntry &e : entries) {
        if (e.number <= 0)
            e.number = ++lastNumber[int(e.kind)];
    }
    // Babies' names depend on their parents', which may be babies too
    std::function<QString(int, int)> nameOf = [&](int i, int depth) -> QString {
        const FishEntry &e = entries[i];
        if (!e.name.isEmpty())
            return e.name;
        if (e.bornAt > 0 && depth < 3) {
            QStringList parents;
            for (const QString &p : e.parents) {
                const int j = index.value(p, -1);
                if (j >= 0 && j != i && !parents.contains(nameOf(j, depth + 1)))
                    parents << nameOf(j, depth + 1);
            }
            if (!parents.isEmpty())
                return QStringLiteral("Baby of %1").arg(parents.join(QStringLiteral(" & ")));
        }
        const QString base = e.bornAt > 0                            ? QStringLiteral("Baby")
                             : e.kind == FishEntry::Kind::Fish    ? QStringLiteral("Fish")
                             : e.kind == FishEntry::Kind::Crawler ? QStringLiteral("Crawler")
                             : e.kind == FishEntry::Kind::Plant   ? QStringLiteral("Plant")
                                                                : QStringLiteral("Decoration");
        return QStringLiteral("%1 %2").arg(base).arg(e.number);
    };
    for (int i = 0; i < entries.size(); ++i)
        entries[i].displayName = nameOf(i, 0);
}

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
            {QStringLiteral("number"), e.number},
        };
        if (std::any_of(e.names.begin(), e.names.end(), [](const QString &n) { return !n.isEmpty(); }))
            o.insert(QStringLiteral("names"), QJsonArray::fromStringList(e.names));
        if (!e.wiggle.isEmpty())
            o.insert(QStringLiteral("wiggle"), e.wiggle);
        if (e.mouth.x() >= 0)
            o.insert(QStringLiteral("mouth"), QJsonArray{e.mouth.x(), e.mouth.y()});
        if (!e.opening.isNull())
            o.insert(QStringLiteral("opening"),
                     QJsonArray{e.opening.x(), e.opening.y(), e.opening.width(), e.opening.height()});
        if (e.bornAt > 0) {
            o.insert(QStringLiteral("bornAt"), QString::number(e.bornAt));
            o.insert(QStringLiteral("parents"), QJsonArray::fromStringList(e.parents));
        }
        if (e.kind == FishEntry::Kind::Plant || e.kind == FishEntry::Kind::Decor) {
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

// Crops sprite and mask with the same box, and turns the mouth into fractions
// of the cropped sprite; false if the drawing is empty
bool cropDrawing(const FishDrawing &in, QImage *sprite, QImage *wiggle, QPointF *mouth, QRectF *opening,
                 QString *error)
{
    const QRect box = FishLibrary::contentBox(in.sprite);
    if (box.isNull()) {
        setError(error, QStringLiteral("The drawing is empty"));
        return false;
    }
    *sprite = in.sprite.convertToFormat(QImage::Format_ARGB32).copy(box);
    *wiggle = in.wiggle.isNull() ? QImage() : in.wiggle.convertToFormat(QImage::Format_Grayscale8).copy(box);
    *mouth = in.mouth.x() >= 0
        ? QPointF(std::clamp((in.mouth.x() - box.x()) / box.width(), 0.0, 1.0),
                  std::clamp((in.mouth.y() - box.y()) / box.height(), 0.0, 1.0))
        : QPointF(-1, -1);
    *opening = QRectF();
    if (!in.opening.isNull()) {
        const QRectF r = in.opening.translated(-box.topLeft()).intersected(QRectF(QPointF(), box.size()));
        if (r.width() > 1 && r.height() > 1)
            *opening = QRectF(r.x() / box.width(), r.y() / box.height(), r.width() / box.width(),
                              r.height() / box.height());
    }
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

QString FishEntry::defaultFishName(int k) const
{
    return count > 1 ? QStringLiteral("%1 #%2").arg(displayName).arg(k + 1) : displayName;
}

QString FishEntry::fishName(int k) const
{
    const QString own = names.value(k);
    return own.isEmpty() ? defaultFishName(k) : own;
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

QString themeRequestPath()
{
    return QFileInfo(directory()).dir().filePath(QStringLiteral("requests.json"));
}

bool requestTheme(const QString &theme, QString *error)
{
    QDir().mkpath(QFileInfo(themeRequestPath()).path());
    const QJsonObject root{{QStringLiteral("theme"), theme},
                           {QStringLiteral("at"), QString::number(QDateTime::currentMSecsSinceEpoch())}};
    QSaveFile file(themeRequestPath());
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

ThemeRequest themeRequest()
{
    QFile file(themeRequestPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    return {o.value(QStringLiteral("theme")).toString(), o.value(QStringLiteral("at")).toString().toLongLong()};
}

QString kindName(FishEntry::Kind kind)
{
    switch (kind) {
    case FishEntry::Kind::Plant:
        return QStringLiteral("plant");
    case FishEntry::Kind::Decor:
        return QStringLiteral("decor");
    case FishEntry::Kind::Crawler:
        return QStringLiteral("crawler");
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
    if (name == QLatin1String("crawler"))
        return FishEntry::Kind::Crawler;
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
        e.name = o.value(QStringLiteral("name")).toString().trimmed();
        e.number = o.value(QStringLiteral("number")).toInt();
        for (const QJsonValue &nv : o.value(QStringLiteral("names")).toArray())
            e.names << nv.toString().trimmed();
        e.file = o.value(QStringLiteral("file")).toString();
        e.wiggle = o.value(QStringLiteral("wiggle")).toString();
        e.style = o.value(QStringLiteral("style")).toString(QStringLiteral("classic"));
        e.scale = std::clamp(o.value(QStringLiteral("scale")).toDouble(1.0), 0.3, 3.0);
        e.count = std::clamp(o.value(QStringLiteral("count")).toInt(1), 0, 50);
        e.kind = kindFromName(o.value(QStringLiteral("kind")).toString());
        e.sway = std::clamp(o.value(QStringLiteral("sway")).toDouble(1.0), 0.0, 3.0);
        e.bornAt = o.value(QStringLiteral("bornAt")).toString().toLongLong();
        const QJsonArray opening = o.value(QStringLiteral("opening")).toArray();
        if (opening.size() == 4) {
            const QRectF r(opening[0].toDouble(), opening[1].toDouble(), opening[2].toDouble(), opening[3].toDouble());
            e.opening = r.intersected(QRectF(0, 0, 1, 1));
            if (e.opening.width() < 0.02 || e.opening.height() < 0.02)
                e.opening = QRectF();
        }
        const QJsonArray mouth = o.value(QStringLiteral("mouth")).toArray();
        if (mouth.size() == 2)
            e.mouth = QPointF(std::clamp(mouth[0].toDouble(), 0.0, 1.0), std::clamp(mouth[1].toDouble(), 0.0, 1.0));
        for (const QJsonValue &pv : o.value(QStringLiteral("parents")).toArray())
            e.parents << pv.toString();
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
    resolveNames(entries);
    return entries;
}

std::optional<FishEntry> add(const FishDrawing &drawing, const FishEntry &meta, QString *error)
{
    QImage sprite, wiggle;
    QPointF mouth;
    QRectF opening;
    if (!cropDrawing(drawing, &sprite, &wiggle, &mouth, &opening, error))
        return std::nullopt;

    WriteLock lock;
    if (!lock.locked()) {
        setError(error, kBusy);
        return std::nullopt;
    }

    FishEntry e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    applyMeta(e, meta, 1);
    // Birth details are set once, here; edits never change them
    e.bornAt = meta.bornAt;
    e.parents = meta.parents;
    e.mouth = mouth;
    e.opening = opening;

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
    // Next free number for its automatic name
    for (const FishEntry &other : std::as_const(entries)) {
        if (other.kind == e.kind)
            e.number = std::max(e.number, other.number);
    }
    ++e.number;
    entries << e;
    if (!save(entries, error)) {
        removeFiles(e);
        return std::nullopt;
    }
    resolveNames(entries);
    return entries.last();
}

bool setFishNames(const QString &id, const QStringList &names, QString *error)
{
    WriteLock lock;
    if (!lock.locked()) {
        setError(error, kBusy);
        return false;
    }
    QList<FishEntry> entries = load(error);
    for (FishEntry &e : entries) {
        if (e.id == id) {
            e.names.clear();
            for (const QString &n : names)
                e.names << n.trimmed().left(60);
            while (!e.names.isEmpty() && e.names.last().isEmpty())
                e.names.removeLast();
            return save(entries, error);
        }
    }
    setError(error, QStringLiteral("No such fish"));
    return false;
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
    QPointF mouth;
    QRectF opening;
    if (!cropDrawing(drawing, &sprite, &wiggle, &mouth, &opening, error))
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
    updated.mouth = mouth;
    // A redrawing without a marked opening keeps the old one
    if (!opening.isNull())
        updated.opening = opening;
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
        // The mouth goes to the other side too
        const QPointF mouth = e.mouth.x() >= 0 ? QPointF((1 - e.mouth.x()) * sprite.width(), e.mouth.y() * sprite.height())
                                               : QPointF(-1, -1);
        // ...and so does a cave's opening
        const QRectF opening = e.opening.isNull()
            ? QRectF()
            : QRectF((1 - e.opening.right()) * sprite.width(), e.opening.y() * sprite.height(),
                     e.opening.width() * sprite.width(), e.opening.height() * sprite.height());
        const FishDrawing flipped{sprite.flipped(Qt::Horizontal),
                                  wiggle.isNull() ? QImage() : wiggle.flipped(Qt::Horizontal), mouth, opening};
        return replaceDrawing(e, flipped, error);
    }
    setError(error, QStringLiteral("No such fish"));
    return false;
}

QPointF guessMouth(const QImage &sprite)
{
    const QImage img = sprite.convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return {0.92, 0.56};
    // The frontmost visible column...
    int front = -1;
    for (int x = img.width() - 1; x >= 0 && front < 0; --x) {
        for (int y = 0; y < img.height(); ++y) {
            if (qAlpha(img.pixel(x, y)) > 64) {
                front = x;
                break;
            }
        }
    }
    if (front < 0)
        return {0.92, 0.56};
    // ...and the middle of what is drawn in the last few percent before it
    const int from = std::max(0, front - std::max(2, img.width() / 25));
    double sumY = 0;
    int n = 0;
    for (int x = from; x <= front; ++x) {
        for (int y = 0; y < img.height(); ++y) {
            if (qAlpha(img.pixel(x, y)) > 64) {
                sumY += y;
                ++n;
            }
        }
    }
    const double mx = std::max(0.0, front - img.width() * 0.02);
    return {mx / img.width(), n ? sumY / n / img.height() : 0.56};
}

double growth(const FishEntry &entry)
{
    if (entry.bornAt <= 0)
        return 1.0;
    constexpr double kGrowMs = 2 * 60 * 60 * 1000.0;
    const double age = double(QDateTime::currentMSecsSinceEpoch() - entry.bornAt);
    return std::clamp(0.4 + 0.6 * age / kGrowMs, 0.4, 1.0);
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
