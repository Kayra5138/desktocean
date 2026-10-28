#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// The drawing surface: a fixed-size transparent image shown scaled to fit.
// Tools: brush, eraser, bucket fill, colour picker, line/rectangle/ellipse,
// and a selection that can be moved, resized, flipped or deleted. Fish are
// drawn facing right; saveToLibrary() crops and stores the result.
class DrawingCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(int brushSize READ brushSize WRITE setBrushSize NOTIFY brushSizeChanged)
    // Rectangles and ellipses are filled instead of outlined
    Q_PROPERTY(bool shapeFilled READ shapeFilled WRITE setShapeFilled NOTIFY shapeFilledChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY contentChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    // Where the canvas is drawn inside the item, for overlays
    Q_PROPERTY(QRectF viewRect READ viewRect NOTIFY viewRectChanged)

public:
    enum Tool { Brush, Eraser, Fill, Picker, Line, Rectangle, Ellipse, Select };
    Q_ENUM(Tool)

    explicit DrawingCanvas(QQuickItem *parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    int brushSize() const { return m_brushSize; }
    void setBrushSize(int size);
    bool shapeFilled() const { return m_shapeFilled; }
    void setShapeFilled(bool filled);
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool empty() const { return m_empty; }
    bool hasSelection() const { return !m_floating.isNull(); }
    QRectF viewRect() const { return m_view; }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();
    // Flips the selection if there is one, otherwise the whole drawing
    Q_INVOKABLE void flipHorizontal();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deleteSelection();
    // Drops the selection back into the drawing where it is
    Q_INVOKABLE void commitSelection();
    // True when the image's border is opaque, i.e. it probably has a background to remove
    Q_INVOKABLE bool hasOpaqueBorder(const QUrl &url) const;
    // Replaces the drawing with the image, scaled to fit; returns an error message or ""
    Q_INVOKABLE QString importImage(const QUrl &url, bool removeBackground);
    // Starts over with the image (e.g. a fish from the library) and empty history
    Q_INVOKABLE QString loadImage(const QUrl &url);
    // Outline templates to colour in: 0 = classic, 1 = long, 2 = round
    Q_INVOKABLE void loadTemplate(int index);
    // Bucket fill at a point in canvas pixels (900 x 540)
    Q_INVOKABLE void fillAt(qreal x, qreal y);
    // Crops and adds the drawing to the fish library; returns an error message or ""
    Q_INVOKABLE QString saveToLibrary(const QString &name, double scale, int count);
    // Replaces the drawing of library fish `id`; returns an error message or ""
    Q_INVOKABLE QString saveOver(const QString &id, const QString &name, double scale, int count);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void toolChanged();
    void colorChanged();
    void brushSizeChanged();
    void shapeFilledChanged();
    void historyChanged();
    void contentChanged();
    void selectionChanged();
    void viewRectChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    enum class Drag { None, Stroke, Shape, Rubber, Move, Resize };

    QPointF toImage(QPointF itemPos) const;
    QRectF toView(const QRectF &imageRect) const;
    bool onResizeHandle(QPointF itemPos) const;
    void pushUndo();
    void drawSegment(QPointF from, QPointF to);
    void drawShape(QPainter &p, QPointF from, QPointF to) const;
    void floodFill(QPoint seed);
    void liftSelection(const QRect &rect);
    void dropFloating();
    void contentModified();
    void updateViewRect();
    void replaceImage(const QImage &source, bool keepHistory);

    QImage m_image;
    QList<QImage> m_undo;
    QList<QImage> m_redo;

    Tool m_tool = Brush;
    QColor m_color = QColor(0xf2, 0x8c, 0x28);
    int m_brushSize = 12;
    bool m_shapeFilled = false;
    bool m_empty = true;

    // Pointer interaction, all in image coordinates
    Drag m_drag = Drag::None;
    QPointF m_start;
    QPointF m_last;
    QRectF m_rubber;
    QRectF m_dragOrigin;
    QPointF m_hover{-1, -1};   // item coordinates

    // Lifted selection floating above the drawing until committed
    QImage m_floating;
    QRectF m_floatRect;

    QRectF m_view;
};
