#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "fishlibrary.h"

// The fish library as a list model for the "My aquarium" page. Follows changes
// on disk, so it stays in sync with saves from the drawing page.
class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // Fish swimming in the tank (sum of copies)
    Q_PROPERTY(int swimming READ swimming NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Which entries to list: "fish" (default), "plant", "decor" or "scenery" (plants and decorations)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1, NameRole, ImageRole, CopiesRole, ScaleRole, StyleRole, WiggleRole,
        FriendsRole, RivalsRole, KindRole, SwayRole, PlacedRole, FamilyRole, CharactersRole, MouthRole,
        CustomNameRole, FishNamesRole, DefaultFishNamesRole, OpeningRole, SpeedRole
    };

    explicit LibraryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int swimming() const;
    QString lastError() const { return m_lastError; }
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);

    Q_INVOKABLE void reload();
    // Row as {fishId, name, image, copies, scale, swimStyle, wiggle}, or empty
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool setCopies(const QString &id, int copies);
    Q_INVOKABLE bool setScale(const QString &id, double scale);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);
    Q_INVOKABLE bool flip(const QString &id);
    // Names single copies of a fish; empty strings mean automatic names
    Q_INVOKABLE bool setFishNames(const QString &id, const QStringList &names);

    // Fish for the family tree, one map per row: {fishId, row, name, image,
    // generation (0 = drawn), parents (ids still in the library, no repeats),
    // lostParents (how many have been removed)}
    Q_INVOKABLE QVariantList familyTree() const;
    // Saves one entry as a .desktocean file to share; returns an error message or ""
    Q_INVOKABLE QString exportFish(const QString &id, const QUrl &file);
    // A file name to suggest for exporting, e.g. "Bubbles.desktocean"
    Q_INVOKABLE QString exportName(const QString &id) const;
    // Adds the fish in a .desktocean file (URL or local path); returns
    // {name, kind} of the new entry, or {error}
    Q_INVOKABLE QVariantMap importFish(const QVariant &file);

Q_SIGNALS:
    void countChanged();
    void lastErrorChanged();
    void kindChanged();

private:
    bool modify(const QString &id, const std::function<void(FishEntry &)> &change);
    void setError(const QString &error);

    void loadRelations();

    QList<FishEntry> m_entries;
    // Names of friends / rivals per entry id, from the wallpaper's state.json
    QHash<QString, QStringList> m_friends;
    QHash<QString, QStringList> m_rivals;
    // Characters of the swimming copies per entry id, e.g. "explorer, 2× shy"
    QHash<QString, QString> m_characters;
    QTimer m_relationsRefresh;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_lastError;
    QString m_kind = QStringLiteral("fish");
};
