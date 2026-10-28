#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include <optional>

// The user's fish collection on disk, shared by the wallpaper and the editor:
//
//   ~/.local/share/desktocean/fish/
//       fish.json        manifest (written atomically)
//       <id>.png         one sprite per entry, head facing right, cropped
//
// The editor writes, the wallpaper watches the directory and reloads.
struct FishEntry {
    QString id;
    QString name;
    QString file;        // file name inside directory()
    double scale = 1.0;  // size multiplier
    int count = 1;       // how many of this fish swim in the tank

    QString path() const;
};

namespace FishLibrary {

QString directory();
QString manifestPath();
bool exists();

QList<FishEntry> load(QString *error = nullptr);

// Crops `sprite` to its visible pixels, saves it and appends an entry.
std::optional<FishEntry> add(const QImage &sprite, const QString &name, double scale, int count,
                             QString *error = nullptr);
bool remove(const QString &id, QString *error = nullptr);
// Updates name/scale/count of the entry with entry.id
bool update(const FishEntry &entry, QString *error = nullptr);
// Replaces the drawing of an existing fish, keeping its id so it stays in the
// tank where it is; name/scale/count are taken from `entry`.
bool replaceSprite(const FishEntry &entry, const QImage &sprite, QString *error = nullptr);
// Mirrors an existing fish's drawing (head the other way)
bool flip(const QString &id, QString *error = nullptr);

// Bounding box of pixels with alpha above a small threshold, plus `margin`
QImage cropToContent(const QImage &image, int margin = 4);

} // namespace FishLibrary
