#include "drawingcanvas.h"
#include "fishlibrary.h"

#include <QCursor>
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
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
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
        return {composed, m_hasWiggle ? m_mask : QImage(), m_mouth, m_opening};
    }
    return {m_image, m_hasWiggle ? m_mask : QImage(), m_mouth, m_opening};
}

void DrawingCanvas::setShowMouth(bool show)
{
    if (show == m_showMouth)
        return;
    m_showMouth = show;
    update();
    Q_EMIT showMouthChanged();
}

void DrawingCanvas::clearOpening()
{
    if (m_opening.isNull())
        return;
    m_opening = QRectF();
    contentModified();
}

bool DrawingCanvas::exportImage(const QString &path) const
{
    return FishLibrary::cropToContent(m_image).save(path);
}

void DrawingCanvas::markOpening(qreal x, qreal y, qreal w, qreal h)
{
    m_opening = QRectF(x, y, w, h).intersected(QRectF(QPointF(), QSizeF(kCanvasSize)));
    contentModified();
}

void DrawingCanvas::resetMouth()
{
    if (m_mouth.x() < 0)
        return;
    pushUndo();
    m_mouth = QPointF(-1, -1);
    contentModified();
}

void DrawingCanvas::markMouth(qreal x, qreal y)
{
    pushUndo();
    m_mouth = QPointF(x, y);
    contentModified();
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

void DrawingCanvas::setSymmetry(Symmetry symmetry)
{
    if (symmetry == m_symmetry)
        return;
    m_symmetry = symmetry;
    update();
    Q_EMIT symmetryChanged();
}

bool DrawingCanvas::symmetryCentred() const
{
    return m_symmetry == LeftRight ? qFuzzyCompare(m_axis.x(), kCanvasSize.width() / 2.0)
                                   : qFuzzyCompare(m_axis.y(), kCanvasSize.height() / 2.0);
}

void DrawingCanvas::centreSymmetryAxis()
{
    m_axis = QPointF(kCanvasSize.width() / 2.0, kCanvasSize.height() / 2.0);
    update();
    Q_EMIT symmetryChanged();
}

QPointF DrawingCanvas::mirrored(QPointF p) const
{
    if (m_symmetry == TopBottom)
        return QPointF(p.x(), 2 * m_axis.y() - p.y());
    if (m_symmetry == LeftRight)
        return QPointF(2 * m_axis.x() - p.x(), p.y());
    return p;
}

QPointF DrawingCanvas::axisHandle() const
{
    const qreal s = m_view.width() / kCanvasSize.width();
    // At the start of the line, a little inside the canvas
    if (m_symmetry == LeftRight)
        return QPointF(m_view.left() + m_axis.x() * s, m_view.top() + 14);
    return QPointF(m_view.left() + 14, m_view.top() + m_axis.y() * s);
}

bool DrawingCanvas::onAxisHandle(QPointF itemPos) const
{
    if (m_symmetry == NoSymmetry)
        return false;
    const QPointF d = itemPos - axisHandle();
    return QPointF::dotProduct(d, d) < 12 * 12;
}

void DrawingCanvas::strokeForTest(const QVariantList &points)
{
    if (points.size() < 2)
        return;
    pushUndo();
    QPointF last(points[0].toDouble(), points[1].toDouble());
    drawSegment(last, last);
    for (int i = 2; i + 1 < points.size(); i += 2) {
        const QPointF p(points[i].toDouble(), points[i + 1].toDouble());
        drawSegment(last, p);
        last = p;
    }
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
    m_undo.append({m_image, m_mask, m_mouth});
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
    m_redo.append({m_image, m_mask, m_mouth});
    const Snapshot snap = m_undo.takeLast();
    m_image = snap.image;
    m_mask = snap.mask;
    m_mouth = snap.mouth;
    Q_EMIT historyChanged();
    contentModified();
}

void DrawingCanvas::redo()
{
    if (m_redo.isEmpty())
        return;
    commitSelection();
    m_undo.append({m_image, m_mask, m_mouth});
    const Snapshot snap = m_redo.takeLast();
    m_image = snap.image;
    m_mask = snap.mask;
    m_mouth = snap.mouth;
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
    m_mouth = QPointF(-1, -1);
    m_opening = QRectF();
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
    if (m_mouth.x() >= 0)
        m_mouth.setX(kCanvasSize.width() - m_mouth.x());
    if (!m_opening.isNull())
        m_opening.moveLeft(kCanvasSize.width() - m_opening.right());
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

QRect DrawingCanvas::replaceImage(const QImage &source, const QImage &wiggle, bool keepHistory)
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
    m_mouth = QPointF(-1, -1);
    m_opening = QRectF();
    contentModified();
    return placed;
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

QString DrawingCanvas::loadImage(const QUrl &url, const QUrl &wiggle, QPointF mouth, QRectF opening)
{
    QImageReader reader(QQmlFile::urlToLocalFileOrQrc(url));
    const QImage img = reader.read();
    if (img.isNull())
        return QStringLiteral("Could not open the fish: %1").arg(reader.errorString());
    const QImage mask = wiggle.isEmpty() ? QImage() : QImage(QQmlFile::urlToLocalFileOrQrc(wiggle));
    const QRect placed =
        replaceImage(img.convertToFormat(QImage::Format_ARGB32), mask.size() == img.size() ? mask : QImage(), false);
    // The saved mouth is a fraction of the sprite, which now sits at `placed`
    if (mouth.x() >= 0)
        m_mouth = QPointF(placed.x() + mouth.x() * placed.width(), placed.y() + mouth.y() * placed.height());
    m_opening = opening.isNull() ? QRectF()
                                 : QRectF(placed.x() + opening.x() * placed.width(), placed.y() + opening.y() * placed.height(),
                                          opening.width() * placed.width(), opening.height() * placed.height());
    // Freshly opened: nothing to lose yet
    m_dirty = false;
    resetView();
    Q_EMIT contentChanged();
    return {};
}

void DrawingCanvas::loadTemplate(int index)
{
    commitSelection();
    pushUndo();
    m_image.fill(Qt::transparent);
    m_mask.fill(qRgb(kWiggleNeutral, kWiggleNeutral, kWiggleNeutral));
    m_mouth = QPointF(-1, -1);
    m_opening = QRectF();
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
    e.speed = m.value(QStringLiteral("speed"), 1.0).toDouble();
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
    if (entry.kind == FishEntry::Kind::Plant || entry.kind == FishEntry::Kind::Decor) {
        // Somewhere on the sand, behind the fish; the layout page moves it
        Placement p;
        p.x = 0.15 + 0.7 * QRandomGenerator::global()->generateDouble();
        entry.placements << p;
        entry.count = 1;
    }
    QString error;
    if (!FishLibrary::add(drawing(), entry, &error))
        return error.isEmpty() ? QStringLiteral("Could not save") : error;
    m_dirty = false;
    Q_EMIT contentChanged();
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
    m_dirty = false;
    Q_EMIT contentChanged();
    return {};
}

void DrawingCanvas::setZoom(qreal zoom)
{
    zoom = std::clamp(zoom, 1.0, 8.0);
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    updateViewRect();
}

void DrawingCanvas::zoomBy(qreal factor, QPointF around)
{
    if (around.x() < 0)
        around = QPointF(width() / 2, height() / 2);
    // Keep the canvas point under `around` where it is
    const QPointF anchor = toImage(around);
    setZoom(m_zoom * factor);
    const qreal s = m_view.width() / kCanvasSize.width();
    const QPointF now = m_view.topLeft() + anchor * s;
    m_pan += around - now;
    updateViewRect();
}

void DrawingCanvas::resetView()
{
    m_zoom = 1;
    m_pan = {};
    updateViewRect();
}

void DrawingCanvas::setPressure(qreal pressure)
{
    pressure = std::clamp(pressure, 0.0, 1.0);
    if (qFuzzyCompare(pressure, m_pressure))
        return;
    m_pressure = pressure;
    Q_EMIT pressureChanged();
}

void DrawingCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        zoomBy(std::pow(1.15, event->angleDelta().y() / 120.0), event->position());
    } else if (m_zoom > 1) {
        // Plain wheel pans a zoomed canvas; Shift turns vertical scrolling sideways
        QPointF delta = QPointF(event->angleDelta()) / 2;
        if (event->modifiers() & Qt::ShiftModifier)
            delta = QPointF(delta.y(), delta.x());
        m_pan += delta;
        updateViewRect();
    } else {
        event->ignore();
        return;
    }
    event->accept();
}

