#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QtQml/qqmlregistration.h>

#include "fishlibrary.h"

// Every placement of every plant and decoration, flattened into one list for
// the tank layout page. Each change is written to the library straight away,
// so the wallpaper follows along live.
class LayoutModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // Colours of the wallpaper's theme {shallow, deep, sandLight, sandDark}
    Q_PROPERTY(QVariantMap themeColors READ themeColors NOTIFY themeChanged)
    // The wallpaper's theme id; setting it asks the wallpaper to switch
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    // [{id, label}] of every theme, for a ComboBox
    Q_PROPERTY(QVariantList themes READ themes CONSTANT)

public:
    enum Role { EntryIdRole = Qt::UserRole + 1, NameRole, ImageRole, AspectRole, KindRole, XRole, YRole, ScaleRole, FrontRole, FlipRole };

    explicit LayoutModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString lastError() const { return m_lastError; }
    QVariantMap themeColors() const { return m_themeColors; }
    QString theme() const { return m_theme; }
    void setTheme(const QString &theme);
    QVariantList themes() const;

    Q_INVOKABLE void reload();
    // x, y: middle of the bottom edge, as fractions of the tank
    Q_INVOKABLE void move(int row, double x, double y);
    Q_INVOKABLE void setScale(int row, double scale);
    Q_INVOKABLE void setFront(int row, bool front);
    Q_INVOKABLE void flip(int row);
    // Returns the row of the copy, or -1
    Q_INVOKABLE int duplicate(int row);
    Q_INVOKABLE void remove(int row);
    // Places another copy of library entry `entryId` on the sand; returns its row or -1
    Q_INVOKABLE int add(const QString &entryId);

Q_SIGNALS:
    void countChanged();
    void lastErrorChanged();
    void themeChanged();

private:
    struct Item {
        int entry;       // index into m_entries
        int placement;   // index into that entry's placements
    };

    Placement *placementAt(int row);
    bool save(int entry);
    void rebuildItems();
    void setError(const QString &error);

    QList<FishEntry> m_entries;   // plants and decorations only
    QList<Item> m_items;
    QHash<QString, double> m_aspect;   // entry id -> sprite height / width
    QString m_lastError;
    QVariantMap m_themeColors;
    QString m_theme;
};
