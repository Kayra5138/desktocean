#pragma once

#include <QObject>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

// Last known global cursor position.
//
// Inside plasmashell the folder view swallows pointer events before they reach
// the wallpaper, but the desktop window still receives them, so Qt keeps
// QCursor::pos() current. QML polls this once per simulation tick.
class Cursor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QPointF position() const;
};
