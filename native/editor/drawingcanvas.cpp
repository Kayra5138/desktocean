#include "drawingcanvas.h"
#include "fishlibrary.h"

#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQmlFile>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace {

constexpr QSize kCanvasSize(900, 540);
constexpr int kMaxHistory = 30;
// Per-channel difference still treated as "the same colour" by the bucket
constexpr int kFillTolerance = 48;
constexpr int kBackgroundTolerance = 36;

int colorDistance(QRgb a, QRgb b)
{
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
                     std::abs(qAlpha(a) - qAlpha(b))});
}

// Scanline flood fill over `img` (Format_ARGB32) starting at `seeds`, calling
// `paint` for every pixel similar to the first seed's colour.
template <typename Paint>
void flood(QImage &img, const std::vector<QPoint> &seeds, QRgb target, int tolerance, Paint paint)
{
    const int w = img.width(), h = img.height();
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<QPoint> stack = seeds;
    auto matches = [&](int x, int y) {
        return !seen[size_t(y) * w + x] && colorDistance(reinterpret_cast<const QRgb *>(img.constScanLine(y))[x], target) <= tolerance;
    };
    while (!stack.empty()) {
        QPoint p = stack.back();
        stack.pop_back();
        int x = p.x();
        const int y = p.y();
        if (!matches(x, y))
            continue;
        while (x > 0 && matches(x - 1, y))
            --x;
        bool up = false, down = false;
        auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (; x < w && matches(x, y); ++x) {
            seen[size_t(y) * w + x] = 1;
            paint(row[x]);
            if (y > 0) {
                const bool m = matches(x, y - 1);
                if (m && !up)
                    stack.emplace_back(x, y - 1);
                up = m;
            }
            if (y < h - 1) {
                const bool m = matches(x, y + 1);
                if (m && !down)
                    stack.emplace_back(x, y + 1);
                down = m;
            }
        }
    }
}

QPainterPath templateOutline(int index)
{
    // Drawn in a 900 x 540 frame, head on the right
    QPainterPath path;
    switch (index) {
    case 1: // long and slim
        path.moveTo(120, 270);
        path.cubicTo(260, 170, 640, 160, 800, 265);
        path.cubicTo(640, 370, 260, 360, 120, 270);
        path.moveTo(130, 270);
        path.lineTo(40, 190);
        path.quadTo(70, 270, 40, 350);
        path.closeSubpath();
        path.addEllipse(QPointF(730, 248), 14, 14);
        break;
    case 2: // round
        path.addEllipse(QPointF(500, 270), 230, 200);
        path.moveTo(280, 270);
        path.lineTo(140, 150);
        path.quadTo(190, 270, 140, 390);
        path.closeSubpath();
        path.addEllipse(QPointF(640, 220), 22, 22);
        break;
    default: // classic
        path.moveTo(220, 270);
        path.cubicTo(330, 110, 650, 100, 790, 262);
        path.cubicTo(650, 430, 330, 430, 220, 270);
        path.moveTo(235, 270);
        path.lineTo(80, 150);
        path.quadTo(120, 270, 80, 390);
        path.closeSubpath();
        path.moveTo(430, 150);
        path.quadTo(500, 60, 600, 140);
        path.addEllipse(QPointF(700, 235), 16, 16);
        break;
    }
    return path;
}

} // namespace

DrawingCanvas::DrawingCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_image(kCanvasSize, QImage::Format_ARGB32)
{
    m_image.fill(Qt::transparent);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
}

void DrawingCanvas::setTool(Tool tool)
{
    if (tool == m_tool)
        return;
    if (m_tool == Select)
        commitSelection();
    m_tool = tool;
    update();
    Q_EMIT toolChanged();
}

void DrawingCanvas::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    Q_EMIT colorChanged();
}

void DrawingCanvas::setBrushSize(int size)
{
    size = std::clamp(size, 1, 120);
    if (size == m_brushSize)
        return;
    m_brushSize = size;
    update();
    Q_EMIT brushSizeChanged();
}

void DrawingCanvas::setShapeFilled(bool filled)
{
    if (filled == m_shapeFilled)
        return;
    m_shapeFilled = filled;
    Q_EMIT shapeFilledChanged();
}

// --- History -----------------------------------------------------------------

void DrawingCanvas::pushUndo()
{
    m_undo.append(m_image);
    if (m_undo.size() > kMaxHistory)
        m_undo.removeFirst();
    m_redo.clear();
    Q_EMIT historyChanged();
}

void DrawingCanvas::undo()
{
    if (m_undo.isEmpty())
        return;
    // The snapshot on top predates lifting the selection, so just let it go
    dropFloating();
    m_redo.append(m_image);
    m_image = m_undo.takeLast();
    Q_EMIT historyChanged();
    contentModified();
}

