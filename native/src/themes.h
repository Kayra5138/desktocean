#pragma once

#include <QColor>
#include <QString>
#include <QVariantMap>

#include <vector>

// Water and sand colours, shared by the wallpaper shader and the editor's
// tank layout so both show the same tank.
struct Theme {
    QString id;
    QString label;
    QColor shallow;     // water near the surface
    QColor deep;        // water near the bottom
    QColor sandLight;   // sand at its edge
    QColor sandDark;    // sand at the bottom of the screen
};

namespace Themes {

const std::vector<Theme> &all();
// Falls back to the first (tropical) theme
const Theme &byId(const QString &id);
// {shallow, deep, sandLight, sandDark} as colours, for QML
QVariantMap colors(const QString &id);

} // namespace Themes
