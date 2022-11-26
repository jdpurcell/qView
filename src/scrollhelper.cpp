#include "scrollhelper.h"

void ScrollHelper::begin(QScrollBar *hScrollBar, QScrollBar *vScrollBar)
{
    this->hScrollBar = hScrollBar;
    this->vScrollBar = vScrollBar;
    lastMoveRoundingError = {};
    overscrollDistance = {};
}

void ScrollHelper::move(QSize scaledContentSize, QRect usableViewportRect, int deltaX, int deltaY)
{
    int hMin, hMax, vMin, vMax;
    calculateScrollRange(scaledContentSize.width(), usableViewportRect.width(), -usableViewportRect.left(), hMin, hMax);
    calculateScrollRange(scaledContentSize.height(), usableViewportRect.height(), -usableViewportRect.top(), vMin, vMax);
    QPointF scrollLocation = QPointF(hScrollBar->value(), vScrollBar->value()) + lastMoveRoundingError;
    qreal scrollDeltaX = calculateScrollDelta(scrollLocation.x(), hMin, hMax, hScrollBar->isRightToLeft() ? -deltaX : deltaX);
    qreal scrollDeltaY = calculateScrollDelta(scrollLocation.y(), vMin, vMax, deltaY);
    scrollLocation += QPointF(scrollDeltaX, scrollDeltaY);
    int scrollValueX = qRound(scrollLocation.x());
    int scrollValueY = qRound(scrollLocation.y());
    lastMoveRoundingError = QPointF(scrollLocation.x() - scrollValueX, scrollLocation.y() - scrollValueY);
    int overscrollDistanceX =
        scrollValueX < hMin ? scrollValueX - hMin :
        scrollValueX > hMax ? scrollValueX - hMax :
        0;
    int overscrollDistanceY =
        scrollValueY < vMin ? scrollValueY - vMin :
        scrollValueY > vMax ? scrollValueY - vMax :
        0;
    overscrollDistance = QPoint(overscrollDistanceX, overscrollDistanceY);
    hScrollBar->setValue(scrollValueX);
    vScrollBar->setValue(scrollValueY);
}

void ScrollHelper::end()
{
    if (overscrollDistance.x() != 0)
        hScrollBar->setValue(hScrollBar->value() - overscrollDistance.x());
    if (overscrollDistance.y() != 0)
        vScrollBar->setValue(vScrollBar->value() - overscrollDistance.y());
}

void ScrollHelper::calculateScrollRange(int contentDimension, int viewportDimension, int offset, int &minValue, int &maxValue)
{
    int overflow = contentDimension - viewportDimension;
    if (overflow >= 0)
    {
        minValue = offset;
        maxValue = overflow + offset;
    }
    else
    {
        minValue = overflow / 2 + offset;
        maxValue = minValue;
    }
}

qreal ScrollHelper::calculateScrollDelta(qreal currentValue, int minValue, int maxValue, int proposedDelta)
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
