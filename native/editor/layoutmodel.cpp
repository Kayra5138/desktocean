#include "layoutmodel.h"
#include "themes.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <QImageReader>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>

LayoutModel::LayoutModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

int LayoutModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant LayoutModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Item &item = m_items[index.row()];
    const FishEntry &e = m_entries[item.entry];
    const Placement &p = e.placements[item.placement];
    switch (role) {
    case EntryIdRole:
        return e.id;
    case NameRole:
        return e.displayName;
    case ImageRole:
        return QUrl::fromLocalFile(e.path());
    case AspectRole:
        return m_aspect.value(e.id, 1.0);
    case KindRole:
        return FishLibrary::kindName(e.kind);
    case XRole:
        return p.x;
    case YRole:
        return p.y;
    case ScaleRole:
        return p.scale;
    case FrontRole:
        return p.front;
    case FlipRole:
        return p.flip;
    }
    return {};
}

QHash<int, QByteArray> LayoutModel::roleNames() const
{
    return {{EntryIdRole, "entryId"}, {NameRole, "name"},   {ImageRole, "image"},   {AspectRole, "aspect"},
            {KindRole, "kind"},       {XRole, "posX"},      {YRole, "posY"},        {ScaleRole, "size"},
            {FrontRole, "front"},     {FlipRole, "flipped"}};
}

void LayoutModel::reload()
{
    QString error;
    const QList<FishEntry> all = FishLibrary::load(&error);
    if (!error.isEmpty())
        setError(error);

    // The wallpaper remembers its theme next to the friendships
    QFile state(FishLibrary::statePath());
    QString theme;
    if (state.open(QIODevice::ReadOnly))
        theme = QJsonDocument::fromJson(state.readAll()).object().value(QStringLiteral("theme")).toString();
    // A theme just chosen here may not have reached the wallpaper's state yet
    const FishLibrary::ThemeRequest request = FishLibrary::themeRequest();
    if (!request.theme.isEmpty() && request.at > QFileInfo(FishLibrary::statePath()).lastModified().toMSecsSinceEpoch())
        theme = request.theme;
    m_theme = Themes::byId(theme).id;
    m_themeColors = Themes::colors(m_theme);
    Q_EMIT themeChanged();

    beginResetModel();
    m_entries.clear();
    for (const FishEntry &e : all) {
        if (e.kind == FishEntry::Kind::Fish || e.kind == FishEntry::Kind::Crawler)
            continue;
        m_entries << e;
        const QSize size = QImageReader(e.path()).size();
        m_aspect.insert(e.id, size.isValid() && size.width() > 0 ? double(size.height()) / size.width() : 1.0);
    }
    rebuildItems();
    endResetModel();
    Q_EMIT countChanged();
}

void LayoutModel::rebuildItems()
{
    m_items.clear();
    for (int e = 0; e < m_entries.size(); ++e) {
        for (int p = 0; p < m_entries[e].placements.size(); ++p)
            m_items.append({e, p});
    }
}

Placement *LayoutModel::placementAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return nullptr;
    const Item &item = m_items[row];
    return &m_entries[item.entry].placements[item.placement];
}

bool LayoutModel::save(int entry)
{
    QString error;
    if (!FishLibrary::update(m_entries[entry], &error)) {
        setError(error);
        return false;
    }
    return true;
}

void LayoutModel::move(int row, double x, double y)
{
    Placement *p = placementAt(row);
    if (!p)
        return;
    p->x = std::clamp(x, -0.1, 1.1);
    p->y = std::clamp(y, 0.1, 1.05);
    save(m_items[row].entry);
    Q_EMIT dataChanged(index(row), index(row), {XRole, YRole});
}

void LayoutModel::setScale(int row, double scale)
{
    Placement *p = placementAt(row);
    if (!p)
        return;
    p->scale = std::clamp(scale, 0.15, 3.0);
    save(m_items[row].entry);
    Q_EMIT dataChanged(index(row), index(row), {ScaleRole});
}

void LayoutModel::setFront(int row, bool front)
{
    Placement *p = placementAt(row);
    if (!p || p->front == front)
        return;
    p->front = front;
    save(m_items[row].entry);
    Q_EMIT dataChanged(index(row), index(row), {FrontRole});
}

void LayoutModel::flip(int row)
{
    Placement *p = placementAt(row);
    if (!p)
        return;
    p->flip = !p->flip;
    save(m_items[row].entry);
    Q_EMIT dataChanged(index(row), index(row), {FlipRole});
}

int LayoutModel::duplicate(int row)
{
    Placement *p = placementAt(row);
    if (!p)
        return -1;
    Placement copy = *p;
    copy.x = std::clamp(copy.x + 0.06, 0.0, 1.0);
    const int entry = m_items[row].entry;
    beginResetModel();
    m_entries[entry].placements << copy;
    rebuildItems();
    endResetModel();
    Q_EMIT countChanged();
    save(entry);
    // The new placement is the last one of its entry
    for (int r = 0; r < m_items.size(); ++r) {
        if (m_items[r].entry == entry && m_items[r].placement == m_entries[entry].placements.size() - 1)
            return r;
    }
    return -1;
}

void LayoutModel::remove(int row)
{
    if (!placementAt(row))
        return;
    const Item item = m_items[row];
    beginResetModel();
    m_entries[item.entry].placements.removeAt(item.placement);
    rebuildItems();
    endResetModel();
    Q_EMIT countChanged();
    save(item.entry);
}

int LayoutModel::add(const QString &entryId)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const FishEntry &e) { return e.id == entryId; });
    if (it == m_entries.end())
        return -1;
    const int entry = int(it - m_entries.begin());
    Placement p;
    p.x = 0.2 + 0.6 * QRandomGenerator::global()->generateDouble();
    beginResetModel();
    m_entries[entry].placements << p;
    rebuildItems();
    endResetModel();
    Q_EMIT countChanged();
    save(entry);
    for (int r = 0; r < m_items.size(); ++r) {
        if (m_items[r].entry == entry && m_items[r].placement == m_entries[entry].placements.size() - 1)
            return r;
    }
    return -1;
}

void LayoutModel::setTheme(const QString &theme)
{
    if (theme == m_theme)
        return;
    QString error;
    if (!FishLibrary::requestTheme(theme, &error)) {
        setError(error);
        return;
    }
    m_theme = theme;
    m_themeColors = Themes::colors(theme);
    Q_EMIT themeChanged();
}

QVariantList LayoutModel::themes() const
{
    QVariantList list;
    for (const Theme &t : Themes::all())
        list << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("label"), t.label}};
    return list;
}

void LayoutModel::setError(const QString &error)
{
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}
