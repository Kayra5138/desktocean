#include "librarymodel.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

LibraryModel::LibraryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QDir().mkpath(FishLibrary::directory());
    m_watcher.addPath(FishLibrary::directory());
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &LibraryModel::reload);
    reload();
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const FishEntry &e = m_entries[index.row()];
    switch (role) {
    case IdRole:
        return e.id;
    case NameRole:
        return e.name;
    case ImageRole:
        return QUrl::fromLocalFile(e.path());
    case CopiesRole:
        return e.count;
    case ScaleRole:
        return e.scale;
    }
    return {};
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {{IdRole, "fishId"}, {NameRole, "name"}, {ImageRole, "image"}, {CopiesRole, "copies"}, {ScaleRole, "scale"}};
}

int LibraryModel::swimming() const
{
    int n = 0;
    for (const FishEntry &e : m_entries)
        n += e.count;
    return n;
}

void LibraryModel::reload()
{
    QString error;
    QList<FishEntry> entries = FishLibrary::load(&error);
    if (!error.isEmpty()) {
        setError(error);
        return;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

bool LibraryModel::remove(const QString &id)
{
    QString error;
    if (!FishLibrary::remove(id, &error)) {
        setError(error);
        return false;
    }
    reload();
    return true;
}

bool LibraryModel::modify(const QString &id, const std::function<void(FishEntry &)> &change)
{
    for (FishEntry e : std::as_const(m_entries)) {
        if (e.id != id)
            continue;
        change(e);
        QString error;
        if (!FishLibrary::update(e, &error)) {
            setError(error);
            return false;
        }
        reload();
        return true;
    }
    return false;
}

bool LibraryModel::setCopies(const QString &id, int copies)
{
    return modify(id, [copies](FishEntry &e) { e.count = copies; });
}

bool LibraryModel::setScale(const QString &id, double scale)
{
    return modify(id, [scale](FishEntry &e) { e.scale = scale; });
}

bool LibraryModel::rename(const QString &id, const QString &name)
{
    return modify(id, [name](FishEntry &e) { e.name = name.trimmed(); });
}

void LibraryModel::setError(const QString &error)
{
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}
