#include "cursor.h"

#include <QCursor>

QPointF Cursor::position() const
{
    return QCursor::pos();
}
