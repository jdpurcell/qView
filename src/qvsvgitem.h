#ifndef QVSVGITEM_H
#define QVSVGITEM_H

#include <QColorSpace>
#include <QGraphicsSvgItem>

class QVSvgItem : public QGraphicsSvgItem
{
public:
    explicit QVSvgItem(QSvgRenderer *renderer);
    void setTargetColorSpace(const QColorSpace &colorSpace);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QColorSpace targetColorSpace;
};

#endif // QVSVGITEM_H
