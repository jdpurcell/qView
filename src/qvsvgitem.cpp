#include "qvsvgitem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QSvgRenderer>

QVSvgItem::QVSvgItem(QSvgRenderer *renderer)
{
    setSharedRenderer(renderer);
    connect(renderer, &QSvgRenderer::repaintNeeded, this, [this] { update(); });
    setFlag(ItemUsesExtendedStyleOption);
    setCacheMode(NoCache);
}

void QVSvgItem::setTargetColorSpace(const QColorSpace &colorSpace)
{
    targetColorSpace = colorSpace;
    update();
}

void QVSvgItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    if (!targetColorSpace.isValid() || targetColorSpace == QColorSpace::SRgb)
    {
        QGraphicsSvgItem::paint(painter, option, widget);
        return;
    }

    // Keep the intermediate surface bounded by the paint device even at extreme zoom.
    // Use viewport-local pixels even when QWidget redirects painting into a backing store.
    const QTransform toDevice = painter->combinedTransform();
    const QPaintDevice *device = painter->device();
    const QSize deviceSize = widget ? widget->size() * device->devicePixelRatioF() :
                                      QSize(device->width(), device->height());
    const QRect pixels = toDevice.mapRect(option->exposedRect).intersected(QRectF(QPoint(), deviceSize)).toAlignedRect();
    if (pixels.isEmpty())
        return;

    QImage image(pixels.size(), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return;
    image.fill(Qt::transparent);
    QPainter buffer(&image);
    buffer.setRenderHints(painter->renderHints());
    buffer.setWorldTransform(toDevice * QTransform::fromTranslate(-pixels.x(), -pixels.y()));
    renderer()->render(&buffer, boundingRect());
    buffer.end();
    image.setColorSpace(QColorSpace::SRgb);
    image.convertToColorSpace(targetColorSpace);

    // The buffer already includes the scene transform and device pixel ratio.
    painter->save();
    painter->setWorldTransform(toDevice.inverted(), true);
    painter->drawImage(pixels.topLeft(), image);
    painter->restore();
}
