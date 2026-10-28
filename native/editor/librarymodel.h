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

public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, ImageRole, CopiesRole, ScaleRole, StyleRole, WiggleRole, FriendsRole, RivalsRole };

    explicit LibraryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int swimming() const;
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void reload();
    // Row as {fishId, name, image, copies, scale, swimStyle, wiggle}, or empty
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool setCopies(const QString &id, int copies);
    Q_INVOKABLE bool setScale(const QString &id, double scale);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);
    Q_INVOKABLE bool flip(const QString &id);

Q_SIGNALS:
    void countChanged();
    void lastErrorChanged();

private:
    bool modify(const QString &id, const std::function<void(FishEntry &)> &change);
    void setError(const QString &error);

    void loadRelations();

    QList<FishEntry> m_entries;
    // Names of friends / rivals per entry id, from the wallpaper's state.json
    QHash<QString, QStringList> m_friends;
    QHash<QString, QStringList> m_rivals;
    QTimer m_relationsRefresh;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_lastError;
};
