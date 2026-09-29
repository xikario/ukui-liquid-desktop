#include "../../../shared/liquid-glass/src/LiquidMaterial.h"
#include "NextKdeGlassView.h"
#include <QDebug>
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

} // namespace

// GUI-thread-only resources. No live rendering loop and no retained screen
// input textures: only context, linked program and reusable output FBO.
class NextKdeGlassView::Backend {
public:
    ~Backend() {
        const bool current=context.isValid() && context.makeCurrent(&surface);
        framebuffer.reset();
        programStorage.reset();
        if (current) context.doneCurrent();
    }

    QImage render(const QImage &body, const QImage &clear, QSize logical, float radius, int control=0) {
        if (!attempted) { attempted=true; ready=initialize(); }
        if (!ready || !context.makeCurrent(&surface)) return {};
        const QImage result=draw(body,clear,logical,radius,control);
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
    QByteArray upstream=resource(":/nextkde/glass.glsl");
    const QByteArray snells=resource(":/nextkde/snells-glass.glsl");
    if (upstream.isEmpty() || snells.isEmpty()) {
        context.doneCurrent();
        return false;
    }
    upstream.replace("#include \"snells-glass.glsl\"", snells);
    const QByteArray fragment = QByteArray(
        "#version 120\n#define texture texture2D\n"
        "uniform sampler2D texUnit; uniform sampler2D bodyUnit; varying vec2 uv;\n"
        "uniform vec2 halfpixel; uniform float viewportScale;\n"
        "uniform vec2 panelSize; uniform float panelRadius; uniform int controlMode;\n") + upstream + R"GLSL(
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
    vec4 normalRadii=clamp(radii*2.0,64.0,128.0);
    if(controlMode>0)
        normalRadii=vec4(min(panelRadius*1.5,min(halfSize.x,halfSize.y)*0.9));
    vec3 optical=snellsRefraction(position,halfSize,normalRadii,
        min(halfSize.x,halfSize.y),d,concave).color.rgb;
    float clearRim=1.0-smoothstep(8.0,40.0,inside);
    vec3 rgb=mix(texture2D(bodyUnit,uv).rgb,optical,clearRim);
    float lum=dot(rgb,vec3(0.299,0.587,0.114));
    // Neutral adaptive scrim: no blue pigment or near-opaque graphite fill.
    // White reaches ~145/255 in the body, clear edges keep 88% transmission.
    float scrim=mix(0.16+0.27*smoothstep(0.25,0.95,lum),0.12,clearRim);
    rgb=mix(vec3(lum),rgb,1.12)*(1.0-scrim);
    // Nested glass samples the already-rendered panel, never icons/text.
    // Do not apply the panel's dark scrim or saturation a second time.
    if(controlMode>0) {
        rgb=mix(texture2D(bodyUnit,uv).rgb,optical,1.0-smoothstep(2.0,12.0,inside));
        rgb=mix(rgb,vec3(1.0),controlMode==2 ? 0.035 : 0.075);
        if(controlMode==2) rgb*=0.91;
    }
    rgb=applySoftMaterial(rgb,position,halfSize,radii,d,edgeFactor);
    rgb=applyLiquidGlints(rgb,position,halfSize,radii,d,aa);
    // Inner caustic: directional, curved, fades inward rather than a flat frame.
    vec2 normal=gradSdRoundedBox(position,halfSize,panelRadius);
    float facing=pow(max(dot(normalize(normal+vec2(0.0001)),
        normalize(vec2(-0.65,0.76))),0.0),3.0);
    float caustic=exp(-pow((inside-4.0)/2.4,2.0));
    rgb+=vec3(0.105,0.12,0.14)*caustic*facing;
    float coverage=1.0-smoothstep(-aa*0.5,aa*0.5,d);
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

    QImage draw(const QImage &body,const QImage &clear,QSize logical,float radius,int control) {
    auto *gl=context.functions();
    auto &program=*programStorage;
    if (!framebuffer || framebuffer->size()!=body.size())
        framebuffer=std::make_unique<QOpenGLFramebufferObject>(body.size());
    auto &fbo=*framebuffer;
    if (!fbo.isValid() || !fbo.bind()) return {};
    QOpenGLTexture texture(clear.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    QOpenGLTexture bodyTexture(body.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
    if (!texture.isCreated() || !bodyTexture.isCreated()) return {};
    texture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    texture.setWrapMode(QOpenGLTexture::ClampToEdge);
    bodyTexture.setMinMagFilters(QOpenGLTexture::Linear,QOpenGLTexture::Linear);
    bodyTexture.setWrapMode(QOpenGLTexture::ClampToEdge);
    bodyTexture.bind(1);
    texture.bind(0);
    gl->glViewport(0,0,body.width(),body.height());
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    program.bind();
    program.setUniformValue("texUnit",0);
    program.setUniformValue("bodyUnit",1);
    program.setUniformValue("panelSize",QVector2D(logical.width(),logical.height()));
    program.setUniformValue("halfpixel",QVector2D(1.f/logical.width(),1.f/logical.height()));
    program.setUniformValue("viewportScale",1.f);
    program.setUniformValue("panelRadius",radius);
    program.setUniformValue("controlMode",control);
    program.setUniformValue("edgeSizePixels",control ? 5.5f : 22.f);
    program.setUniformValue("refractionStrength",1.f);
    program.setUniformValue("refractionNormalPow",2.f);
    program.setUniformValue("refractionRGBFringing",control ? 0.35f : 0.65f);
    program.setUniformValue("refractionOffsetStrength",control ? 0.55f : 3.5f);
    program.setUniformValue("materialSoftness",0.f);
    program.setUniformValue("materialReflectionStrength",control ? 0.28f : 0.14f);
    program.setUniformValue("cornerExponent",2.f);
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

NextKdeGlassView::NextKdeGlassView(QObject *parent) : QObject(parent) {}
NextKdeGlassView::~NextKdeGlassView() = default;

qreal NextKdeGlassView::luminanceAt(const QRectF &logicalRect) const
{
    if (m_image.isNull()) return 0;
    const qreal dpr=m_image.devicePixelRatio();
    const QRectF bounds(0,0,m_image.width()/dpr,m_image.height()/dpr);
    const QRectF area=logicalRect.intersected(bounds);
    if (area.isEmpty()) return 0;
    qreal sum=0;
    for(int y=0;y<3;++y) for(int x=0;x<5;++x) {
        const int px=qBound(0,int((area.left()+area.width()*(x+0.5)/5)*dpr),m_image.width()-1);
        const int py=qBound(0,int((area.top()+area.height()*(y+0.5)/3)*dpr),m_image.height()-1);
        const QColor c=m_image.pixelColor(px,py);
        sum+=(0.299*c.redF()+0.587*c.greenF()+0.114*c.blueF())*c.alphaF();
    }
    return sum/15;
}

void NextKdeGlassView::setBackdrop(const QImage &source)
{
    m_controls.clear();
    m_controlRenderCount=0;
    m_image={};
    m_usedGpu=false;
    m_lastRenderMs=0;
    if (source.isNull()) return;
    QElapsedTimer timer;
    timer.start();
    const qreal dpr=source.devicePixelRatio();
    const QSize logical(qMax(1,qRound(source.width()/dpr)),
                        qMax(1,qRound(source.height()/dpr)));
    QImage body=diffuse(source,logical);
    body.setDevicePixelRatio(1);
    QImage clear=diffuse(source,logical,1,2);
    clear.setDevicePixelRatio(1);
    if (!qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_NO_GL")) {
        if (!m_backend) {
            m_backend=std::make_unique<Backend>();
            ++m_gpuInitializationCount;
        }
        // The panel is displayed at a large size, but its optical rim does
        // not benefit from rendering every physical pixel. Render the heavy
        // shader at 2/3 resolution and upscale once; this cuts the FBO work
        // by more than half while retaining the broad liquid refraction.
        constexpr qreal kRenderScale = 0.67;
        const QSize renderSize(qMax(1, qRound(source.width()*kRenderScale)),
                               qMax(1, qRound(source.height()*kRenderScale)));
        const QImage bodySmall = body.scaled(renderSize, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
        const QImage clearSmall = clear.scaled(renderSize, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation);
        QImage rendered = m_backend->render(bodySmall,clearSmall,logical,m_radius);
        if (!rendered.isNull() && rendered.size() != source.size())
            rendered = rendered.scaled(source.size(), Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
        m_image=rendered;
    }
    m_usedGpu=!m_image.isNull();
    if (!m_usedGpu) {
        m_image=QImage(source.size(),QImage::Format_ARGB32_Premultiplied);
        m_image.fill(Qt::transparent);
        QPainter p(&m_image);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(QPointF(),QSizeF(source.size())),m_radius*dpr,m_radius*dpr);
        p.setClipPath(path);
        p.drawImage(0,0,body);
        p.fillPath(path,QColor(0,0,0,105));
        p.setPen(QPen(QColor(255,255,255,65),dpr));
        p.drawPath(path);
    }
    m_image.setDevicePixelRatio(dpr);
    m_lastRenderMs=timer.elapsed();
    qInfo() << "[NextKdeGlass]" << (m_usedGpu ? "upstream Snell/glints" : "CPU fallback")
            << source.size() << "DPR" << dpr << m_lastRenderMs << "ms";
}

void NextKdeGlassView::setBackdropFast(const QImage &source)
{
    m_controls.clear();
    m_controlRenderCount=0;
    m_image={};
    m_usedGpu=false;
    m_lastRenderMs=0;
    if (source.isNull()) return;
    QElapsedTimer timer;
    timer.start();
    const qreal dpr=source.devicePixelRatio();
    const QSize logical(qMax(1,qRound(source.width()/dpr)),
                        qMax(1,qRound(source.height()/dpr)));
    QImage material=diffuse(source,logical);
    material.setDevicePixelRatio(1);
    const QImage clear=source.convertToFormat(QImage::Format_RGB32);
    const int w=material.width(), h=material.height();
    const qreal radius=qMin(qreal(m_radius)*dpr, qMin(w,h)*0.5);
    const qreal rim=28*dpr;
    // The body needs only diffusion. Compute refraction on the narrow rim,
    // keeping icons/text outside this image and avoiding any GL resources.
    for (int y=0; y<h; ++y) {
        QRgb *out=reinterpret_cast<QRgb *>(material.scanLine(y));
        for (int x=0; x<w; ++x) {
            const qreal px=x+0.5-w*0.5, py=y+0.5-h*0.5;
            const qreal qx=qAbs(px)-(w*0.5-radius), qy=qAbs(py)-(h*0.5-radius);
            const qreal ox=qMax(qreal(0),qx), oy=qMax(qreal(0),qy);
            const qreal len=(ox>0 && oy>0) ? std::sqrt(ox*ox+oy*oy) : qMax(ox,oy);
            const qreal inside=radius-len-qMin(qMax(qx,qy),qreal(0));
            const QRgb base=out[x];
            qreal red=qRed(base), green=qGreen(base), blue=qBlue(base);
            qreal edge=0, nx=0, ny=0;
            if (inside<rim) {
                edge=qBound(qreal(0),1-inside/rim,qreal(1));
                if (len>0) { nx=ox/len; ny=oy/len; }
                else if (qx>qy) nx=1;
                else ny=1;
                if (px<0) nx=-nx;
                if (py<0) ny=-ny;
                const qreal bend=12*dpr*edge*edge;
                const int sx=qBound(0,qRound(x-nx*bend),w-1);
                const int sy=qBound(0,qRound(y-ny*bend),h-1);
                const QRgb refracted=reinterpret_cast<const QRgb *>(clear.constScanLine(sy))[sx];
                red+=(qRed(refracted)-red)*edge;
                green+=(qGreen(refracted)-green)*edge;
                blue+=(qBlue(refracted)-blue)*edge;
            }
            const qreal lum=(0.299*red+0.587*green+0.114*blue)/255;
            const qreal t=qBound(qreal(0),(lum-0.25)/0.70,qreal(1));
            const qreal scrim=(0.16+0.27*t*t*(3-2*t))*(1-edge)+0.12*edge;
            const qreal light=qMax(qreal(0),-0.65*nx-0.76*ny)*edge*edge*22;
            out[x]=qRgb(qBound(0,qRound(red*(1-scrim)+light),255),
                        qBound(0,qRound(green*(1-scrim)+light),255),
                        qBound(0,qRound(blue*(1-scrim)+light),255));
        }
    }
    m_image=QImage(source.size(),QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);
    {
        QPainter p(&m_image);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(0,0,w,h),radius,radius);
        p.setClipPath(shape);
        p.drawImage(0,0,material);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255,255,255,65),dpr));
        p.drawPath(shape);
    }
    m_image.setDevicePixelRatio(dpr);
    m_lastRenderMs=timer.elapsed();
    qInfo() << "[NextKdeGlass] fast CPU refraction" << source.size()
            << "DPR" << dpr << m_lastRenderMs << "ms";
}

QImage NextKdeGlassView::controlImage(const QRectF &rect, qreal radius, bool pressed)
{
    if(m_image.isNull() || rect.width()<2 || rect.height()<2) return {};
    const qreal dpr=m_image.devicePixelRatio();
    const QRect pixels(qRound(rect.x()*dpr),qRound(rect.y()*dpr),
                       qRound(rect.width()*dpr),qRound(rect.height()*dpr));
    // Never stretch an intersected crop at a clipped/scrolling boundary.
    if(!m_image.rect().contains(pixels)) return {};
    const QString key=QString("%1,%2,%3,%4/%5/%6")
        .arg(pixels.x()).arg(pixels.y()).arg(pixels.width()).arg(pixels.height())
        .arg(radius).arg(pressed);
    if(auto *cached=m_controls.object(key)) return *cached;
    QImage crop=m_image.copy(pixels);
    crop.setDevicePixelRatio(1);
    QImage result;
    if(m_usedGpu && m_backend && !qEnvironmentVariableIsSet("KAISHICAIDAN_GLASS_NO_GL"))
        result=m_backend->render(crop,crop,rect.size().toSize(),radius,pressed ? 2 : 1);
    if(result.isNull()) {
        result=QImage(pixels.size(),QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter p(&result);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(QPointF(),QSizeF(pixels.size())),radius*dpr,radius*dpr);
        p.setClipPath(shape);
        p.drawImage(0,0,crop);
        p.fillPath(shape,pressed ? QColor(0,0,0,20) : QColor(255,255,255,20));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255,255,255,95),dpr));
        p.drawPath(shape);
    }
    result.setDevicePixelRatio(dpr);
    ++m_controlRenderCount;
    m_controls.insert(key,new QImage(result),qMax(1,int(result.sizeInBytes()/1024)));
    return result;
}
