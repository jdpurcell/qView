#include "scrollhelper.h"
#include <QDebug>
#include <QtMath>

ScrollHelper::ScrollHelper(QAbstractScrollArea *parent, GetParametersCallback getParametersCallback) : QObject(parent)
{
    hScrollBar = parent->horizontalScrollBar();
    vScrollBar = parent->verticalScrollBar();
    this->getParametersCallback = getParametersCallback;

    scrollAnimation = new QVariantAnimation(this);
    scrollAnimation->setDuration(animatedScrollDuration);
    scrollAnimation->setEasingCurve(QEasingCurve::OutCirc);
    connect(scrollAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setScrollPosition(value.toPoint());
    });
}

void ScrollHelper::move(QPointF delta)
{
    Parameters p;
    getParametersCallback(p);
    if (!p.contentRect.isValid() || !p.usableViewportRect.isValid())
    {
        overscrollDistance = {};
        return;
    }
    bool isRightToLeft = hScrollBar->isRightToLeft();
    int hMin, hMax, vMin, vMax;
    calculateScrollRange(
        p.contentRect.width(),
        p.usableViewportRect.width(),
        isRightToLeft ?
            hScrollBar->minimum() + hScrollBar->maximum() + p.usableViewportRect.width() - p.contentRect.left() - p.contentRect.width() :
            p.contentRect.left(),
        p.shouldCenter,
        hMin,
        hMax
    );
    calculateScrollRange(
        p.contentRect.height(),
        p.usableViewportRect.height(),
        p.contentRect.top() - p.usableViewportRect.top(),
        p.shouldCenter,
        vMin,
        vMax
    );
    QPointF scrollLocation = getScrollPosition() + lastMoveRoundingError;
    qreal scrollDeltaX = delta.x();
    qreal scrollDeltaY = delta.y();
    if (p.shouldConstrain)
    {
        scrollDeltaX = calculateScrollDelta(scrollLocation.x(), hMin, hMax, scrollDeltaX);
        scrollDeltaY = calculateScrollDelta(scrollLocation.y(), vMin, vMax, scrollDeltaY);
    }
    scrollLocation += QPointF(scrollDeltaX, scrollDeltaY);
    int scrollValueX = qRound(scrollLocation.x());
    int scrollValueY = qRound(scrollLocation.y());
    lastMoveRoundingError = QPointF(scrollLocation.x() - scrollValueX, scrollLocation.y() - scrollValueY);
    int overscrollDistanceX =
        p.shouldConstrain && scrollValueX < hMin ? scrollValueX - hMin :
        p.shouldConstrain && scrollValueX > hMax ? scrollValueX - hMax :
        0;
    int overscrollDistanceY =
        p.shouldConstrain && scrollValueY < vMin ? scrollValueY - vMin :
        p.shouldConstrain && scrollValueY > vMax ? scrollValueY - vMax :
        0;
    overscrollDistance = QPoint(overscrollDistanceX, overscrollDistanceY);
    hScrollBar->setValue(scrollValueX);
    vScrollBar->setValue(scrollValueY);
}

void ScrollHelper::constrain(bool skipAnimation)
{
    // Zero-delta movement to calculate overscroll distance
    move(QPointF());

    if (skipAnimation)
        setScrollPosition(getScrollPosition() - overscrollDistance);
    else
        beginAnimatedScroll(-overscrollDistance);
}

void ScrollHelper::cancelAnimation()
{
    scrollAnimation->stop();
}

void ScrollHelper::beginAnimatedScroll(QPoint delta)
{
    if (delta.isNull())
        return;
    QPoint startPos = getScrollPosition();
    scrollAnimation->setStartValue(startPos);
    scrollAnimation->setEndValue(startPos + delta);
    scrollAnimation->start();
}

QPoint ScrollHelper::getScrollPosition()
{
    return QPoint(hScrollBar->value(), vScrollBar->value());
}

void ScrollHelper::setScrollPosition(QPoint pos)
{
    hScrollBar->setValue(pos.x());
    vScrollBar->setValue(pos.y());
}

void ScrollHelper::calculateScrollRange(int contentDimension, int viewportDimension, int offset, bool shouldCenter, int &minValue, int &maxValue)
{
    int overflow = contentDimension - viewportDimension;
    if (overflow >= 0)
    {
        minValue = offset;
        maxValue = overflow + offset;
    }
    else if (shouldCenter)
    {
        minValue = overflow / 2 + offset;
        maxValue = minValue;
    }
    else
    {
        minValue = overflow + offset;
        maxValue = offset;
    }
}

qreal ScrollHelper::calculateScrollDelta(qreal currentValue, int minValue, int maxValue, qreal proposedDelta)
{
    const double overflowScaleFactor = 0.05;
    if (proposedDelta < 0 && currentValue + proposedDelta < minValue)
    {
        return currentValue <= minValue ? proposedDelta * overflowScaleFactor :
            (minValue - currentValue) + ((currentValue + proposedDelta) - minValue) * overflowScaleFactor;
    }
    if (proposedDelta > 0 && currentValue + proposedDelta > maxValue)
    {
        return currentValue >= maxValue ? proposedDelta * overflowScaleFactor :
            (maxValue - currentValue) + ((currentValue + proposedDelta) - maxValue) * overflowScaleFactor;
    }
    return proposedDelta;
}
