#include "librarymodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    // The wallpaper saves friendships about once a minute
    m_relationsRefresh.setInterval(30 * 1000);
    connect(&m_relationsRefresh, &QTimer::timeout, this, &LibraryModel::reload);
    m_relationsRefresh.start();
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
        return e.displayName;
    case CustomNameRole:
        return e.name;
    case FishNamesRole: {
        QStringList own;
        for (int k = 0; k < e.count; ++k)
            own << e.names.value(k);
        return own;
    }
    case DefaultFishNamesRole: {
        QStringList defaults;
        for (int k = 0; k < e.count; ++k)
            defaults << e.defaultFishName(k);
        return defaults;
    }
    case ImageRole:
        return QUrl::fromLocalFile(e.path());
    case CopiesRole:
        return e.count;
    case ScaleRole:
        return e.scale;
    case StyleRole:
        return e.style;
    case WiggleRole:
        return e.wiggle.isEmpty() ? QUrl() : QUrl::fromLocalFile(e.wigglePath());
    case FriendsRole:
        return m_friends.value(e.id).join(QStringLiteral(", "));
    case RivalsRole:
        return m_rivals.value(e.id).join(QStringLiteral(", "));
    case KindRole:
        return FishLibrary::kindName(e.kind);
    case SwayRole:
        return e.sway;
    case PlacedRole:
        return int(e.placements.size());
    case CharactersRole:
        return m_characters.value(e.id);
    case MouthRole:
        return e.mouth;
    case FamilyRole: {
        // "Baby of Orange & Blue · growing 45%" for babies, "" for drawn fish
        if (e.bornAt <= 0)
            return QString();
        const double g = FishLibrary::growth(e);
        const QString grown = g < 1.0 ? QStringLiteral(" · growing %1%").arg(qRound((g - 0.4) / 0.6 * 100)) : QString();
        return QStringLiteral("Born in the tank") + grown;
    }
    }
    return {};
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {{IdRole, "fishId"},   {NameRole, "name"},       {ImageRole, "image"},  {CopiesRole, "copies"},
            {ScaleRole, "scale"}, {StyleRole, "swimStyle"}, {WiggleRole, "wiggle"},
            {FriendsRole, "friends"}, {RivalsRole, "rivals"}, {KindRole, "kind"},
            {SwayRole, "sway"},       {PlacedRole, "placed"},   {FamilyRole, "family"},
            {CharactersRole, "characters"}, {MouthRole, "mouth"},
            {CustomNameRole, "customName"}, {FishNamesRole, "fishNames"}, {DefaultFishNamesRole, "defaultFishNames"}};
}

QVariantMap LibraryModel::get(int row) const
{
    QVariantMap map;
    const QModelIndex idx = index(row);
    if (!idx.isValid())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return map;
}

int LibraryModel::swimming() const
{
    int n = 0;
    for (const FishEntry &e : m_entries)
        n += e.count;
    return n;
}

void LibraryModel::setKind(const QString &kind)
{
    if (kind == m_kind)
        return;
    m_kind = kind;
    reload();
    Q_EMIT kindChanged();
}

void LibraryModel::reload()
{
    QString error;
    QList<FishEntry> all = FishLibrary::load(&error);
    if (!error.isEmpty()) {
        setError(error);
        return;
    }
    QList<FishEntry> entries;
    for (const FishEntry &e : std::as_const(all)) {
        const QString kind = FishLibrary::kindName(e.kind);
        // Crawlers are listed with the fish: they live in the tank the same way
        const bool creature = e.kind == FishEntry::Kind::Fish || e.kind == FishEntry::Kind::Crawler;
        if (kind == m_kind || (m_kind == QLatin1String("fish") && creature)
            || (m_kind == QLatin1String("scenery") && !creature))
            entries << e;
    }
    beginResetModel();
    m_entries = entries;
    loadRelations();
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

bool LibraryModel::setFishNames(const QString &id, const QStringList &names)
{
    QString error;
    if (!FishLibrary::setFishNames(id, names, &error)) {
        setError(error);
        return false;
    }
    reload();
    return true;
}

bool LibraryModel::flip(const QString &id)
{
    QString error;
    if (!FishLibrary::flip(id, &error)) {
        setError(error);
        return false;
    }
    reload();
    return true;
}

void LibraryModel::loadRelations()
{
    m_friends.clear();
    m_rivals.clear();
    m_characters.clear();
    QFile file(FishLibrary::statePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    QHash<QString, QString> names;
    for (const FishEntry &e : std::as_const(m_entries))
        names.insert(e.id, e.displayName);
    auto collect = [&](const QString &key, QHash<QString, QStringList> &out) {
        for (const QJsonValue &pair : root.value(key).toArray()) {
            const QString a = pair.toArray().at(0).toString(), b = pair.toArray().at(1).toString();
            if (!names.contains(a) || !names.contains(b))
                continue;
            if (!out[a].contains(names[b]))
                out[a] << names[b];
            if (!out[b].contains(names[a]))
                out[b] << names[a];
        }
    };
    collect(QStringLiteral("friends"), m_friends);
    collect(QStringLiteral("rivals"), m_rivals);

    // ["shy", "explorer", "shy"] -> "2× shy, explorer"
    const QJsonObject characters = root.value(QStringLiteral("characters")).toObject();
    for (auto it = characters.constBegin(); it != characters.constEnd(); ++it) {
        QStringList order;
        QHash<QString, int> counts;
        for (const QJsonValue &v : it.value().toArray()) {
            const QString name = v.toString();
            if (!counts.contains(name))
                order << name;
            ++counts[name];
        }
        QStringList parts;
        for (const QString &name : std::as_const(order))
            parts << (counts[name] > 1 ? QStringLiteral("%1× %2").arg(counts[name]).arg(name) : name);
        m_characters.insert(it.key(), parts.join(QStringLiteral(", ")));
    }
}

void LibraryModel::setError(const QString &error)
{
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}