void DrawingCanvas::redo()
{
    if (m_redo.isEmpty())
        return;
    commitSelection();
    m_undo.append(m_image);
    m_image = m_redo.takeLast();
    Q_EMIT historyChanged();
    contentModified();
}

// --- Whole-drawing operations ------------------------------------------------

void DrawingCanvas::clear()
{
    dropFloating();
    if (m_empty)
        return;
    pushUndo();
    m_image.fill(Qt::transparent);
    contentModified();
}

void DrawingCanvas::flipHorizontal()
{
    if (hasSelection()) {
        m_floating = m_floating.flipped(Qt::Horizontal);
        update();
        return;
    }
    pushUndo();
    m_image = m_image.flipped(Qt::Horizontal);
    contentModified();
}

bool DrawingCanvas::hasOpaqueBorder(const QUrl &url) const
{
    QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
    const QImage img = reader.read().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return false;
    const QRgb corners[] = {img.pixel(0, 0), img.pixel(img.width() - 1, 0), img.pixel(0, img.height() - 1),
                            img.pixel(img.width() - 1, img.height() - 1)};
    return std::all_of(std::begin(corners), std::end(corners), [](QRgb c) { return qAlpha(c) > 200; });
}

void DrawingCanvas::replaceImage(const QImage &source, bool keepHistory)
{
    commitSelection();
    if (keepHistory) {
        pushUndo();
    } else {
        m_undo.clear();
        m_redo.clear();
        Q_EMIT historyChanged();
    }
    m_image.fill(Qt::transparent);
    const QSize target = source.size().scaled(kCanvasSize * 0.9, Qt::KeepAspectRatio);
    QPainter p(&m_image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(QPoint((kCanvasSize.width() - target.width()) / 2, (kCanvasSize.height() - target.height()) / 2),
                      target),
                source);
    p.end();
    contentModified();
}

QString DrawingCanvas::importImage(const QUrl &url, bool removeBackground)
{
    QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull())
        return QStringLiteral("Could not open the image: %1").arg(reader.errorString());
    img = img.convertToFormat(QImage::Format_ARGB32);

    if (removeBackground) {
        // Everything connected to the border that looks like the top-left corner goes
        std::vector<QPoint> seeds;
        for (int x = 0; x < img.width(); ++x)
            seeds.insert(seeds.end(), {QPoint(x, 0), QPoint(x, img.height() - 1)});
        for (int y = 0; y < img.height(); ++y)
            seeds.insert(seeds.end(), {QPoint(0, y), QPoint(img.width() - 1, y)});
        flood(img, seeds, img.pixel(0, 0), kBackgroundTolerance, [](QRgb &px) { px = qRgba(0, 0, 0, 0); });
    }

    const QImage cropped = FishLibrary::cropToContent(img, 0);
    if (cropped.isNull())
        return QStringLiteral("The image is empty after removing the background");
    replaceImage(cropped, true);
    return {};
}

QString DrawingCanvas::loadImage(const QUrl &url)
{
    QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
    const QImage img = reader.read();
    if (img.isNull())
        return QStringLiteral("Could not open the fish: %1").arg(reader.errorString());
    replaceImage(img.convertToFormat(QImage::Format_ARGB32), false);
    return {};
}

void DrawingCanvas::loadTemplate(int index)
{
    commitSelection();
    pushUndo();
    m_image.fill(Qt::transparent);
    QPainter p(&m_image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x2a, 0x2a, 0x35), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::white);
    p.drawPath(templateOutline(index));
    p.end();
    contentModified();
}

QString DrawingCanvas::saveToLibrary(const QString &name, double scale, int count)
{
    commitSelection();
    QString error;
    if (!FishLibrary::add(m_image, name, scale, count, &error))
        return error.isEmpty() ? QStringLiteral("Could not save the fish") : error;
    return {};
}

QString DrawingCanvas::saveOver(const QString &id, const QString &name, double scale, int count)
{
    commitSelection();
    FishEntry entry;
    entry.id = id;
    entry.name = name;
    entry.scale = scale;
    entry.count = count;
    QString error;
    if (!FishLibrary::replaceSprite(entry, m_image, &error))
        return error.isEmpty() ? QStringLiteral("Could not save the fish") : error;
    return {};
}

void DrawingCanvas::contentModified()
{
    m_empty = !hasSelection() && FishLibrary::cropToContent(m_image, 0).isNull();
    update();
    Q_EMIT contentChanged();
}

// --- Selection ---------------------------------------------------------------