void DrawingCanvas::contentModified()
{
    const QRect box = FishLibrary::contentBox(m_image, 0);
    m_empty = !hasSelection() && box.isNull();
    m_dirty = !m_empty;
    // Where the mouth would go if the user doesn't mark it
    if (box.isNull()) {
        m_guessedMouth = QPointF(-1, -1);
    } else {
        const QPointF g = FishLibrary::guessMouth(m_image.copy(box));
        m_guessedMouth = QPointF(box.x() + g.x() * box.width(), box.y() + g.y() * box.height());
    }
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
    drawOneSegment(p, from, to);
    if (m_symmetry != NoSymmetry)
        drawOneSegment(p, mirrored(from), mirrored(to));
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
    const QRgb fill = m_color.rgba();
    // With symmetry the mirrored spot is filled too, in the same undo step
    QList<QPoint> seeds{seed};
    if (m_symmetry != NoSymmetry)
        seeds << mirrored(QPointF(seed)).toPoint();
    bool pushed = false;
    for (const QPoint &at : std::as_const(seeds)) {
        if (!m_image.rect().contains(at))
            continue;
        const QRgb target = m_image.pixel(at);
        if (colorDistance(target, fill) == 0)
            continue;
        if (!std::exchange(pushed, true))
            pushUndo();
        flood(m_image, {at}, target, kFillTolerance, [fill](QRgb &px) { px = fill; });
    }
    if (pushed)
        contentModified();
}

