#include "LiquidMaterial.h"
#include "LiquidOpticsRenderer.h"
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QPainterPath>
#include <QVector2D>
#include <cmath>

namespace {
// Body diffusion (~7 logical pixels). Keep colour and large features intact.
// The optical rim gets a separate, nearly clear texture below.
using LiquidMaterial::diffuse;

QByteArray resource(const char *path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

// Arbitrary magnetic contours: signed distance in logical pixels, encoded in
// RG16 (1/64 px), plus independently antialiased coverage in B. Calculated only
// when shape/geometry changes, never on pointer frames. Original GLSL is kept
// untouched; the adapter uses this field in its distance/normal functions.
QImage shapeField(const QPainterPath &path, QSize pixels, qreal dpr)
{
    if (path.isEmpty()) return {};
    QImage mask(pixels, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    { QPainter p(&mask); p.setRenderHint(QPainter::Antialiasing); p.scale(dpr,dpr); p.fillPath(path,Qt::white); }
    const QPolygonF polygon = path.toFillPolygon();
    struct Segment { qreal x,y,dx,dy,inverseLength; };
    QVector<Segment> segments;
    segments.reserve(polygon.size());
    for (int i=0; i+1<polygon.size(); ++i) {
        const QPointF delta=polygon[i+1]-polygon[i];
        const qreal length=QPointF::dotProduct(delta,delta);
        segments.append({polygon[i].x(),polygon[i].y(),delta.x(),delta.y(),length>0 ? 1.0/length : 0});
    }
    QImage field(pixels, QImage::Format_RGB32);
    for (int y=0; y<pixels.height(); ++y) {
        auto *out = reinterpret_cast<QRgb *>(field.scanLine(y));
        const auto *coverage = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x=0; x<pixels.width(); ++x) {
            const qreal px=(x+0.5)/dpr, py=(y+0.5)/dpr;
            // Optical influence ends well before this distance.
            qreal distanceSquared = 128*128;
            for (const auto &edge : segments) {
                const qreal ox=px-edge.x, oy=py-edge.y;
                const qreal t=qBound(0.0,(ox*edge.dx+oy*edge.dy)*edge.inverseLength,1.0);
                const qreal nx=ox-t*edge.dx, ny=oy-t*edge.dy;
                distanceSquared=qMin(distanceSquared,nx*nx+ny*ny);
            }
            const int alpha=qAlpha(coverage[x]);
            const qreal distance=std::sqrt(distanceSquared)*(alpha>=128 ? -1 : 1);
            const int encoded=qBound(0,qRound(32768+distance*64),65535);
            out[x]=qRgb(encoded/256,encoded%256,alpha);
        }
    }
    return field;
}

} // namespace

// GUI-thread-only resources. No live rendering loop and no retained screen
// input textures: only context, linked program and reusable output FBO.
class LiquidOpticsRenderer::Backend {
public:
    static std::shared_ptr<Backend> acquire() {
        Q_ASSERT(QCoreApplication::instance() &&
                 QThread::currentThread() == QCoreApplication::instance()->thread());
        // Weak ownership releases every GL object when the last renderer dies,
        // while QApplication and its display connection still exist.
        static std::weak_ptr<Backend> shared;
        auto backend = shared.lock();
        if (!backend) {
            backend = std::make_shared<Backend>();
            shared = backend;
        }
        return backend;
    }
    ~Backend() {
        const bool current=context.isValid() && context.makeCurrent(&surface);
        framebuffer.reset();
        programStorage.reset();
        if (current) context.doneCurrent();
    }

