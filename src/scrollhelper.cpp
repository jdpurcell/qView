#include "scrollhelper.h"
#include <QDebug>
#include <QtMath>

ScrollHelper::ScrollHelper(QAbstractScrollArea *parent, GetParametersCallback getParametersCallback) : QObject(parent)
{
    hScrollBar = parent->horizontalScrollBar();
    vScrollBar = parent->verticalScrollBar();
    this->getParametersCallback = getParametersCallback;

    animatedScrollTimer = new QTimer(this);
    animatedScrollTimer->setSingleShot(true);
    animatedScrollTimer->setTimerType(Qt::PreciseTimer);
    animatedScrollTimer->setInterval(10);
    connect(animatedScrollTimer, &QTimer::timeout, this, [this]{ handleAnimatedScroll(); });
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
    QPointF scrollLocation = QPointF(hScrollBar->value(), vScrollBar->value()) + lastMoveRoundingError;
    qreal scrollDeltaX = delta.x();
    qreal scrollDeltaY = delta.y();
    if (p.shouldConstrain)
    {
        scrollDeltaX = calculateScrollDelta(scrollLocation.x(), hMin, hMax, scrollDeltaX);
        scrollDeltaY = calculateScrollDelta(scrollLocation.y(), vMin, vMax, scrollDeltaY);
    }
    scrollLocation += QPointF(scrollDeltaX, scrollDeltaY);
    int scrollValueX = qAbs(scrollLocation.x()) == 0.5 ? 0 : qRound(scrollLocation.x());
    int scrollValueY = qAbs(scrollLocation.y()) == 0.5 ? 0 : qRound(scrollLocation.y());
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
        applyScrollDelta(-overscrollDistance);
    else
        beginAnimatedScroll(-overscrollDistance);
}

void ScrollHelper::cancelAnimation()
{
    animatedScrollTimer->stop();
}

void ScrollHelper::beginAnimatedScroll(QPoint delta)
{
    if (delta.isNull())
        return;
    animatedScrollTotalDelta = delta;
    animatedScrollAppliedDelta = {};
    animatedScrollElapsed.start();
    animatedScrollTimer->start();
}

void ScrollHelper::handleAnimatedScroll()
{
    qreal elapsed = animatedScrollElapsed.elapsed();
    if (elapsed >= animatedScrollDuration)
    {
        applyScrollDelta(animatedScrollTotalDelta - animatedScrollAppliedDelta);
    }
    else
    {
        QPoint intermediateDelta = animatedScrollTotalDelta * smoothAnimation(elapsed / animatedScrollDuration);
        applyScrollDelta(intermediateDelta - animatedScrollAppliedDelta);
        animatedScrollAppliedDelta = intermediateDelta;
        animatedScrollTimer->start();
    }
}

void ScrollHelper::applyScrollDelta(QPoint delta)
{
    if (delta.x() != 0)
        hScrollBar->setValue(hScrollBar->value() + delta.x());
    if (delta.y() != 0)
        vScrollBar->setValue(vScrollBar->value() + delta.y());
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

// Converts linear motion from [0,1] into something that looks more natural. Derived from the
// formula for a circle, i.e. the graph of this is literally the top-left quarter of a circle.
qreal ScrollHelper::smoothAnimation(qreal x)
{
    return qPow(1.0 - qPow(x - 1.0, 2.0), 0.5);
}