void DrawingCanvas::drawSegment(QPointF from, QPointF to)
{
    QPainter p(&m_image);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_tool == Eraser)
        p.setCompositionMode(QPainter::CompositionMode_Clear);
    // A stylus draws thinner when pressed lightly; the mouse always reports 1
    const qreal width = std::max(1.0, m_brushSize * (0.2 + 0.8 * m_pressure));
    p.setPen(QPen(m_color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    drawOneSegment(p, from, to);
    if (m_symmetry != NoSymmetry)
        drawOneSegment(p, mirrored(from), mirrored(to));
}

void DrawingCanvas::drawOneSegment(QPainter &p, QPointF from, QPointF to) const
{
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
    drawOneShape(p, from, to);
    if (m_symmetry != NoSymmetry)
        drawOneShape(p, mirrored(from), mirrored(to));
}

void DrawingCanvas::drawOneShape(QPainter &p, QPointF from, QPointF to) const
{
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
    if (event->button() == Qt::MiddleButton) {
        m_drag = Drag::Pan;
        m_panStart = event->position() - m_pan;
        return;
    }
    const QPointF p = toImage(event->position());
    m_start = m_last = p;

    if (onAxisHandle(event->position()) && m_tool != Select) {
        m_drag = Drag::Axis;
        return;
    }

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
    case Opening:
        m_drag = Drag::Opening;
        m_opening = QRectF(p, p);
        update();
        return;
    case Mouth:
        if (m_image.rect().contains(p.toPoint())) {
            pushUndo();
            m_mouth = p;
            contentModified();
        }
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
    case Drag::Pan:
        m_pan = event->position() - m_panStart;
        updateViewRect();
        return;
    case Drag::Axis:
        m_axis = QPointF(std::clamp(p.x(), 0.0, qreal(kCanvasSize.width())),
                         std::clamp(p.y(), 0.0, qreal(kCanvasSize.height())));
        Q_EMIT symmetryChanged();
        break;
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
    case Drag::Opening:
        m_opening = QRectF(m_start, p).normalized().intersected(QRectF(QPointF(), QSizeF(kCanvasSize)));
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
    case Drag::Opening:
        // A click (or a sliver) clears it
        if (m_opening.width() < 20 || m_opening.height() < 20)
            m_opening = QRectF();
        contentModified();
        break;
    default:
        update();
        break;
    }
}

