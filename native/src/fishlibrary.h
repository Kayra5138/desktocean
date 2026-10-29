#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QStringList>

#include <optional>

// Where one copy of a plant or decoration stands in the tank. Coordinates are
// fractions of the tank size; (x, y) is the middle of the bottom edge.
struct Placement {
    double x = 0.5;
    double y = 0.93;
    double scale = 1.0;
    bool front = false;   // drawn in front of the fish
    bool flip = false;    // mirrored
};

// The user's fish collection on disk, shared by the wallpaper and the editor:
//
//   ~/.local/share/desktocean/fish/
//       fish.json             manifest (written atomically)
//       <id>[-<n>].png        one sprite per entry, head facing right, cropped
//       <...>.wiggle.png      optional motion mask, same size (grey 128 = normal)
//
// Besides fish it holds plants (sway in the current) and decorations (still),
// which are placed in the tank rather than swimming.
//
// The editor writes, the wallpaper watches the directory and reloads.
struct FishEntry {
    // Crawlers walk on the bottom (crabs, snails...); their gait is in `style`
    enum class Kind { Fish, Plant, Decor, Crawler };

    QString id;
    Kind kind = Kind::Fish;
    QString name;                          // given by the user; empty = automatic
    // What to call it: the given name, or "Fish 3", or "Baby of A & B" (which
    // follows the parents' names). Worked out by load(), never stored.
    QString displayName;
    int number = 0;                        // for automatic names, per kind; 1, 2, ...
    QStringList names;                     // fish: names of single copies; empty = automatic
    QString file;                          // sprite file name inside directory()
    QString wiggle;                        // wiggle mask file name, or empty
    QString style = QStringLiteral("classic");  // FishMesh swim style id
    double scale = 1.0;                    // size multiplier
    int count = 1;                         // how many of this fish swim in the tank
    double sway = 1.0;                     // plants: how much they move in the current
    QList<Placement> placements;           // plants and decorations: where they stand
    qint64 bornAt = 0;                     // babies: when they were born (ms since epoch); 0 = drawn
    QStringList parents;                   // babies: ids of the two parents
    // Fish: mouth on the sprite as fractions of its width and height (head
    // right); negative = not marked, guess it from the drawing
    QPointF mouth{-1, -1};
    // Decorations: a cave opening fish can swim into, as fractions of the
    // sprite (an arch: rounded top, straight sides down to the bottom); null = none
    QRectF opening;

    QString path() const;
    QString wigglePath() const;            // empty when there is no mask
    // Name of copy k: its own name, or the species name with a number when
    // there are several
    QString fishName(int k) const;
    QString defaultFishName(int k) const;
};

// What the editor hands over: the drawing and, optionally, its wiggle mask
// (same size as the drawing; null means "move like the style says").
struct FishDrawing {
    QImage sprite;
    QImage wiggle;
    QPointF mouth{-1, -1};   // in sprite pixels; negative = not marked
    QRectF opening;          // decorations: cave opening in sprite pixels; null = none
};

namespace FishLibrary {

QString directory();
QString manifestPath();
bool exists();
// The wallpaper's state (friendships, theme). Outside directory() on purpose:
// the tank reloads whenever something in there changes.
QString statePath();

// The editor asks the wallpaper to switch theme by writing this request; the
// wallpaper applies a request only once (by its time) and stores the theme in
// its own settings, which stay the source of truth.
struct ThemeRequest {
    QString theme;
    qint64 at = 0;   // ms since epoch; 0 = none
};
bool requestTheme(const QString &theme, QString *error = nullptr);
ThemeRequest themeRequest();
QString themeRequestPath();

QString kindName(FishEntry::Kind kind);
FishEntry::Kind kindFromName(const QString &name);

// Starter decorations are added once per library, even if the user deletes them
bool decorSeeded();
void markDecorSeeded();

QList<FishEntry> load(QString *error = nullptr);

// Crops the drawing to its visible pixels, saves it and appends an entry with
// meta's name, style, scale and count.
std::optional<FishEntry> add(const FishDrawing &drawing, const FishEntry &meta, QString *error = nullptr);
bool remove(const QString &id, QString *error = nullptr);
// Updates name, style, scale, count, sway and placements of the entry with meta.id
bool update(const FishEntry &meta, QString *error = nullptr);
// Replaces the drawing of an existing fish, keeping its id so it stays in the
// tank where it is; also applies meta like update()
bool replaceDrawing(const FishEntry &meta, const FishDrawing &drawing, QString *error = nullptr);
// Mirrors an existing fish (and its wiggle mask) so the head points the other way
bool flip(const QString &id, QString *error = nullptr);
// Names single copies of a fish; an empty name means automatic
bool setFishNames(const QString &id, const QStringList &names, QString *error = nullptr);

// Sharing a single fish, plant or decoration as one "<name>.desktocean" file:
// JSON {format: "desktocean-fish", version: 1, kind, name, style, scale, sway,
// mouth, sprite: base64 PNG, wiggle: base64 PNG (optional)}
bool exportEntry(const QString &id, const QString &path, QString *error = nullptr);
// Adds the fish in a .desktocean file as a new entry (one copy, no family).
// The file is untrusted: sizes are capped and every field is checked.
std::optional<FishEntry> importFile(const QString &path, QString *error = nullptr);

// Best guess for an unmarked mouth: the front tip of the drawing, at the
// height of its nose. Fractions of the image (head facing right).
QPointF guessMouth(const QImage &sprite);

// How grown up a fish is: 1 for drawn fish; babies start at 0.4 and reach
// 1 after a couple of hours
double growth(const FishEntry &entry);

// Bounding box of pixels with alpha above a small threshold, grown by `margin`
// and clipped to the image; null when nothing is visible
QRect contentBox(const QImage &image, int margin = 4);
QImage cropToContent(const QImage &image, int margin = 4);

} // namespace FishLibrary
