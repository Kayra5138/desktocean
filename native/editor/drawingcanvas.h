#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// The drawing surface: a fixed-size transparent image shown scaled to fit,
// with brush, eraser and bucket fill, undo/redo, image import and templates.
// Fish are drawn facing right; saveToLibrary() crops and stores the result.
class DrawingCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(int brushSize READ brushSize WRITE setBrushSize NOTIFY brushSizeChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY contentChanged)
    // Where the canvas is drawn inside the item, for overlays
    Q_PROPERTY(QRectF viewRect READ viewRect NOTIFY viewRectChanged)

public:
    enum Tool { Brush, Eraser, Fill };
    Q_ENUM(Tool)

    explicit DrawingCanvas(QQuickItem *parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    int brushSize() const { return m_brushSize; }
    void setBrushSize(int size);
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool empty() const { return m_empty; }
    QRectF viewRect() const { return m_view; }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void flipHorizontal();
    // True when the image's border is opaque, i.e. it probably has a background to remove
    Q_INVOKABLE bool hasOpaqueBorder(const QUrl &url) const;
    // Replaces the drawing with the image, scaled to fit; returns an error message or ""
    Q_INVOKABLE QString importImage(const QUrl &url, bool removeBackground);
    // Outline templates to colour in: 0 = classic, 1 = long, 2 = round
    Q_INVOKABLE void loadTemplate(int index);
    Q_INVOKABLE int templateCount() const { return 3; }
    // Bucket fill at a point in canvas pixels (900 x 540)
    Q_INVOKABLE void fillAt(qreal x, qreal y) { floodFill(QPointF(x, y).toPoint()); }
    // Crops and adds the drawing to the fish library; returns an error message or ""
    Q_INVOKABLE QString saveToLibrary(const QString &name, double scale, int count);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void toolChanged();
    void colorChanged();
    void brushSizeChanged();
    void historyChanged();
    void contentChanged();
    void viewRectChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QPointF toImage(QPointF itemPos) const;
    void pushUndo();
    void drawSegment(QPointF from, QPointF to);
    void floodFill(QPoint seed);
    void contentModified();
    void updateViewRect();

    QImage m_image;
    QList<QImage> m_undo;
    QList<QImage> m_redo;

    Tool m_tool = Brush;
    QColor m_color = QColor(0xf2, 0x8c, 0x28);
    int m_brushSize = 12;
    bool m_empty = true;

    bool m_stroking = false;
    QPointF m_last;
    QPointF m_hover{-1, -1};
    QRectF m_view;
};