void DrawingCanvas::hoverMoveEvent(QHoverEvent *event)
{
    m_hover = event->position();
    if (onAxisHandle(m_hover))
        setCursor(m_symmetry == LeftRight ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    else
        unsetCursor();
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
    const QSizeF fitted = QSizeF(kCanvasSize).scaled(size(), Qt::KeepAspectRatio) * m_zoom;
    // Never let the canvas slide completely out of view
    const qreal maxX = std::max(0.0, (fitted.width() - width()) / 2 + width() * 0.4);
    const qreal maxY = std::max(0.0, (fitted.height() - height()) / 2 + height() * 0.4);
    m_pan = QPointF(std::clamp(m_pan.x(), -maxX, maxX), std::clamp(m_pan.y(), -maxY, maxY));
    const QPointF centre = QPointF(width() / 2, height() / 2) + m_pan;
    const QRectF view(centre - QPointF(fitted.width() / 2, fitted.height() / 2), fitted);
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

    // The mouth: solid when marked, dashed when it is only a guess
    const QPointF mouth = m_mouth.x() >= 0 ? m_mouth : m_guessedMouth;
    if (m_showMouth && !m_empty && mouth.x() >= 0 && !m_motionMode) {
        const QPointF at = m_view.topLeft() + mouth * scale;
        const qreal r = 9;
        painter->setBrush(m_mouth.x() >= 0 ? QColor(255, 80, 80, 170) : Qt::NoBrush);
        painter->setPen(QPen(Qt::white, 3));
        painter->drawEllipse(at, r, r);
        painter->setPen(QPen(QColor(220, 40, 40), 1.6, m_mouth.x() >= 0 ? Qt::SolidLine : Qt::DashLine));
        painter->drawEllipse(at, r, r);
        if (m_tool == Mouth) {
            painter->setPen(QColor(220, 40, 40));
            painter->drawText(at + QPointF(r + 4, 4), QStringLiteral("mouth"));
        }
    }

    // The cave opening: an arch, like the cut the tank makes in the drawing
    if (!m_opening.isNull() && !m_motionMode) {
        const QRectF r = toView(m_opening);
        const qreal round = r.height() * 0.55;
        QPainterPath arch;
        arch.moveTo(r.left(), r.bottom());
        arch.lineTo(r.left(), r.top() + round);
        arch.arcTo(QRectF(r.left(), r.top(), r.width(), round * 2), 180, -180);
        arch.lineTo(r.right(), r.bottom());
        arch.closeSubpath();
        painter->setBrush(QColor(0x30, 0x80, 0xe0, 40));
        painter->setPen(QPen(Qt::white, 3));
        painter->drawPath(arch);
        painter->setPen(QPen(QColor(0x30, 0x80, 0xe0), 1.6, Qt::DashLine));
        painter->drawPath(arch);
        painter->drawText(r.topLeft() + QPointF(4, -6), QStringLiteral("cave opening"));
    }

    // The mirror line, faint and dashed, with a round handle to drag it by
    if (m_symmetry != NoSymmetry) {
        const QPointF a = m_view.topLeft() + m_axis * scale;
        const QLineF line = m_symmetry == LeftRight ? QLineF(a.x(), m_view.top(), a.x(), m_view.bottom())
                                                    : QLineF(m_view.left(), a.y(), m_view.right(), a.y());
        painter->setPen(QPen(QColor(255, 255, 255, 140), 3));
        painter->drawLine(line);
        painter->setPen(QPen(QColor(0x30, 0x80, 0xe0, 150), 1.5, Qt::DashLine));
        painter->drawLine(line);
        const bool hot = m_drag == Drag::Axis || (m_hover.x() >= 0 && onAxisHandle(m_hover));
        painter->setPen(QPen(Qt::white, 1.5));
        painter->setBrush(QColor(0x30, 0x80, 0xe0, hot ? 230 : 150));
        painter->drawEllipse(axisHandle(), 7, 7);
    }

    // Brush outline under the cursor (and its mirror image)
    const bool brushLike = m_motionMode || m_tool == Brush || m_tool == Eraser;
    if (brushLike && m_drag != Drag::Axis && m_hover.x() >= 0 && m_view.contains(m_hover) && !onAxisHandle(m_hover)) {
        const qreal r = m_brushSize * 0.5 * scale;
        QList<QPointF> at{m_hover};
        if (m_symmetry != NoSymmetry)
            at << m_view.topLeft() + mirrored(toImage(m_hover)) * scale;
        painter->setBrush(Qt::NoBrush);
        for (const QPointF &c : std::as_const(at)) {
            painter->setPen(QPen(QColor(255, 255, 255, 200), 2));
            painter->drawEllipse(c, r, r);
            painter->setPen(QPen(QColor(0, 0, 0, 160), 1));
            painter->drawEllipse(c, r, r);
        }
    }
}
