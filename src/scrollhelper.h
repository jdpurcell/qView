#ifndef SCROLLHELPER_H
#define SCROLLHELPER_H

#include "QtWidgets/qscrollbar.h"

class ScrollHelper
{
public:
    void begin(QScrollBar *hScrollBar, QScrollBar *vScrollBar);

    void move(QSize scaledContentSize, QRect usableViewportRect, qreal deltaX, qreal deltaY);

    void end();

private:
    static void calculateScrollRange(int contentDimension, int viewportDimension, int offset, int &minValue, int &maxValue);

    static qreal calculateScrollDelta(qreal currentValue, int minValue, int maxValue, qreal proposedDelta);

    bool isInProgress {false};
    QScrollBar *hScrollBar;
    QScrollBar *vScrollBar;
    QPointF lastMoveRoundingError;
    QPoint overscrollDistance;
};

#endif // SCROLLHELPER_H
