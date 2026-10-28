#pragma once

#include "fishmesh.h"

#include <QColor>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <memory>

class QImage;
class QQuickWindow;
class QSGTexture;
class SpriteMaterial;

// A batch of textured, depth-fogged triangles drawn in one call with the
// fishbatch shaders. Shared by FishTank and the editor's swim preview.
class SpriteNode : public QSGGeometryNode
{
public:
    SpriteNode();
    ~SpriteNode() override;

    // Uploads `atlas` as the node's texture (mipmapped); call from updatePaintNode
    void setAtlas(QQuickWindow *window, const QImage &atlas);
    bool hasAtlas() const;
    void setFogColor(const QColor &color);

    // Resizes the geometry if needed and returns its buffers to fill
    FishMesh::Vertex *reserve(int vertexCount, int indexCount, quint32 **indices);
    void commit();

private:
    QSGGeometry m_geometry;
    std::unique_ptr<SpriteMaterial> m_material;
    std::unique_ptr<QSGTexture> m_texture;
};
