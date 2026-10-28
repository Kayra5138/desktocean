#include "themes.h"

#include <algorithm>

namespace Themes {

const std::vector<Theme> &all()
{
    static const std::vector<Theme> list = {
        {QStringLiteral("tropical"), QStringLiteral("Tropical"), QColor(0x21, 0x85, 0xa8), QColor(0x05, 0x20, 0x3d),
         QColor(0xcc, 0xb3, 0x80), QColor(0x73, 0x61, 0x45)},
        {QStringLiteral("lagoon"), QStringLiteral("Lagoon"), QColor(0x33, 0xb8, 0xb0), QColor(0x0a, 0x4a, 0x5a),
         QColor(0xf0, 0xe6, 0xcf), QColor(0xa8, 0x9c, 0x80)},
        {QStringLiteral("deepsea"), QStringLiteral("Deep sea"), QColor(0x17, 0x4a, 0x7a), QColor(0x02, 0x0a, 0x1f),
         QColor(0x6f, 0x6a, 0x62), QColor(0x2e, 0x2b, 0x27)},
        {QStringLiteral("kelp"), QStringLiteral("Kelp forest"), QColor(0x3f, 0x8a, 0x6a), QColor(0x0b, 0x2a, 0x22),
         QColor(0xa8, 0x9a, 0x6a), QColor(0x4f, 0x46, 0x30)},
        {QStringLiteral("volcanic"), QStringLiteral("Volcanic"), QColor(0x2a, 0x7a, 0xb0), QColor(0x06, 0x1a, 0x33),
         QColor(0x4a, 0x4a, 0x50), QColor(0x18, 0x18, 0x1c)},
    };
    return list;
}

const Theme &byId(const QString &id)
{
    const auto &list = all();
    const auto it = std::find_if(list.begin(), list.end(), [&](const Theme &t) { return t.id == id; });
    return it != list.end() ? *it : list.front();
}

QVariantMap colors(const QString &id)
{
    const Theme &t = byId(id);
    return {{QStringLiteral("shallow"), t.shallow},
            {QStringLiteral("deep"), t.deep},
            {QStringLiteral("sandLight"), t.sandLight},
            {QStringLiteral("sandDark"), t.sandDark}};
}

} // namespace Themes