void DrawingCanvas::liftSelection(const QRect &rect)
{
    const QRect r = rect.intersected(m_image.rect());
    if (r.width() < 2 || r.height() < 2)
        return;
    pushUndo();
    m_floating = m_image.copy(r);
    m_floatRect = r;
    QPainter p(&m_image);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(r, Qt::transparent);
    p.end();
    Q_EMIT selectionChanged();
    contentModified();
}

void DrawingCanvas::selectAll()
{
    commitSelection();
    setTool(Select);
    // Lift just the visible part, not the empty canvas around it
    const QImage argb = m_image.convertToFormat(QImage::Format_ARGB32);
    QRect box;
    for (int y = 0; y < argb.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) > 8)
                box |= QRect(x, y, 1, 1);
        }
    }
    liftSelection(box);
}

void DrawingCanvas::commitSelection()
{
    if (!hasSelection())
        return;
    QPainter p(&m_image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(m_floatRect, m_floating);
    p.end();
    m_floating = {};
    Q_EMIT selectionChanged();
    contentModified();
}

void DrawingCanvas::deleteSelection()
{
    if (!hasSelection())
        return;
    // Its pixels were already cut out when it was lifted
    m_floating = {};
    Q_EMIT selectionChanged();
    contentModified();
}

void DrawingCanvas::dropFloating()
{
    if (!hasSelection())
        return;
    m_floating = {};
    Q_EMIT selectionChanged();
}

// --- Painting into the image -------------------------------------------------

void DrawingCanvas::fillAt(qreal x, qreal y)
{
    commitSelection();
    floodFill(QPointF(x, y).toPoint());
}

void DrawingCanvas::floodFill(QPoint seed)
{
    if (!m_image.rect().contains(seed))
        return;
    const QRgb target = m_image.pixel(seed);
    const QRgb fill = m_color.rgba();
    if (colorDistance(target, fill) == 0)
        return;
    pushUndo();
    flood(m_image, {seed}, target, kFillTolerance, [fill](QRgb &px) { px = fill; });
    contentModified();
}

void DrawingCanvas::drawSegment(QPointF from, QPointF to)
{
    QPainter p(&m_image);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_tool == Eraser)
        p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.setPen(QPen(m_color, m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (from == to)
        p.drawPoint(from);
    else
        p.drawLine(from, to);
}

void DrawingCanvas::drawShape(QPainter &p, QPointF from, QPointF to) const
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_color, m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(m_shapeFilled && m_tool != Line ? QBrush(m_color) : Qt::NoBrush);
    const QRectF r = QRectF(from, to).normalized();
    switch (m_tool) {
    case Line:
        p.drawLine(from, to);
        break;
    case Rectangle:
        p.drawRect(r);
        break;
    case Ellipse:
        p.drawEllipse(r);
        break;
    default:
        break;
    }
}

// --- Pointer -----------------------------------------------------------------

QPointF DrawingCanvas::toImage(QPointF itemPos) const
{
    if (m_view.isEmpty())
        return {};
    return (itemPos - m_view.topLeft()) * (kCanvasSize.width() / m_view.width());
}

QRectF DrawingCanvas::toView(const QRectF &imageRect) const
{
    const qreal s = m_view.width() / kCanvasSize.width();
    return QRectF(m_view.topLeft() + imageRect.topLeft() * s, imageRect.size() * s);
}

bool DrawingCanvas::onResizeHandle(QPointF itemPos) const
{
    if (!hasSelection())
        return false;
    const QPointF corner = toView(m_floatRect).bottomRight();
    return std::abs(itemPos.x() - corner.x()) < 12 && std::abs(itemPos.y() - corner.y()) < 12;
}

void DrawingCanvas::mousePressEvent(QMouseEvent *event)
{
    const QPointF p = toImage(event->position());
    m_start = m_last = p;

    switch (m_tool) {
    case Fill:
        floodFill(p.toPoint());
        return;
    case Picker: {
        const QPoint px = p.toPoint();
        if (m_image.rect().contains(px) && qAlpha(m_image.pixel(px)) > 0)
            setColor(QColor::fromRgba(m_image.pixel(px)).toRgb());
        return;
    }
    case Line:
    case Rectangle:
    case Ellipse:
        m_drag = Drag::Shape;
        update();
        return;
    case Select:
        m_dragOrigin = m_floatRect;
        if (onResizeHandle(event->position())) {
            m_drag = Drag::Resize;
        } else if (hasSelection() && m_floatRect.contains(p)) {
            m_drag = Drag::Move;
        } else {
            commitSelection();
            m_drag = Drag::Rubber;
            m_rubber = QRectF(p, p);
        }
        update();
        return;
    case Brush:
    case Eraser:
        pushUndo();
        m_drag = Drag::Stroke;
        drawSegment(p, p);
        update();
        return;
    }
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent *event)
{
    m_hover = event->position();
    const QPointF p = toImage(event->position());
    switch (m_drag) {
    case Drag::None:
        return;
    case Drag::Stroke:
        drawSegment(m_last, p);
        break;
    case Drag::Shape:
        break;
    case Drag::Rubber:
        m_rubber = QRectF(m_start, p).normalized();
        break;
    case Drag::Move:
        m_floatRect.moveTopLeft(m_dragOrigin.topLeft() + (p - m_start));
        break;
    case Drag::Resize: {
        // Keep the aspect ratio, grow from the top-left corner
        const qreal w = std::max<qreal>(8, m_dragOrigin.width() + (p.x() - m_start.x()));
        m_floatRect.setSize(QSizeF(w, w * m_dragOrigin.height() / m_dragOrigin.width()));
        break;
    }
    }
    m_last = p;
    update();
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent *)
{
    const Drag drag = std::exchange(m_drag, Drag::None);
    switch (drag) {
    case Drag::Stroke:
        contentModified();
        break;
    case Drag::Shape: {
        if (m_tool == Line ? m_start == m_last : (std::abs(m_last.x() - m_start.x()) < 1 || std::abs(m_last.y() - m_start.y()) < 1))
            break;
        pushUndo();
        QPainter p(&m_image);
        drawShape(p, m_start, m_last);
        p.end();
        contentModified();
        break;
    }
    case Drag::Rubber:
        liftSelection(m_rubber.toAlignedRect());
        m_rubber = {};
        break;
    default:
        update();
        break;
    }
}

void DrawingCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_hover = event->position();
    update();
}

