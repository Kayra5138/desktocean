#include "drawingcanvas.h"
#include "fishlibrary.h"

#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQmlFile>
#include <QRandomGenerator>

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
constexpr int kWiggleNeutral = 128;

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
    case 3: { // seaweed: three wavy blades growing from the bottom
        const qreal bases[] = {380, 450, 520};
        const qreal tops[] = {90, 40, 120};
        for (int i = 0; i < 3; ++i) {
            const qreal x = bases[i], top = tops[i], sway = (i % 2 ? 40 : -40);
            path.moveTo(x - 22, 530);
            path.cubicTo(x - 40 + sway, 400, x - 10 - sway, 230, x + sway * 0.3, top);
            path.cubicTo(x + 18 - sway, 230, x + 30 + sway, 400, x + 22, 530);
            path.closeSubpath();
        }
        break;
    }
    case 4: { // kelp: a stem with leaves on alternating sides
        path.moveTo(440, 532);
        path.cubicTo(420, 380, 480, 250, 450, 30);
        path.cubicTo(470, 250, 445, 380, 462, 532);
        path.closeSubpath();
        for (int i = 0; i < 5; ++i) {
            const qreal y = 460 - i * 90;
            const qreal side = i % 2 ? 1 : -1;
            path.moveTo(452, y);
            path.cubicTo(452 + side * 60, y - 60, 452 + side * 150, y - 40, 452 + side * 170, y - 70);
            path.cubicTo(452 + side * 130, y - 5, 452 + side * 60, y + 10, 452, y);
            path.closeSubpath();
        }
        break;
    }
    case 5: // rock
        path.moveTo(160, 530);
        path.cubicTo(120, 400, 260, 250, 420, 240);
        path.cubicTo(560, 220, 720, 280, 760, 380);
        path.cubicTo(790, 450, 780, 510, 760, 530);
        path.closeSubpath();
        path.moveTo(330, 330);
        path.quadTo(420, 290, 520, 300);
        break;
    case 6: // scallop shell with ribs
        path.moveTo(450, 520);
        path.cubicTo(250, 470, 200, 220, 330, 120);
        path.cubicTo(400, 70, 500, 70, 570, 120);
        path.cubicTo(700, 220, 650, 470, 450, 520);
        path.closeSubpath();
        for (int i = -3; i <= 3; ++i) {
            path.moveTo(450, 510);
            path.lineTo(450 + i * 55, 110 + std::abs(i) * 25);
        }
        path.addRect(400, 500, 100, 30);
        break;
    case 7: { // castle: two towers, a wall with a door
        auto battlements = [&](qreal x, qreal w, qreal y) {
            const int n = 3;
            const qreal step = w / (2 * n - 1);
            for (int k = 0; k < n; ++k)
                path.addRect(x + 2 * k * step, y - 30, step, 30);
        };
        path.addRect(250, 230, 120, 300);
        battlements(250, 120, 230);
        path.addRect(530, 230, 120, 300);
        battlements(530, 120, 230);
        path.addRect(370, 320, 160, 210);
        battlements(370, 160, 320);
        path.moveTo(415, 530);
        path.lineTo(415, 440);
        path.arcTo(415, 400, 70, 80, 180, -180);
        path.lineTo(485, 530);
        path.addEllipse(QPointF(310, 320), 18, 26);
        path.addEllipse(QPointF(590, 320), 18, 26);
        break;
    }
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
    , m_mask(kCanvasSize, QImage::Format_ARGB32)
{
    m_image.fill(Qt::transparent);
    m_mask.fill(qRgb(kWiggleNeutral, kWiggleNeutral, kWiggleNeutral));
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

void DrawingCanvas::setMotionMode(bool on)
{
    if (on == m_motionMode)
        return;
    commitSelection();
    m_motionMode = on;
    update();
    Q_EMIT motionModeChanged();
}

void DrawingCanvas::setWiggleBrush(Wiggle brush)
{
    if (brush == m_wiggleBrush)
        return;
    m_wiggleBrush = brush;
    Q_EMIT wiggleBrushChanged();
}

FishDrawing DrawingCanvas::drawing() const
{
    if (hasSelection()) {
        // Include the floating selection without committing it
        QImage composed = m_image;
        QPainter p(&composed);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(m_floatRect, m_floating);
        p.end();
        return {composed, m_hasWiggle ? m_mask : QImage()};
    }
    return {m_image, m_hasWiggle ? m_mask : QImage()};
}

void DrawingCanvas::resetWiggle()
{
    if (!m_hasWiggle)
        return;
    pushUndo();
    m_mask.fill(qRgb(kWiggleNeutral, kWiggleNeutral, kWiggleNeutral));
    contentModified();
}

void DrawingCanvas::paintTailWiggleForTest()
{
    pushUndo();
    QPainter p(&m_mask);
    p.fillRect(QRect(0, 0, kCanvasSize.width() / 3, kCanvasSize.height()), QColor(255, 255, 255));
    p.end();
    setMotionMode(true);
    contentModified();
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
    m_undo.append({m_image, m_mask});
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
    m_redo.append({m_image, m_mask});
    const Snapshot snap = m_undo.takeLast();
    m_image = snap.image;
    m_mask = snap.mask;
    Q_EMIT historyChanged();
    contentModified();
}

void DrawingCanvas::redo()
{
    if (m_redo.isEmpty())
        return;
    commitSelection();
    m_undo.append({m_image, m_mask});
    const Snapshot snap = m_redo.takeLast();
    m_image = snap.image;
    m_mask = snap.mask;
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
    m_mask.fill(qRgb(kWiggleNeutral, kWiggleNeutral, kWiggleNeutral));
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
    m_mask = m_mask.flipped(Qt::Horizontal);
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

void DrawingCanvas::replaceImage(const QImage &source, const QImage &wiggle, bool keepHistory)
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
    m_mask.fill(qRgb(kWiggleNeutral, kWiggleNeutral, kWiggleNeutral));
    const QSize target = source.size().scaled(kCanvasSize * 0.9, Qt::KeepAspectRatio);
    const QRect placed(QPoint((kCanvasSize.width() - target.width()) / 2, (kCanvasSize.height() - target.height()) / 2),
                       target);
    QPainter p(&m_image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(placed, source);
    p.end();
    if (!wiggle.isNull()) {
        QPainter mp(&m_mask);
        mp.setRenderHint(QPainter::SmoothPixmapTransform);
        mp.drawImage(placed, wiggle.convertToFormat(QImage::Format_ARGB32));
    }
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
    replaceImage(cropped, {}, true);
    return {};
}

QString DrawingCanvas::loadImage(const QUrl &url, const QUrl &wiggle)
{
    QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
    const QImage img = reader.read();
    if (img.isNull())
        return QStringLiteral("Could not open the fish: %1").arg(reader.errorString());
    const QImage mask = wiggle.isEmpty() ? QImage() : QImage(QQmlFile::urlToLocalFileOrQrc(wiggle));
    replaceImage(img.convertToFormat(QImage::Format_ARGB32), mask.size() == img.size() ? mask : QImage(), false);
    return {};
}

void DrawingCanvas::loadTemplate(int index)
{
    commitSelection();
    pushUndo();
    m_image.fill(Qt::transparent);
    m_mask.fill(qRgb(kWiggleNeutral, kWiggleNeutral, kWiggleNeutral));
    QPainter p(&m_image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x2a, 0x2a, 0x35), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::white);
    p.drawPath(templateOutline(index));
    p.end();
    contentModified();
}

namespace {
FishEntry entryFromMap(const QVariantMap &m)
{
    FishEntry e;
    e.kind = FishLibrary::kindFromName(m.value(QStringLiteral("kind"), QStringLiteral("fish")).toString());
    e.name = m.value(QStringLiteral("name")).toString();
    e.scale = m.value(QStringLiteral("scale"), 1.0).toDouble();
    e.count = m.value(QStringLiteral("count"), 1).toInt();
    e.style = m.value(QStringLiteral("swimStyle"), QStringLiteral("classic")).toString();
    e.sway = m.value(QStringLiteral("sway"), 1.0).toDouble();
    return e;
}
}

QString DrawingCanvas::saveToLibrary(const QVariantMap &meta)
{
    commitSelection();
    FishEntry entry = entryFromMap(meta);
    if (entry.kind != FishEntry::Kind::Fish) {
        // Somewhere on the sand, behind the fish; the layout page moves it
        Placement p;
        p.x = 0.15 + 0.7 * QRandomGenerator::global()->generateDouble();
        entry.placements << p;
        entry.count = 1;
    }
    QString error;
    if (!FishLibrary::add(drawing(), entry, &error))
        return error.isEmpty() ? QStringLiteral("Could not save") : error;
    return {};
}

QString DrawingCanvas::saveOver(const QString &id, const QVariantMap &meta)
{
    commitSelection();
    FishEntry entry = entryFromMap(meta);
    entry.id = id;
    QString error;
    if (!FishLibrary::replaceDrawing(entry, drawing(), &error))
        return error.isEmpty() ? QStringLiteral("Could not save") : error;
    return {};
}

void DrawingCanvas::contentModified()
{
    m_empty = !hasSelection() && FishLibrary::contentBox(m_image, 0).isNull();
    // Any pixel away from neutral means the user painted motion
    m_hasWiggle = false;
    for (int y = 0; y < m_mask.height() && !m_hasWiggle; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(m_mask.constScanLine(y));
        for (int x = 0; x < m_mask.width(); ++x) {
            if (qRed(row[x]) != kWiggleNeutral) {
                m_hasWiggle = true;
                break;
            }
        }
    }
    m_overlayDirty = true;
    update();
    Q_EMIT contentChanged();
}

void DrawingCanvas::paintWiggle(QPointF from, QPointF to)
{
    const int g = m_wiggleBrush == More ? 255 : m_wiggleBrush == Less ? 0 : kWiggleNeutral;
    QPainter p(&m_mask);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(g, g, g), m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (from == to)
        p.drawPoint(from);
    else
        p.drawLine(from, to);
    m_overlayDirty = true;
}

const QImage &DrawingCanvas::wiggleOverlay()
{
    if (!m_overlayDirty)
        return m_overlay;
    m_overlayDirty = false;
    // Red where it wiggles more, blue where it is stiffer; only over the drawing
    m_overlay = QImage(kCanvasSize, QImage::Format_ARGB32_Premultiplied);
    m_overlay.fill(Qt::transparent);
    for (int y = 0; y < kCanvasSize.height(); ++y) {
        const auto *img = reinterpret_cast<const QRgb *>(m_image.constScanLine(y));
        const auto *mask = reinterpret_cast<const QRgb *>(m_mask.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(m_overlay.scanLine(y));
        for (int x = 0; x < kCanvasSize.width(); ++x) {
            const int d = qRed(mask[x]) - kWiggleNeutral;
            if (d == 0 || qAlpha(img[x]) < 16)
                continue;
            const int a = std::min(200, std::abs(d) * 3 / 2);
            out[x] = d > 0 ? qPremultiply(qRgba(235, 60, 60, a)) : qPremultiply(qRgba(50, 120, 240, a));
        }
    }
    return m_overlay;
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

    if (m_motionMode) {
        pushUndo();
        m_drag = Drag::Stroke;
        paintWiggle(p, p);
        update();
        return;
    }

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
        if (m_motionMode)
            paintWiggle(m_last, p);
        else
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
    if (m_motionMode)
        painter->drawImage(m_view, wiggleOverlay());

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
    const bool brushLike = m_motionMode || m_tool == Brush || m_tool == Eraser;
    if (brushLike && m_hover.x() >= 0 && m_view.contains(m_hover)) {
        const qreal r = m_brushSize * 0.5 * scale;
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 200), 2));
        painter->drawEllipse(m_hover, r, r);
        painter->setPen(QPen(QColor(0, 0, 0, 160), 1));
        painter->drawEllipse(m_hover, r, r);
    }
}