    QImage render(const QImage &body, const QImage &clear, QSize logical, float radius, const QImage &shape, float refraction, float shade, float highlight, float chroma, float liquidStrength, int control) {
        if (!attempted) {
            QElapsedTimer elapsed; elapsed.start();
            attempted=true; ready=initialize();
            if (qEnvironmentVariableIsSet("UKUI_FENCES_STARTUP_TRACE"))
                qInfo() << "[FencesStartup] widget-gl-initialized" << elapsed.elapsed() << "ms" << ready;
        }
        if (!ready || !context.makeCurrent(&surface)) return {};
        const QImage result=draw(body,clear,logical,radius,shape,refraction,shade,highlight,chroma,liquidStrength,control);
        context.doneCurrent(); // textures were released by draw() before this
        return result;
    }

private:
    bool initialize() {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(2,1);
        context.setFormat(format);
        if (!context.create()) return false;
        surface.setFormat(context.format());
        surface.create();
        if (!surface.isValid() || !context.makeCurrent(&surface)) return false;
        context.functions()->initializeOpenGLFunctions();
        programStorage=std::make_unique<QOpenGLShaderProgram>();
        auto &program=*programStorage;
    const QByteArray vertex =
        "#version 120\nattribute vec2 aPosition; varying vec2 uv;\n"
        "void main(){uv=(aPosition+1.0)*0.5; gl_Position=vec4(aPosition,0.0,1.0);}";
    QByteArray upstream=resource(":/liquid-optics/glass.glsl");
    const QByteArray snells=resource(":/liquid-optics/snells-glass.glsl");
    if (upstream.isEmpty() || snells.isEmpty()) {
        context.doneCurrent();
        return false;
    }
    upstream.replace("#include \"snells-glass.glsl\"", snells);
    upstream.replace("float roundedRectangleDist(", "float roundedBoxDist(");
    upstream.replace("vec2 gradSdRoundedBox(", "vec2 boxGradient(");
    const QByteArray fragment = QByteArray(
        "#version 120\n#define texture texture2D\n"
        "uniform sampler2D texUnit; uniform sampler2D bodyUnit; varying vec2 uv;\n"
        "uniform vec2 halfpixel; uniform float viewportScale;\n"
        "uniform vec2 panelSize; uniform float panelRadius; uniform int controlMode;\n"
        "uniform float shadeStrength; uniform float highlightStrength; uniform float chromaStrength; uniform float liquidStrength;\n"
        "uniform sampler2D shapeUnit; uniform int shaped;\n"
        "float roundedRectangleDist(vec2 p,vec2 b,vec4 r);\n"
        "vec2 gradSdRoundedBox(vec2 p,vec2 b,float r);\n") + upstream + R"GLSL(
float contourDistance(vec2 position) {
    vec2 encoded=texture2D(shapeUnit,clamp(position/panelSize+0.5,0.0,1.0)).rg;
    return (dot(encoded,vec2(65280.0,255.0))-32768.0)/64.0;
}
float roundedRectangleDist(vec2 p,vec2 b,vec4 r) {
    return shaped>0 ? contourDistance(p) : roundedBoxDist(p,b,r);
}
vec2 gradSdRoundedBox(vec2 p,vec2 b,float r) {
    if(shaped==0) return boxGradient(p,b,r);
    return vec2(contourDistance(p+vec2(1.0,0.0))-contourDistance(p-vec2(1.0,0.0)),
                contourDistance(p+vec2(0.0,1.0))-contourDistance(p-vec2(0.0,1.0)))*0.5;
}
void main() {
    vec2 halfSize=panelSize*0.5;
    vec2 position=uv*panelSize-halfSize;
    vec4 radii=vec4(panelRadius);
    float d=roundedRectangleDist(position,halfSize,radii);
    float aa=max(fwidth(d),0.75);
    // Preserve upstream Snell optics but separate them from diffuse content.
    float inside=max(-d,0.0);
    float edgeFactor=1.0-clamp(inside/edgeSizePixels,0.0,1.0);
    float concave=1.0-sqrt(max(0.0,1.0-pow(smoothstep(0.0,1.0,edgeFactor),refractionNormalPow)));
    vec4 normalRadii=clamp(radii*2.0,min(64.0,min(halfSize.x,halfSize.y)),min(128.0,min(halfSize.x,halfSize.y)));
    if(controlMode>0)
        normalRadii=vec4(min(panelRadius*1.5,min(halfSize.x,halfSize.y)*0.9));
    vec3 optical=snellsRefraction(position,halfSize,normalRadii,
        min(halfSize.x,halfSize.y),d,concave).color.rgb;
    float clearRim=1.0-smoothstep(3.0,min(40.0,min(halfSize.x,halfSize.y)*0.8),inside);
    if(liquidStrength!=1.0) {
        float width=mix(0.35,1.65,clamp(liquidStrength/2.0,0.0,1.0));
        clearRim=(1.0-smoothstep(3.0*width,min(40.0,min(halfSize.x,halfSize.y)*0.8)*width,inside))
                 *min(liquidStrength,1.0);
    }
    vec3 rgb=mix(texture2D(bodyUnit,uv).rgb,optical,clearRim);
    float lum=dot(rgb,vec3(0.299,0.587,0.114));
    // Neutral adaptive scrim: no blue pigment or near-opaque graphite fill.
    // White reaches ~145/255 in the body, clear edges keep 88% transmission.
    float scrim=mix(0.16+0.27*smoothstep(0.25,0.95,lum),0.12,clearRim);
    rgb=mix(vec3(lum),rgb,chromaStrength)*(1.0-scrim*shadeStrength);
    // Nested glass samples the already-rendered panel, never icons/text.
    // Do not apply the panel's dark scrim or saturation a second time.
    if(controlMode>0) {
        rgb=mix(texture2D(bodyUnit,uv).rgb,optical,1.0-smoothstep(2.0,12.0,inside));
        rgb=mix(rgb,vec3(1.0),controlMode==2 ? 0.035 : 0.075);
        if(controlMode==2) rgb*=0.91;
    }
    rgb=applySoftMaterial(rgb,position,halfSize,radii,d,edgeFactor);
    rgb=mix(rgb,applyLiquidGlints(rgb,position,halfSize,radii,d,aa),highlightStrength);
    // Inner caustic: directional, curved, fades inward rather than a flat frame.
    vec2 normal=gradSdRoundedBox(position,halfSize,panelRadius);
    float facing=pow(max(dot(normalize(normal+vec2(0.0001)),
        normalize(vec2(-0.65,0.76))),0.0),3.0);
    float caustic=exp(-pow((inside-4.0)/2.4,2.0));
    rgb+=vec3(0.105,0.12,0.14)*caustic*facing*highlightStrength;
    float coverage=1.0-smoothstep(-aa*0.5,aa*0.5,d);
    if(shaped>0) coverage=texture2D(shapeUnit,uv).b;
    // QOpenGLFramebufferObject::toImage returns premultiplied ARGB.
    // Straight RGB at alpha=0 caused coloured square-corner leaks in QPainter.
    gl_FragColor=vec4(clamp(rgb,0.0,1.0)*coverage,coverage);
})GLSL";
    if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex)
        || !program.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment)
        || !program.link()) {
        qWarning() << "[NextKdeGlass] shader fallback:" << program.log();
        context.doneCurrent();
        return false;
    }
    context.doneCurrent();
    return true;
    }

    QImage draw(const QImage &body,const QImage &clear,QSize logical,float radius,const QImage &shape, float refraction, float shade, float highlight, float chroma, float liquidStrength, int control) {
    auto *gl=context.functions();
    auto &program=*programStorage;
    if (!framebuffer || framebuffer->size()!=body.size())
        framebuffer=std::make_unique<QOpenGLFramebufferObject>(body.size());
    auto &fbo=*framebuffer;
    if (!fbo.isValid() || !fbo.bind()) return {};
    QOpenGLTexture texture(clear.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    QOpenGLTexture bodyTexture(body.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    QImage shapeInput=shape;
    if (shapeInput.isNull()) { shapeInput=QImage(1,1,QImage::Format_RGB32); shapeInput.fill(Qt::black); }
    QOpenGLTexture shapeTexture(shapeInput.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    if (!texture.isCreated() || !bodyTexture.isCreated() || !shapeTexture.isCreated()) { fbo.release(); return {}; }
    texture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    texture.setWrapMode(QOpenGLTexture::ClampToEdge);
    bodyTexture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    bodyTexture.setWrapMode(QOpenGLTexture::ClampToEdge);
    shapeTexture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    shapeTexture.setWrapMode(QOpenGLTexture::ClampToEdge);
    shapeTexture.bind(2);
    bodyTexture.bind(1);
    texture.bind(0);
    gl->glViewport(0,0,body.width(),body.height());
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    program.bind();
    program.setUniformValue("texUnit",0);
    program.setUniformValue("bodyUnit",1);
    program.setUniformValue("shapeUnit",2);
    program.setUniformValue("shaped",shape.isNull() ? 0 : 1);
    program.setUniformValue("panelSize",QVector2D(logical.width(),logical.height()));
    program.setUniformValue("halfpixel",QVector2D(1.f/logical.width(),1.f/logical.height()));
    program.setUniformValue("viewportScale",1.f);
    program.setUniformValue("panelRadius",radius);
    program.setUniformValue("controlMode",control);
    program.setUniformValue("edgeSizePixels",control ? 5.5f : qMin(22.f,qMin(logical.width(),logical.height())*.24f));
    program.setUniformValue("refractionStrength",1.f);
    program.setUniformValue("refractionNormalPow",2.f);
    program.setUniformValue("refractionRGBFringing",control ? 0.35f : 0.65f);
    program.setUniformValue("refractionOffsetStrength",control ? 0.55f : refraction*liquidStrength);
    program.setUniformValue("liquidStrength",liquidStrength);
    program.setUniformValue("materialSoftness",0.f);
    program.setUniformValue("materialReflectionStrength",control ? 0.28f : 0.14f);
    program.setUniformValue("cornerExponent",2.f);
    program.setUniformValue("shadeStrength",shade/.58f);
    program.setUniformValue("highlightStrength",highlight/.55f);
    program.setUniformValue("chromaStrength",chroma);
    const GLfloat triangle[]={-1,-1,3,-1,-1,3};
    const int loc=program.attributeLocation("aPosition");
    program.enableAttributeArray(loc);
    program.setAttributeArray(loc,GL_FLOAT,triangle,2);
    gl->glDrawArrays(GL_TRIANGLES,0,3);
    program.disableAttributeArray(loc);
    const QImage result=fbo.toImage();
    program.release();
    fbo.release();
    return result;
    }

    QOffscreenSurface surface;
    QOpenGLContext context;
    std::unique_ptr<QOpenGLShaderProgram> programStorage;
    std::unique_ptr<QOpenGLFramebufferObject> framebuffer;
    bool attempted=false;
    bool ready=false;
};

static void initializeOpticsResources() { Q_INIT_RESOURCE(liquid_optics); }
LiquidOpticsRenderer::LiquidOpticsRenderer() { initializeOpticsResources(); }
void LiquidOpticsRenderer::setOptics(qreal refraction,qreal shade,qreal highlight,qreal chroma) {
    m_refraction=qBound(0.,refraction,8.);m_shade=qBound(.15,shade,.95);m_highlight=qBound(0.,highlight,1.);
    m_chroma=qBound(0.,chroma,1.3);
}
void LiquidOpticsRenderer::setMaterial(qreal clarity, qreal liquidStrength) {
    m_clarity=qBound(0.,clarity,1.);
    m_liquidStrength=qBound(0.,liquidStrength,2.);
}
LiquidOpticsRenderer::~LiquidOpticsRenderer() = default;

void LiquidOpticsRenderer::setWallpaper(const QImage &source)
{
    if (source.cacheKey() == m_source.cacheKey()
        && source.devicePixelRatio() == m_source.devicePixelRatio()) return;
    m_source = source;
    m_body = {};
    m_clear = {};
}

void LiquidOpticsRenderer::setPreparedWallpaper(const LiquidMaterial::Prepared &material)
{
    m_source = material.source;
    m_body = material.body;
    m_clear = material.clear;
    ++m_preparationCount;
}

QImage LiquidOpticsRenderer::renderPanel(const QRect &logicalRect, qreal radius, const QPainterPath &shape)
{
    return renderSurface(logicalRect,radius,shape,0);
}
QImage LiquidOpticsRenderer::renderControl(const QRect &logicalRect, qreal radius, bool pressed)
{
    return renderSurface(logicalRect,radius,{},pressed?2:1);
}
QImage LiquidOpticsRenderer::renderSurface(const QRect &logicalRect, qreal radius, const QPainterPath &shape, int control)
{
    m_usedGpu = false;
    if (m_source.isNull() || logicalRect.isEmpty()) return {};
    const qreal dpr = m_source.devicePixelRatio();
    if (m_body.isNull() && !control) {
        const QSize logical(qMax(1, qRound(m_source.width()/dpr)),
                            qMax(1, qRound(m_source.height()/dpr)));
        m_body = diffuse(m_source, logical);
        m_clear = diffuse(m_source, logical, 1, 2);
        m_body.setDevicePixelRatio(1);
        m_clear.setDevicePixelRatio(1);
        ++m_preparationCount;
    }
    const QRect pixels(qRound(logicalRect.x()*dpr), qRound(logicalRect.y()*dpr),
                       qMax(1, qRound(logicalRect.width()*dpr)),
                       qMax(1, qRound(logicalRect.height()*dpr)));
    QImage body = (control?m_source:m_body).copy(pixels);
    QImage clear = (control?m_source:m_clear).copy(pixels);
    body.setDevicePixelRatio(1);clear.setDevicePixelRatio(1);
    if(m_clarity>0) {
        QImage raw=m_source.copy(pixels);raw.setDevicePixelRatio(1);
        { QPainter p(&body);p.setOpacity(m_clarity);p.drawImage(0,0,raw); }
        { QPainter p(&clear);p.setOpacity(m_clarity);p.drawImage(0,0,raw); }
    }
    // Radius must fit collapsed title-only fences too.
    radius = qMin(radius, qMin(logicalRect.width(), logicalRect.height())/2.0);
    QImage result;
    if (!qEnvironmentVariableIsSet("UKUI_LIQUID_GLASS_NO_GL")) {
        if (!m_backend) m_backend = Backend::acquire();
        if (!shape.isEmpty() && (m_shapeField.isNull() || m_cachedShape != shape ||
            m_shapeSize != pixels.size() || !qFuzzyCompare(m_shapeDpr, dpr))) {
            m_shapeField = shapeField(shape, pixels.size(), dpr);
            m_cachedShape = shape;
            m_shapeSize = pixels.size();
            m_shapeDpr = dpr;
        }
        result = m_backend->render(body, clear, logicalRect.size(), radius,
                                  shape.isEmpty() ? QImage() : m_shapeField, m_refraction, m_shade, m_highlight, m_chroma, m_liquidStrength, control);
    }
    m_usedGpu = !result.isNull();
    if (!m_usedGpu) {
        // Readable, antialiased fallback, explicitly without Snell refraction.
        result = QImage(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter p(&result);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        if (shape.isEmpty())
            path.addRoundedRect(QRectF(QPointF(), QSizeF(result.size())), radius*dpr, radius*dpr);
        else
            path=QTransform::fromScale(dpr,dpr).map(shape);
        if(!qFuzzyCompare(m_liquidStrength,1.0)) {
            // Bounded row sampling, only on cache rebuild. CPU mode still gives
            // the strength control visible curvature, without a render loop.
            const QImage input=body;
            const qreal band=qMax(1.,qMin(22.,qMin(logicalRect.width(),logicalRect.height())*.24)*dpr);
            for(int y=0;y<body.height();++y) {
                auto *out=reinterpret_cast<QRgb *>(body.scanLine(y));
                for(int x=0;x<body.width();++x) {
                    const qreal edge=qBound(0.,1.-qMin(qMin(x,body.width()-1-x),qMin(y,body.height()-1-y))/band,1.);
                    const qreal bend=m_refraction*(m_liquidStrength-1.)*dpr*edge*edge;
                    const int sx=qBound(0,qRound(x+(x<body.width()/2?bend:-bend)),body.width()-1);
                    const int sy=qBound(0,qRound(y+(y<body.height()/2?bend:-bend)),body.height()-1);
                    out[x]=input.pixel(sx,sy);
                }
            }
        }
        p.drawImage(0, 0, body);
        p.fillRect(result.rect(), control ? (control==2?QColor(0,0,0,20):QColor(255,255,255,20)) : QColor(0, 0, 0, qRound(180*m_shade)));
        QImage mask(result.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        { QPainter mp(&mask); mp.setRenderHint(QPainter::Antialiasing); mp.fillPath(path, Qt::white); }
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, mask);
    }
    result.setDevicePixelRatio(dpr);
    return result;
}
