#include "spritenode.h"

#include <QImage>
#include <QQuickWindow>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>

#include <cstring>

namespace {

const QSGGeometry::AttributeSet &vertexAttributes()
{
    static const QSGGeometry::Attribute attrs[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 1, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
        QSGGeometry::Attribute::createWithAttributeType(3, 1, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {4, sizeof(FishMesh::Vertex), attrs};
    return set;
}

class SpriteShader : public QSGMaterialShader
{
public:
    SpriteShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/desktocean/shaders/fishbatch.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/desktocean/shaders/fishbatch.frag.qsb"));
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override;
    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture, QSGMaterial *newMaterial,
                            QSGMaterial *) override;
};

} // namespace

class SpriteMaterial : public QSGMaterial
{
public:
    SpriteMaterial() { setFlag(Blending); }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType t;
        return &t;
    }

    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override { return new SpriteShader; }

    int compare(const QSGMaterial *other) const override
    {
        const auto *o = static_cast<const SpriteMaterial *>(other);
        if (texture != o->texture)
            return texture < o->texture ? -1 : 1;
        if (fogColor != o->fogColor)
            return fogColor.rgba() < o->fogColor.rgba() ? -1 : 1;
        return 0;
    }

    QSGTexture *texture = nullptr;
    QColor fogColor = QColor(0x0b, 0x30, 0x48);
};

// Uniform block layout (std140): mat4 @0, float opacity @64, vec4 fogColor @80
bool SpriteShader::updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial)
{
    QByteArray *buf = state.uniformData();
    Q_ASSERT(buf->size() >= 96);
    bool changed = false;
    if (state.isMatrixDirty()) {
        const QMatrix4x4 m = state.combinedMatrix();
        std::memcpy(buf->data(), m.constData(), 64);
        changed = true;
    }
    if (state.isOpacityDirty()) {
        const float opacity = state.opacity();
        std::memcpy(buf->data() + 64, &opacity, 4);
        changed = true;
    }
    const auto *mat = static_cast<SpriteMaterial *>(newMaterial);
    const auto *old = static_cast<SpriteMaterial *>(oldMaterial);
    if (!old || old->fogColor != mat->fogColor) {
        const float c[4] = {float(mat->fogColor.redF()), float(mat->fogColor.greenF()), float(mat->fogColor.blueF()),
                            float(mat->fogColor.alphaF())};
        std::memcpy(buf->data() + 80, c, 16);
        changed = true;
    }
    return changed;
}

void SpriteShader::updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                                      QSGMaterial *newMaterial, QSGMaterial *)
{
    if (binding != 1)
        return;
    QSGTexture *t = static_cast<SpriteMaterial *>(newMaterial)->texture;
    if (t)
        t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    *texture = t;
}

SpriteNode::SpriteNode()
    : m_geometry(vertexAttributes(), 0, 0, QSGGeometry::UnsignedIntType)
    , m_material(std::make_unique<SpriteMaterial>())
{
    m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
    setGeometry(&m_geometry);
    setMaterial(m_material.get());
}

SpriteNode::~SpriteNode() = default;

void SpriteNode::setAtlas(QQuickWindow *window, const QImage &atlas)
{
    QSGTexture *t = window->createTextureFromImage(
        atlas, QQuickWindow::CreateTextureOptions(QQuickWindow::TextureHasAlphaChannel | QQuickWindow::TextureHasMipmaps));
    t->setFiltering(QSGTexture::Linear);
    t->setMipmapFiltering(QSGTexture::Linear);
    m_material->texture = t;
    m_texture.reset(t);
    markDirty(QSGNode::DirtyMaterial);
}

bool SpriteNode::hasAtlas() const
{
    return m_texture != nullptr;
}

void SpriteNode::setFogColor(const QColor &color)
{
    if (m_material->fogColor == color)
        return;
    m_material->fogColor = color;
    markDirty(QSGNode::DirtyMaterial);
}

FishMesh::Vertex *SpriteNode::reserve(int vertexCount, int indexCount, quint32 **indices)
{
    if (m_geometry.vertexCount() != vertexCount || m_geometry.indexCount() != indexCount)
        m_geometry.allocate(vertexCount, indexCount);
    *indices = m_geometry.indexDataAsUInt();
    return static_cast<FishMesh::Vertex *>(m_geometry.vertexData());
}

void SpriteNode::commit()
{
    markDirty(QSGNode::DirtyGeometry);
}
