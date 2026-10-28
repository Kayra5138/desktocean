#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "fishmesh.h"

class DrawingCanvas;

// The fish on the canvas swimming back and forth with the chosen style and
// wiggle mask, rendered exactly like the wallpaper does.
class SwimPreview : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(DrawingCanvas *canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    Q_PROPERTY(QString swimStyle READ swimStyle WRITE setSwimStyle NOTIFY swimStyleChanged)
    // "fish" swims; "plant" stands and sways by `sway`; "decor" just stands
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(qreal sway READ sway WRITE setSway NOTIFY swayChanged)
    // [{id, label}] of every swim style, for a ComboBox
    Q_PROPERTY(QVariantList styles READ styles CONSTANT)

public:
    explicit SwimPreview(QQuickItem *parent = nullptr);

    DrawingCanvas *canvas() const { return m_canvas; }
    void setCanvas(DrawingCanvas *canvas);
    QString swimStyle() const { return m_style; }
    void setSwimStyle(const QString &style);
    QVariantList styles() const;
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    qreal sway() const { return m_sway; }
    void setSway(qreal sway);

Q_SIGNALS:
    void canvasChanged();
    void swimStyleChanged();
    void kindChanged();
    void swayChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;

private:
    void refreshSprite();
    void tick();
    void updateTimer();

    QPointer<DrawingCanvas> m_canvas;
    QString m_style = QStringLiteral("classic");
    QString m_kind = QStringLiteral("fish");
    qreal m_sway = 1.0;
    QTimer m_refresh;
    QTimer m_timer;
    QElapsedTimer m_clock;

    QImage m_atlas;
    QRectF m_uv;
    float m_aspect = 0.6f;
    std::vector<float> m_weights;
    bool m_atlasDirty = false;

    float m_time = 0;
    float m_phase = 0;
    float m_x = 0;
    float m_speed = 0;
    float m_facing = 1;
};