void DrawingCanvas::hoverLeaveEvent(QHoverEvent *)
{
    m_hover = QPointF(-1, -1);
    update();
}

void DrawingCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    updateViewRect();
}

void DrawingCanvas::updateViewRect()
{
    const QSizeF fitted = QSizeF(kCanvasSize).scaled(size(), Qt::KeepAspectRatio);
    const QRectF view(QPointF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    if (view == m_view)
        return;
    m_view = view;
    update();
    Q_EMIT viewRectChanged();
}

// --- Rendering ---------------------------------------------------------------

void DrawingCanvas::paint(QPainter *painter)
{
    if (m_view.isEmpty())
        return;

    // Checkerboard shows what is transparent (and so will be water)
    static const QPixmap checker = [] {
        QPixmap pm(24, 24);
        pm.fill(QColor(0xf4, 0xf4, 0xf6));
        QPainter p(&pm);
        p.fillRect(0, 0, 12, 12, QColor(0xe2, 0xe2, 0xe8));
        p.fillRect(12, 12, 12, 12, QColor(0xe2, 0xe2, 0xe8));
        return pm;
    }();
    painter->fillRect(m_view, QBrush(checker));
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(m_view, m_image);

    const qreal scale = m_view.width() / kCanvasSize.width();
    painter->setRenderHint(QPainter::Antialiasing);

    // Shape being dragged out, previewed in view coordinates
    if (m_drag == Drag::Shape) {
        painter->save();
        painter->translate(m_view.topLeft());
        painter->scale(scale, scale);
        drawShape(*painter, m_start, m_last);
        painter->restore();
    }

    QPen dashed(QColor(0x30, 0x80, 0xe0), 1.5, Qt::DashLine);
    if (m_drag == Drag::Rubber) {
        painter->setBrush(QColor(0x30, 0x80, 0xe0, 30));
        painter->setPen(dashed);
        painter->drawRect(toView(m_rubber));
    }
    if (hasSelection()) {
        const QRectF r = toView(m_floatRect);
        painter->drawImage(r, m_floating);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(dashed);
        painter->drawRect(r);
        // Resize handle
        painter->setPen(QPen(Qt::white, 1.5));
        painter->setBrush(QColor(0x30, 0x80, 0xe0));
        painter->drawRect(QRectF(r.bottomRight() - QPointF(6, 6), QSizeF(12, 12)));
    }

    // Brush outline under the cursor
    const bool brushLike = m_tool == Brush || m_tool == Eraser;
    if (brushLike && m_hover.x() >= 0 && m_view.contains(m_hover)) {
        const qreal r = m_brushSize * 0.5 * scale;
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 200), 2));
        painter->drawEllipse(m_hover, r, r);
        painter->setPen(QPen(QColor(0, 0, 0, 160), 1));
        painter->drawEllipse(m_hover, r, r);
    }
}
